/*
test_usability.c
Usability tests — verifying that the system's user-facing outputs
(state strings, log format, error feedback) are clear, readable,
and adhere to the documented format (REQ-LOG-060).

These tests validate the "user experience" layer of the system:
  - Are state strings human-readable and distinct?
  - Does the log format match the specification?
  - Are error conditions communicated clearly?
  - Is critical information (airport IDs) always present?
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../client/include/aircraft_state_machine.h"
#include "../../common/logger.h"
#include "../../common/packet.h"

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
  } while (0)

#define TEST_LOG_FILE "test_usability_output.log"

// Helper: read the first line of the test log file.
static bool readFirstLogLine(char *buf, size_t bufLen) {
  FILE *f = fopen(TEST_LOG_FILE, "r");
  if (f == NULL)
    return false;
  bool got = (fgets(buf, (int)bufLen, f) != NULL);
  fclose(f);
  return got;
}

// Helper: count occurrences of character c in string s.
static int countChar(const char *s, char c) {
  int count = 0;
  while (*s) {
    if (*s == c)
      count++;
    s++;
  }
  return count;
}

// Group A: State string readability and uniqueness

// Test 1: All FuelState values produce non-empty, readable strings
static void test_state_strings_non_empty(void) {
  AircraftStateMachine sm;

  smInit(&sm, 1);
  smUpdateFuel(&sm, 80.0f);
  const char *normal = smGetStateString(&sm);
  TEST_ASSERT(normal != NULL && strlen(normal) > 0,
              "NORMAL_CRUISE string is non-empty and readable");

  smInit(&sm, 1);
  smUpdateFuel(&sm, 20.0f);
  const char *low = smGetStateString(&sm);
  TEST_ASSERT(low != NULL && strlen(low) > 0,
              "LOW_FUEL string is non-empty and readable");

  smInit(&sm, 1);
  smUpdateFuel(&sm, 10.0f);
  const char *critical = smGetStateString(&sm);
  TEST_ASSERT(critical != NULL && strlen(critical) > 0,
              "CRITICAL_FUEL string is non-empty and readable");

  smInit(&sm, 1);
  smSetEmergencyDivert(&sm);
  const char *divert = smGetStateString(&sm);
  TEST_ASSERT(divert != NULL && strlen(divert) > 0,
              "EMERGENCY_DIVERT string is non-empty and readable");

  smInit(&sm, 1);
  smSetLanded(&sm, true);
  const char *landed = smGetStateString(&sm);
  TEST_ASSERT(landed != NULL && strlen(landed) > 0,
              "LANDED_SAFE string is non-empty and readable");
}

// Test 2: All state strings are distinct from each other
static void test_state_strings_unique(void) {
  AircraftStateMachine sm;
  const char *strings[5];

  smInit(&sm, 1);
  smUpdateFuel(&sm, 80.0f);
  strings[0] = smGetStateString(&sm);

  // Re-init for each state to get independent string
  AircraftStateMachine sm2;
  smInit(&sm2, 2);
  smUpdateFuel(&sm2, 20.0f);
  strings[1] = smGetStateString(&sm2);

  AircraftStateMachine sm3;
  smInit(&sm3, 3);
  smUpdateFuel(&sm3, 10.0f);
  strings[2] = smGetStateString(&sm3);

  AircraftStateMachine sm4;
  smInit(&sm4, 4);
  smSetEmergencyDivert(&sm4);
  strings[3] = smGetStateString(&sm4);

  AircraftStateMachine sm5;
  smInit(&sm5, 5);
  smSetLanded(&sm5, true);
  strings[4] = smGetStateString(&sm5);

  // Check all pairs are different
  bool allUnique = true;
  for (int i = 0; i < 5 && allUnique; i++) {
    for (int j = i + 1; j < 5 && allUnique; j++) {
      if (strcmp(strings[i], strings[j]) == 0) {
        allUnique = false;
        printf("    Duplicate: \"%s\" used for states %d and %d\n", strings[i],
               i, j);
      }
    }
  }
  TEST_ASSERT(allUnique,
              "All 5 state strings are unique (user can distinguish states)");
}

// Group B: Log format compliance (REQ-LOG-060)

// Test 3: Log line contains exactly 3 pipe separators (4 fields)
// Expected format: DateTime | TYPE | AircraftID | Details
static void test_log_format_pipe_count(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_INFO(101, "Test message");
  logClose();

  char line[512] = {0};
  bool got = readFirstLogLine(line, sizeof(line));
  TEST_ASSERT(got, "Log file has content after LOG_INFO");

  int pipes = countChar(line, '|');
  TEST_ASSERT(pipes == 3,
              "Log line has exactly 3 pipe separators (REQ-LOG-060 format)");
  remove(TEST_LOG_FILE);
}

// Test 4: INFO log contains "INFO"
static void test_log_info_label(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_INFO(1, "Info test");
  logClose();

  char line[512] = {0};
  readFirstLogLine(line, sizeof(line));
  TEST_ASSERT(strstr(line, "INFO") != NULL,
              "INFO log line contains 'INFO' label");
  remove(TEST_LOG_FILE);
}

// Test 5: WARNING log contains "WARNING"
static void test_log_warning_label(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_WARNING(1, "Warning test");
  logClose();

  char line[512] = {0};
  readFirstLogLine(line, sizeof(line));
  TEST_ASSERT(strstr(line, "WARNING") != NULL,
              "WARNING log line contains 'WARNING' label");
  remove(TEST_LOG_FILE);
}

// Test 6: ERROR log contains "ERROR"
static void test_log_error_label(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_ERROR(1, "Error test");
  logClose();

  char line[512] = {0};
  readFirstLogLine(line, sizeof(line));
  TEST_ASSERT(strstr(line, "ERROR") != NULL,
              "ERROR log line contains 'ERROR' label");
  remove(TEST_LOG_FILE);
}

// Test 7: Log line contains the aircraft ID
static void test_log_contains_aircraft_id(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_INFO(42, "ID check");
  logClose();

  char line[512] = {0};
  readFirstLogLine(line, sizeof(line));
  TEST_ASSERT(strstr(line, "42") != NULL, "Log line contains aircraft ID '42'");
  remove(TEST_LOG_FILE);
}

// Test 8: Log line contains a timestamp with date format
static void test_log_contains_timestamp(void) {
  remove(TEST_LOG_FILE);
  logInit(TEST_LOG_FILE);
  LOG_INFO(1, "Timestamp check");
  logClose();

  char line[512] = {0};
  readFirstLogLine(line, sizeof(line));
  // Check for date format with '-' separator (e.g., 2026-04-13)
  TEST_ASSERT(strstr(line, "-") != NULL,
              "Log line contains timestamp with date separator");
  // Check for time format with ':' separator (e.g., 15:30:00)
  TEST_ASSERT(strstr(line, ":") != NULL,
              "Log line contains timestamp with time separator");
  remove(TEST_LOG_FILE);
}

// Group C: User feedback clarity

// Test 9: Invalid fuel input returns clear error code (-1)
static void test_invalid_fuel_returns_error(void) {
  AircraftStateMachine sm;
  smInit(&sm, 1);

  int r1 = smUpdateFuel(&sm, -1.0f);
  TEST_ASSERT(r1 == -1, "Negative fuel returns -1 (clear error feedback)");

  int r2 = smUpdateFuel(&sm, 101.0f);
  TEST_ASSERT(r2 == -1, "Fuel > 100 returns -1 (clear error feedback)");

  // State should remain unchanged
  TEST_ASSERT(smGetState(&sm) == STATE_NORMAL_CRUISE,
              "State unchanged after invalid input (no silent corruption)");
}

// Test 10: Alert messages can be set and read back
static void test_alert_message_readable(void) {
  FuelPacket *pkt = createPacket(1, FUEL_STATUS);
  TEST_ASSERT(pkt != NULL, "Packet created");

  int r = setAlertMessage(pkt, "LOW FUEL: Divert immediately");
  TEST_ASSERT(r == 0, "setAlertMessage succeeds");
  TEST_ASSERT(pkt->body.alertMessage != NULL, "Alert message is not NULL");
  TEST_ASSERT(strcmp(pkt->body.alertMessage, "LOW FUEL: Divert immediately") ==
                  0,
              "Alert message is readable and matches input");

  freePacket(pkt);
}

// Test 11: Each packet type is distinguishable
static void test_packet_types_distinguishable(void) {
  TEST_ASSERT(FUEL_STATUS != LANDED_SAFE, "FUEL_STATUS != LANDED_SAFE");
  TEST_ASSERT(FUEL_STATUS != ACK_DIVERT, "FUEL_STATUS != ACK_DIVERT");
  TEST_ASSERT(FUEL_STATUS != DIVERT_CMD, "FUEL_STATUS != DIVERT_CMD");
  TEST_ASSERT(LANDED_SAFE != ACK_DIVERT, "LANDED_SAFE != ACK_DIVERT");
  TEST_ASSERT(LANDED_SAFE != DIVERT_CMD, "LANDED_SAFE != DIVERT_CMD");
  TEST_ASSERT(ACK_DIVERT != DIVERT_CMD, "ACK_DIVERT != DIVERT_CMD");
}

// Main

int main(void) {
  printf("=== Usability Tests ===\n\n");

  // Group A: state string readability
  RUN_TEST(test_state_strings_non_empty);
  RUN_TEST(test_state_strings_unique);

  // Group B: log format compliance
  RUN_TEST(test_log_format_pipe_count);
  RUN_TEST(test_log_info_label);
  RUN_TEST(test_log_warning_label);
  RUN_TEST(test_log_error_label);
  RUN_TEST(test_log_contains_aircraft_id);
  RUN_TEST(test_log_contains_timestamp);

  // Group C: user feedback clarity
  RUN_TEST(test_invalid_fuel_returns_error);
  RUN_TEST(test_alert_message_readable);
  RUN_TEST(test_packet_types_distinguishable);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);
  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
