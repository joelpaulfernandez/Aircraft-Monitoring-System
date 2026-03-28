/*
 * tests/unit/test_logger.c
 * Unit tests for common/logger.c
 *
 * Tests: logInit, logWrite, logClose, LOG_INFO/WARNING/ERROR macros
 *
 * Strategy: write to a temp log file, read it back, check format and content.
 * REQ-LOG-060 format: DateTime | TYPE    | AircraftID | Details
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/logger.h"

#define TEST_LOG_FILE "test_logger_output.log"

// Test harness

static int tests_run    = 0;
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
    remove(TEST_LOG_FILE);                                                     \
    name();                                                                    \
    logClose();                                                                \
    remove(TEST_LOG_FILE);                                                     \
  } while (0)

// Helper: read the first line of the test log file into buf.
// Returns true if a line was read, false otherwise.
static bool readFirstLogLine(char *buf, size_t bufLen) {
    FILE *f = fopen(TEST_LOG_FILE, "r");
    if (f == NULL) return false;
    bool got = (fgets(buf, (int)bufLen, f) != NULL);
    fclose(f);
    return got;
}

// ── logInit ──────────────────────────────────────────────────────────────────

// Test 1: logInit returns 0 for a valid file path
static void test_logInit_success(void) {
    int result = logInit(TEST_LOG_FILE);
    TEST_ASSERT(result == 0, "logInit() returns 0 for a valid file path");
}

// Test 2: logInit returns -1 for an invalid/unwritable path
static void test_logInit_invalid_path(void) {
    int result = logInit("/this/path/does/not/exist/test.log");
    TEST_ASSERT(result == -1,
                "logInit() returns -1 for an invalid file path");
}

// ── logWrite / REQ-LOG-060 format ────────────────────────────────────────────

// Test 3: LOG_INFO writes a line containing "INFO" and the details (REQ-LOG-060)
static void test_logWrite_info_level(void) {
    logInit(TEST_LOG_FILE);
    LOG_INFO(1, "Test info message");
    logClose();

    char line[256] = {0};
    bool got = readFirstLogLine(line, sizeof(line));
    TEST_ASSERT(got, "Log file contains at least one line after LOG_INFO");
    TEST_ASSERT(strstr(line, "INFO") != NULL,
                "LOG_INFO writes a line containing \"INFO\" (REQ-LOG-060)");
    TEST_ASSERT(strstr(line, "Test info message") != NULL,
                "LOG_INFO line contains the details message");
}

// Test 4: LOG_WARNING writes a line containing "WARNING"
static void test_logWrite_warning_level(void) {
    logInit(TEST_LOG_FILE);
    LOG_WARNING(2, "Test warning message");
    logClose();

    char line[256] = {0};
    bool got = readFirstLogLine(line, sizeof(line));
    TEST_ASSERT(got, "Log file contains at least one line after LOG_WARNING");
    TEST_ASSERT(strstr(line, "WARNING") != NULL,
                "LOG_WARNING writes a line containing \"WARNING\" (REQ-LOG-060)");
    TEST_ASSERT(strstr(line, "Test warning message") != NULL,
                "LOG_WARNING line contains the details message");
}

// Test 5: LOG_ERROR writes a line containing "ERROR"
static void test_logWrite_error_level(void) {
    logInit(TEST_LOG_FILE);
    LOG_ERROR(3, "Test error message");
    logClose();

    char line[256] = {0};
    bool got = readFirstLogLine(line, sizeof(line));
    TEST_ASSERT(got, "Log file contains at least one line after LOG_ERROR");
    TEST_ASSERT(strstr(line, "ERROR") != NULL,
                "LOG_ERROR writes a line containing \"ERROR\" (REQ-LOG-060)");
    TEST_ASSERT(strstr(line, "Test error message") != NULL,
                "LOG_ERROR line contains the details message");
}

// Test 6: Log line contains the aircraft ID (REQ-LOG-060)
static void test_logWrite_contains_aircraft_id(void) {
    logInit(TEST_LOG_FILE);
    LOG_INFO(99, "Aircraft ID check");
    logClose();

    char line[256] = {0};
    readFirstLogLine(line, sizeof(line));
    TEST_ASSERT(strstr(line, "99") != NULL,
                "Log line contains the aircraft ID (REQ-LOG-060)");
}

// Test 7: Log line contains a date/time field (REQ-LOG-060)
static void test_logWrite_contains_datetime(void) {
    logInit(TEST_LOG_FILE);
    LOG_INFO(1, "Datetime check");
    logClose();

    char line[256] = {0};
    readFirstLogLine(line, sizeof(line));
    // The timestamp is formatted as YYYY-MM-DD — check for the dash separator
    TEST_ASSERT(strstr(line, "-") != NULL,
                "Log line contains a datetime field with '-' separator (REQ-LOG-060)");
}

// Test 8: Multiple log entries — each on its own line
static void test_logWrite_multiple_entries(void) {
    logInit(TEST_LOG_FILE);
    LOG_INFO(1, "First entry");
    LOG_INFO(1, "Second entry");
    LOG_INFO(1, "Third entry");
    logClose();

    FILE *f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT(f != NULL, "Log file exists after multiple writes");

    int lineCount = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f) != NULL) lineCount++;
    fclose(f);

    TEST_ASSERT(lineCount == 3,
                "Three LOG_INFO calls produce exactly three lines in the log file");
}

// ── logWrite before logInit ───────────────────────────────────────────────────

// Test 9: logWrite before logInit does not crash (no file open yet)
static void test_logWrite_before_init_no_crash(void) {
    // logClose was called at the end of the previous RUN_TEST, so logFile is NULL
    LOG_INFO(0, "Write before init");
    TEST_ASSERT(true, "logWrite() before logInit() does not crash");
}

// ── logClose ─────────────────────────────────────────────────────────────────

// Test 10: logClose can be called twice without crashing
static void test_logClose_idempotent(void) {
    logInit(TEST_LOG_FILE);
    logClose();
    logClose(); // second call — should be a no-op
    TEST_ASSERT(true, "logClose() called twice does not crash");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
    printf("=== Logger Unit Tests ===\n\n");

    RUN_TEST(test_logInit_success);
    RUN_TEST(test_logInit_invalid_path);
    RUN_TEST(test_logWrite_info_level);
    RUN_TEST(test_logWrite_warning_level);
    RUN_TEST(test_logWrite_error_level);
    RUN_TEST(test_logWrite_contains_aircraft_id);
    RUN_TEST(test_logWrite_contains_datetime);
    RUN_TEST(test_logWrite_multiple_entries);
    RUN_TEST(test_logWrite_before_init_no_crash);
    RUN_TEST(test_logClose_idempotent);

    printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed, tests_run);
    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
