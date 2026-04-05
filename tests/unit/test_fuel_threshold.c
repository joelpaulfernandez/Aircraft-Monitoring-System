/*
test_fuel_threshold.c
Unit tests for server-side fuel threshold detection (Sprint 2).

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

// ─── Group A: checkFuelThresholds — state determination ─────────────

// Test 1: fuel above 25% returns STATE_NORMAL_CRUISE
static void test_check_threshold_normal_cruise(void) {
  FuelPacket pkt = makeTestPacket(101, 50.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_NORMAL_CRUISE,
              "checkFuelThresholds(50.0) returns STATE_NORMAL_CRUISE");
}

// Test 2: fuel exactly at 25% returns STATE_LOW_FUEL (boundary)
static void test_check_threshold_at_low_boundary(void) {
  FuelPacket pkt = makeTestPacket(101, 25.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_LOW_FUEL,
              "checkFuelThresholds(25.0) returns STATE_LOW_FUEL");
}

// Test 3: fuel between 15-25% returns STATE_LOW_FUEL
static void test_check_threshold_low_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 20.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_LOW_FUEL,
              "checkFuelThresholds(20.0) returns STATE_LOW_FUEL");
}

// Test 4: fuel exactly at 15% returns STATE_CRITICAL_FUEL (boundary)
static void test_check_threshold_at_critical_boundary(void) {
  FuelPacket pkt = makeTestPacket(101, 15.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_CRITICAL_FUEL,
              "checkFuelThresholds(15.0) returns STATE_CRITICAL_FUEL");
}

// Test 5: fuel below 15% returns STATE_CRITICAL_FUEL
static void test_check_threshold_critical_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 10.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_CRITICAL_FUEL,
              "checkFuelThresholds(10.0) returns STATE_CRITICAL_FUEL");
}

// Test 6: fuel at 0% returns STATE_CRITICAL_FUEL
static void test_check_threshold_zero_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 0.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_CRITICAL_FUEL,
              "checkFuelThresholds(0.0) returns STATE_CRITICAL_FUEL");
}

// Test 7: fuel at 100% returns STATE_NORMAL_CRUISE
static void test_check_threshold_full_fuel(void) {
  FuelPacket pkt = makeTestPacket(101, 100.0f);
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_NORMAL_CRUISE,
              "checkFuelThresholds(100.0) returns STATE_NORMAL_CRUISE");
}

// ─── Group B: updateAircraftRecord — record management ──────────────

// Test 8: updateAircraftRecord creates a new record
static void test_update_creates_new_record(void) {
  FuelPacket pkt = makeTestPacket(101, 50.0f);
  int result = updateAircraftRecord(&pkt);
  TEST_ASSERT(result == 0, "updateAircraftRecord() returns 0 for new aircraft");

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL,
              "getAircraftRecord(101) returns non-NULL after creation");
}

// Test 9: updateAircraftRecord updates existing record state
static void test_update_updates_existing_record(void) {
  FuelPacket pkt1 = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt1);

  FuelPacket pkt2 = makeTestPacket(101, 20.0f);
  int result = updateAircraftRecord(&pkt2);
  TEST_ASSERT(result == 0,
              "updateAircraftRecord() returns 0 for existing aircraft");

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_LOW_FUEL,
              "Record state updated to STATE_LOW_FUEL after fuel=20.0");
}

// Test 10: updateAircraftRecord copies packet fields into record
static void test_update_copies_packet_fields(void) {
  FuelPacket pkt = makeTestPacket(101, 50.0f);
  pkt.body.nearestAirportID = 42;
  pkt.body.destinationAirportID = 99;
  pkt.body.flightTimeRemaining = 200.0f;
  pkt.body.timeToDestination = 150.0f;
  updateAircraftRecord(&pkt);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL, "Record exists after update");
  TEST_ASSERT(rec->lastFuelLevel == 50.0f, "lastFuelLevel copied correctly");
  TEST_ASSERT(rec->nearestAirportID == 42, "nearestAirportID copied correctly");
  TEST_ASSERT(rec->destinationAirportID == 99,
              "destinationAirportID copied correctly");
  TEST_ASSERT(rec->flightTimeRemaining == 200.0f,
              "flightTimeRemaining copied correctly");
  TEST_ASSERT(rec->timeToDestination == 150.0f,
              "timeToDestination copied correctly");
  TEST_ASSERT(rec->isActive == true, "isActive set to true");
}

// Test 11: updateAircraftRecord returns -1 when table is full
static void test_update_returns_neg1_when_full(void) {
  // Fill all MAX_AIRCRAFT slots with unique IDs
  for (int i = 0; i < MAX_AIRCRAFT; i++) {
    FuelPacket pkt = makeTestPacket(1000 + i, 50.0f);
    int result = updateAircraftRecord(&pkt);
    TEST_ASSERT(result == 0, "Filling slot succeeds");
  }

  // One more should fail
  FuelPacket extra = makeTestPacket(9999, 50.0f);
  int result = updateAircraftRecord(&extra);
  TEST_ASSERT(result == -1,
              "updateAircraftRecord() returns -1 when table is full");
}

// ─── Group C: State transition detection ────────────────────────────

// Test 12: detect NORMAL_CRUISE -> LOW_FUEL transition
static void test_transition_normal_to_low(void) {
  FuelPacket pkt1 = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt1);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_NORMAL_CRUISE,
              "Initial state is NORMAL_CRUISE");

  FuelPacket pkt2 = makeTestPacket(101, 20.0f);
  updateAircraftRecord(&pkt2);

  rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_LOW_FUEL,
              "State transitioned to LOW_FUEL after fuel=20.0");
}

// Test 13: detect LOW_FUEL -> CRITICAL_FUEL transition
static void test_transition_low_to_critical(void) {
  FuelPacket pkt1 = makeTestPacket(101, 20.0f);
  updateAircraftRecord(&pkt1);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_LOW_FUEL,
              "Initial state is LOW_FUEL");

  FuelPacket pkt2 = makeTestPacket(101, 10.0f);
  updateAircraftRecord(&pkt2);

  rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_CRITICAL_FUEL,
              "State transitioned to CRITICAL_FUEL after fuel=10.0");
}

// Test 14: detect NORMAL_CRUISE -> CRITICAL_FUEL (skip LOW)
static void test_transition_normal_to_critical(void) {
  FuelPacket pkt1 = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt1);

  FuelPacket pkt2 = makeTestPacket(101, 10.0f);
  updateAircraftRecord(&pkt2);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_CRITICAL_FUEL,
              "State transitioned directly to CRITICAL_FUEL from NORMAL");
}

// Test 15: no transition when state unchanged
static void test_no_transition_same_state(void) {
  FuelPacket pkt1 = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt1);

  FuelPacket pkt2 = makeTestPacket(101, 45.0f);
  updateAircraftRecord(&pkt2);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_NORMAL_CRUISE,
              "State remains NORMAL_CRUISE when fuel stays above 25%");
}

// Test 16: detect CRITICAL_FUEL -> NORMAL_CRUISE (recovery)
static void test_transition_critical_to_normal(void) {
  FuelPacket pkt1 = makeTestPacket(101, 10.0f);
  updateAircraftRecord(&pkt1);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_CRITICAL_FUEL,
              "Initial state is CRITICAL_FUEL");

  FuelPacket pkt2 = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt2);

  rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL && rec->currentState == STATE_NORMAL_CRUISE,
              "State recovered to NORMAL_CRUISE after fuel=50.0");
}

// ─── Group D: Edge cases ────────────────────────────────────────────

// Test 17: resetAircraftRecords clears all records
static void test_reset_clears_records(void) {
  FuelPacket pkt = makeTestPacket(101, 50.0f);
  updateAircraftRecord(&pkt);

  AircraftRecord *rec = getAircraftRecord(101);
  TEST_ASSERT(rec != NULL, "Record exists before reset");

  resetAircraftRecords();

  rec = getAircraftRecord(101);
  TEST_ASSERT(rec == NULL, "Record is NULL after resetAircraftRecords()");
}

// Test 18: checkFuelThresholds handles NULL packet gracefully
static void test_check_threshold_null_packet(void) {
  FuelState state = checkFuelThresholds(NULL);
  TEST_ASSERT(state == STATE_NORMAL_CRUISE,
              "checkFuelThresholds(NULL) returns STATE_NORMAL_CRUISE");
}

// Test 19: updateAircraftRecord returns -1 for NULL packet
static void test_update_null_packet(void) {
  int result = updateAircraftRecord(NULL);
  TEST_ASSERT(result == -1, "updateAircraftRecord(NULL) returns -1");
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

  printf("=== Fuel Threshold Detection Unit Tests ===\n\n");

  // Group A: threshold state determination
  RUN_TEST(test_check_threshold_normal_cruise);
  RUN_TEST(test_check_threshold_at_low_boundary);
  RUN_TEST(test_check_threshold_low_fuel);
  RUN_TEST(test_check_threshold_at_critical_boundary);
  RUN_TEST(test_check_threshold_critical_fuel);
  RUN_TEST(test_check_threshold_zero_fuel);
  RUN_TEST(test_check_threshold_full_fuel);

  // Group B: record management
  RUN_TEST(test_update_creates_new_record);
  RUN_TEST(test_update_updates_existing_record);
  RUN_TEST(test_update_copies_packet_fields);
  RUN_TEST(test_update_returns_neg1_when_full);

  // Group C: state transitions
  RUN_TEST(test_transition_normal_to_low);
  RUN_TEST(test_transition_low_to_critical);
  RUN_TEST(test_transition_normal_to_critical);
  RUN_TEST(test_no_transition_same_state);
  RUN_TEST(test_transition_critical_to_normal);

  // Group D: edge cases
  RUN_TEST(test_reset_clears_records);
  RUN_TEST(test_check_threshold_null_packet);
  RUN_TEST(test_update_null_packet);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);

#ifdef _WIN32
  WSACleanup();
#endif

  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
