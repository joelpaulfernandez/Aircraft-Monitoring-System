/*
test_fuel_system.c
Unit tests for client/aircraft_state_machine.c

Tests the AircraftStateMachine struct and its public functions:
  smInit, smUpdateFuel, smSetEmergencyDivert, smSetLanded,
  smGetState, smGetStateString
*/

#include <stdio.h>
#include <stdbool.h>
#include "../../client/include/aircraft_state_machine.h"

// Test harness

static int tests_run    = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg)                   \
  do {                                                \
    tests_run++;                                      \
    if (!(condition)) {                               \
      tests_failed++;                                 \
      printf("  FAIL: %s\n", (msg));                  \
    } else {                                          \
      printf("  PASS: %s\n", (msg));                  \
    }                                                 \
  } while (0)

#define RUN_TEST(name)                                \
  do {                                               \
    printf("[TEST] %s\n", #name);                     \
    name();                                           \
  } while (0)

// ---------- smInit ----------

static void test_init_sets_normal_cruise(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  TEST_ASSERT(smGetState(&sm) == STATE_NORMAL_CRUISE,
              "Initial state should be STATE_NORMAL_CRUISE");
  TEST_ASSERT(sm.fuelLevel == 100.0f, "Initial fuel should be 100%");
  TEST_ASSERT(sm.landed == false,     "Initial landed should be false");
  TEST_ASSERT(sm.aircraftID == 1,     "aircraftID should be set");
}

// ---------- smUpdateFuel — state transitions ----------

static void test_fuel_above_low_threshold_is_normal(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, FUEL_THRESHOLD_LOW + 1.0f);
  TEST_ASSERT(smGetState(&sm) == STATE_NORMAL_CRUISE,
              "Fuel above LOW threshold => STATE_NORMAL_CRUISE");
}

static void test_fuel_at_low_threshold_is_low_fuel(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, FUEL_THRESHOLD_LOW);  // exactly at boundary — not above
  TEST_ASSERT(smGetState(&sm) == STATE_LOW_FUEL,
              "Fuel == LOW threshold => STATE_LOW_FUEL");
}

static void test_fuel_between_low_and_critical_is_low_fuel(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, 20.0f);
  TEST_ASSERT(smGetState(&sm) == STATE_LOW_FUEL,
              "Fuel between LOW and CRITICAL => STATE_LOW_FUEL");
}

static void test_fuel_at_critical_threshold_is_critical(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, FUEL_THRESHOLD_CRITICAL);  // exactly at boundary
  TEST_ASSERT(smGetState(&sm) == STATE_CRITICAL_FUEL,
              "Fuel == CRITICAL threshold => STATE_CRITICAL_FUEL");
}

static void test_fuel_below_critical_stays_critical(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, 5.0f);
  TEST_ASSERT(smGetState(&sm) == STATE_CRITICAL_FUEL,
              "Fuel below CRITICAL threshold => STATE_CRITICAL_FUEL (server decides emergency)");
}

static void test_fuel_zero_stays_critical(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, 0.0f);
  TEST_ASSERT(smGetState(&sm) == STATE_CRITICAL_FUEL,
              "Fuel 0% without server divert command => STATE_CRITICAL_FUEL");
}

// ---------- smUpdateFuel — input validation ----------

static void test_negative_fuel_rejected(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  int result = smUpdateFuel(&sm, -1.0f);
  TEST_ASSERT(result == -1,
              "Negative fuel => returns -1");
  TEST_ASSERT(smGetState(&sm) == STATE_NORMAL_CRUISE,
              "State unchanged after invalid input");
}

static void test_fuel_over_100_rejected(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  int result = smUpdateFuel(&sm, 101.0f);
  TEST_ASSERT(result == -1,
              "Fuel > 100 => returns -1");
  TEST_ASSERT(smGetState(&sm) == STATE_NORMAL_CRUISE,
              "State unchanged after invalid input");
}

