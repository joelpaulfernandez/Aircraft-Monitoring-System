/*
test_divert_command.c
Unit tests for server-side emergency divert command (Sprint 2, Task 2).

Compiled with -DTESTING and -DSERVER_PORT=18080.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../common/packet.h"
#include "../../server/include/server.h"

// Test harness

static int tests_run = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    tests_run++;                                                               \
    if (!(condition)) {                                                        \
      tests_failed++;                                                          \
      printf("  FAIL: %s\n", (msg));                                           \
    } else {                                                                   \
      printf("  PASS: %s\n", (msg));                                           \
    }                                                                          \
  } while (0)

#define RUN_TEST(name)                                                         \
  do {                                                                         \
    printf("[TEST] %s\n", #name);                                              \
    name();                                                                    \
    resetAircraftRecords();                                                    \
    resetClients();                                                            \
  } while (0)

// Helper: create a minimal valid FuelPacket on the stack for testing.
static FuelPacket makeTestPacket(int aircraftID, float fuelLevel) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.packetID = 1;
  pkt.header.type = FUEL_STATUS;
  pkt.header.aircraftID = aircraftID;
  pkt.header.timestamp = time(NULL);
  pkt.body.fuelLevel = fuelLevel;
  pkt.body.currentState = STATE_NORMAL_CRUISE;
  pkt.body.nearestAirportID = 5;
  pkt.body.destinationAirportID = 10;
  pkt.body.flightTimeRemaining = 120.0f;
  pkt.body.timeToDestination = 90.0f;
  pkt.body.alertMessage = NULL;
  return pkt;
}

// ─── Group A: evaluateDivertDecision — US5 divert logic ─────────────

// Test 1: DIVERT when flightTimeRemaining < timeToDestination and CRITICAL
static void test_divert_when_insufficient_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  pkt.body.nearestAirportID = 42;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == true,
              "evaluateDivertDecision returns true when "
              "flightTimeRemaining < timeToDestination in CRITICAL state");
}

// Test 2: NO divert when flightTimeRemaining >= timeToDestination
static void test_no_divert_when_sufficient_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 90.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false when "
              "flightTimeRemaining >= timeToDestination");
}

// Test 3: NO divert when flightTimeRemaining == timeToDestination (equal)
static void test_no_divert_when_equal_times(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 60.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false when "
              "flightTimeRemaining == timeToDestination");
}

// Test 4: NO divert when state is not CRITICAL_FUEL (NORMAL)
static void test_no_divert_when_normal_state(void) {
  FuelPacket pkt = makeTestPacket(101, 50.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false in NORMAL_CRUISE state");
}

// Test 5: NO divert when state is LOW_FUEL (not yet critical)
static void test_no_divert_when_low_fuel_state(void) {
  FuelPacket pkt = makeTestPacket(101, 20.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false in LOW_FUEL state");
}

// Test 6: NO divert for unknown aircraftID
static void test_no_divert_unknown_aircraft(void) {
  bool shouldDivert = evaluateDivertDecision(9999);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false for unknown aircraftID");
}

// Test 7: NO divert when already in EMERGENCY_DIVERT state
static void test_no_divert_when_already_diverted(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  // Simulate that divert was already issued
  AircraftRecord *rec = getAircraftRecord(101);
  rec->currentState = STATE_EMERGENCY_DIVERT;
  rec->awaitingACK = true;

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "evaluateDivertDecision returns false when already "
              "in EMERGENCY_DIVERT state");
}

// ─── Group B: broadcastDivertCommand — send and record ──────────────

// Test 8: broadcastDivertCommand sets awaitingACK and divertCommandTime
static void test_broadcast_sets_ack_fields(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  pkt.body.nearestAirportID = 42;
  updateAircraftRecord(&pkt);

  time_t before = time(NULL);
  int result = broadcastDivertCommand(101);
  time_t after = time(NULL);

  TEST_ASSERT(result == 0,
              "broadcastDivertCommand returns 0 on success");

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->awaitingACK == true,
              "awaitingACK set to true after divert command");
  TEST_ASSERT(rec != NULL && rec->divertCommandTime >= before &&
                  rec->divertCommandTime <= after,
              "divertCommandTime set to current time");
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_EMERGENCY_DIVERT,
              "State set to EMERGENCY_DIVERT after divert command");
}

// Test 9: broadcastDivertCommand returns -1 for unknown aircraft
static void test_broadcast_unknown_aircraft(void) {
  int result = broadcastDivertCommand(9999);
  TEST_ASSERT(result == -1,
              "broadcastDivertCommand returns -1 for unknown aircraftID");
}

// Test 10: broadcastDivertCommand returns -1 when already awaiting ACK
static void test_broadcast_already_awaiting_ack(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  // First divert should succeed
  int result1 = broadcastDivertCommand(101);
  TEST_ASSERT(result1 == 0,
              "First broadcastDivertCommand succeeds");

  // Second divert should fail (already awaiting ACK)
  int result2 = broadcastDivertCommand(101);
  TEST_ASSERT(result2 == -1,
              "broadcastDivertCommand returns -1 when already awaiting ACK");
}

// ─── Group C: Integration — evaluate + broadcast combined ───────────

// Test 11: Full flow — critical fuel with insufficient time triggers divert
static void test_full_divert_flow(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 30.0f;
  pkt.body.timeToDestination = 60.0f;
  pkt.body.nearestAirportID = 42;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == true,
              "Divert decision is true for critical aircraft");

  if (shouldDivert) {
    int result = broadcastDivertCommand(101);
    TEST_ASSERT(result == 0,
                "broadcastDivertCommand succeeds after positive decision");
  }

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_EMERGENCY_DIVERT,
              "Aircraft in EMERGENCY_DIVERT after full flow");
  TEST_ASSERT(rec != NULL && rec->awaitingACK == true,
              "Aircraft awaiting ACK after full flow");
}

// Test 12: Full flow — critical fuel with sufficient time does NOT trigger
static void test_full_no_divert_flow(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  pkt.body.flightTimeRemaining = 90.0f;
  pkt.body.timeToDestination = 60.0f;
  updateAircraftRecord(&pkt);

  bool shouldDivert = evaluateDivertDecision(101);
  TEST_ASSERT(shouldDivert == false,
              "Divert decision is false when fuel is sufficient");

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_CRITICAL_FUEL,
              "Aircraft remains in CRITICAL_FUEL (no divert issued)");
  TEST_ASSERT(rec != NULL && rec->awaitingACK == false,
              "Aircraft not awaiting ACK");
}

// ─── Main ───────────────────────────────────────────────────────────

int main(void) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return EXIT_FAILURE;
  }
#endif

  printf("=== Emergency Divert Command Unit Tests ===\n\n");

  // Group A: evaluateDivertDecision
  RUN_TEST(test_divert_when_insufficient_fuel);
  RUN_TEST(test_no_divert_when_sufficient_fuel);
  RUN_TEST(test_no_divert_when_equal_times);
  RUN_TEST(test_no_divert_when_normal_state);
  RUN_TEST(test_no_divert_when_low_fuel_state);
  RUN_TEST(test_no_divert_unknown_aircraft);
  RUN_TEST(test_no_divert_when_already_diverted);

  // Group B: broadcastDivertCommand
  RUN_TEST(test_broadcast_sets_ack_fields);
  RUN_TEST(test_broadcast_unknown_aircraft);
  RUN_TEST(test_broadcast_already_awaiting_ack);

  // Group C: Integration
  RUN_TEST(test_full_divert_flow);
  RUN_TEST(test_full_no_divert_flow);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);

#ifdef _WIN32
  WSACleanup();
#endif

  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