static void test_valid_fuel_returns_zero(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  int result = smUpdateFuel(&sm, 50.0f);
  TEST_ASSERT(result == 0, "Valid fuel => returns 0");
}

// ---------- smSetEmergencyDivert ----------

static void test_emergency_divert_sets_state(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, 10.0f);  // STATE_CRITICAL_FUEL
  smSetEmergencyDivert(&sm);
  TEST_ASSERT(smGetState(&sm) == STATE_EMERGENCY_DIVERT,
              "smSetEmergencyDivert => STATE_EMERGENCY_DIVERT");
}

static void test_fuel_update_does_not_leave_emergency_divert(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smSetEmergencyDivert(&sm);
  smUpdateFuel(&sm, 80.0f);  // even with high fuel, state should stay
  TEST_ASSERT(smGetState(&sm) == STATE_EMERGENCY_DIVERT,
              "Fuel update does not clear STATE_EMERGENCY_DIVERT");
}

// ---------- smSetLanded ----------

static void test_landed_sets_state(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smUpdateFuel(&sm, 80.0f);
  smSetLanded(&sm, true);
  TEST_ASSERT(smGetState(&sm) == STATE_LANDED_SAFE,
              "setLanded(true) => STATE_LANDED_SAFE");
}

static void test_landed_overrides_emergency_divert(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smSetEmergencyDivert(&sm);
  smSetLanded(&sm, true);
  TEST_ASSERT(smGetState(&sm) == STATE_LANDED_SAFE,
              "setLanded(true) overrides EMERGENCY_DIVERT => STATE_LANDED_SAFE");
}

static void test_fuel_update_while_landed_stays_landed(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);
  smSetLanded(&sm, true);
  smUpdateFuel(&sm, 80.0f);
  TEST_ASSERT(smGetState(&sm) == STATE_LANDED_SAFE,
              "Fuel update while landed => stays STATE_LANDED_SAFE");
}

// ---------- smGetStateString ----------

static void test_state_strings(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);

  smUpdateFuel(&sm, 80.0f);
  TEST_ASSERT(smGetStateString(&sm)[0] != '\0', "NORMAL_CRUISE string non-empty");

  smUpdateFuel(&sm, 20.0f);
  TEST_ASSERT(smGetStateString(&sm)[0] != '\0', "LOW_FUEL string non-empty");

  smUpdateFuel(&sm, 10.0f);
  TEST_ASSERT(smGetStateString(&sm)[0] != '\0', "CRITICAL_FUEL string non-empty");

  smSetEmergencyDivert(&sm);
  TEST_ASSERT(smGetStateString(&sm)[0] != '\0', "EMERGENCY_DIVERT string non-empty");

  smSetLanded(&sm, true);
  TEST_ASSERT(smGetStateString(&sm)[0] != '\0', "LANDED_SAFE string non-empty");
}

// ---------- main ----------

int main(void) {
  RUN_TEST(test_init_sets_normal_cruise);

  RUN_TEST(test_fuel_above_low_threshold_is_normal);
  RUN_TEST(test_fuel_at_low_threshold_is_low_fuel);
  RUN_TEST(test_fuel_between_low_and_critical_is_low_fuel);
  RUN_TEST(test_fuel_at_critical_threshold_is_critical);
  RUN_TEST(test_fuel_below_critical_stays_critical);
  RUN_TEST(test_fuel_zero_stays_critical);

  RUN_TEST(test_negative_fuel_rejected);
  RUN_TEST(test_fuel_over_100_rejected);
  RUN_TEST(test_valid_fuel_returns_zero);

  RUN_TEST(test_emergency_divert_sets_state);
  RUN_TEST(test_fuel_update_does_not_leave_emergency_divert);

  RUN_TEST(test_landed_sets_state);
  RUN_TEST(test_landed_overrides_emergency_divert);
  RUN_TEST(test_fuel_update_while_landed_stays_landed);

  RUN_TEST(test_state_strings);

  printf("\n%d tests run, %d failed.\n", tests_run, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
