/*
 * tests/unit/test_packet.c
 * Unit tests for common/packet.c
 *
 * Tests: createPacket, freePacket, setAlertMessage
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/packet.h"

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
    name();                                                                    \
  } while (0)

// ── createPacket ─────────────────────────────────────────────────────────────

// Test 1: createPacket returns a non-NULL pointer
static void test_createPacket_returns_non_null(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() returns non-NULL");
    freePacket(pkt);
}

// Test 2: header.aircraftID is set correctly (REQ-PKT-010)
static void test_createPacket_sets_aircraftID(void) {
    FuelPacket *pkt = createPacket(42, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    TEST_ASSERT(pkt->header.aircraftID == 42,
                "createPacket() sets aircraftID correctly (REQ-PKT-010)");
    freePacket(pkt);
}

// Test 3: header.type is set correctly
static void test_createPacket_sets_type(void) {
    FuelPacket *pkt1 = createPacket(1, FUEL_STATUS);
    FuelPacket *pkt2 = createPacket(1, LANDED_SAFE);
    FuelPacket *pkt3 = createPacket(1, ACK_DIVERT);

    TEST_ASSERT(pkt1 != NULL && pkt1->header.type == FUEL_STATUS,
                "createPacket() sets type FUEL_STATUS");
    TEST_ASSERT(pkt2 != NULL && pkt2->header.type == LANDED_SAFE,
                "createPacket() sets type LANDED_SAFE");
    TEST_ASSERT(pkt3 != NULL && pkt3->header.type == ACK_DIVERT,
                "createPacket() sets type ACK_DIVERT");

    freePacket(pkt1);
    freePacket(pkt2);
    freePacket(pkt3);
}

// Test 4: header.timestamp is non-zero
static void test_createPacket_sets_timestamp(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    TEST_ASSERT(pkt->header.timestamp != 0,
                "createPacket() sets a non-zero timestamp");
    freePacket(pkt);
}

// Test 5: header.packetID increments across successive calls
static void test_createPacket_increments_packetID(void) {
    FuelPacket *pkt1 = createPacket(1, FUEL_STATUS);
    FuelPacket *pkt2 = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt1 != NULL && pkt2 != NULL, "Both createPacket() calls succeed");
    TEST_ASSERT(pkt2->header.packetID == pkt1->header.packetID + 1,
                "createPacket() increments packetID on each call");
    freePacket(pkt1);
    freePacket(pkt2);
}

// Test 6: body fields default to zero / NULL (consumptionRate, fuelLevel, alertMessage)
static void test_createPacket_body_defaults_to_zero(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    TEST_ASSERT(pkt->body.fuelLevel == 0.0f,
                "createPacket() initialises fuelLevel to 0 (REQ-PKT-020)");
    TEST_ASSERT(pkt->body.consumptionRate == 0.0f,
                "createPacket() initialises consumptionRate to 0 (REQ-PKT-030)");
    TEST_ASSERT(pkt->body.emergencyFlag == false,
                "createPacket() initialises emergencyFlag to false (REQ-PKT-060)");
    TEST_ASSERT(pkt->body.alertMessage == NULL,
                "createPacket() initialises alertMessage to NULL (REQ-PKT-070)");
    freePacket(pkt);
}

// ── freePacket ───────────────────────────────────────────────────────────────

// Test 7: freePacket with NULL does not crash
static void test_freePacket_null_safe(void) {
    freePacket(NULL);
    TEST_ASSERT(true, "freePacket(NULL) does not crash");
}

// Test 8: freePacket frees alertMessage without crashing
static void test_freePacket_frees_alert_message(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    setAlertMessage(pkt, "Low fuel warning");
    TEST_ASSERT(pkt->body.alertMessage != NULL,
                "alertMessage is set before free");
    freePacket(pkt); // Should free alertMessage without crashing
    TEST_ASSERT(true, "freePacket() with alertMessage does not crash");
}

// ── setAlertMessage ──────────────────────────────────────────────────────────

// Test 9: setAlertMessage returns 0 and stores the correct string (REQ-PKT-070)
static void test_setAlertMessage_stores_message(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    int result = setAlertMessage(pkt, "Critical fuel level");
    TEST_ASSERT(result == 0, "setAlertMessage() returns 0 on success");
    TEST_ASSERT(pkt->body.alertMessage != NULL,
                "setAlertMessage() sets alertMessage to non-NULL (REQ-PKT-070)");
    TEST_ASSERT(strcmp(pkt->body.alertMessage, "Critical fuel level") == 0,
                "setAlertMessage() stores the correct message string");
    freePacket(pkt);
}

// Test 10: setAlertMessage replaces a previously set message
static void test_setAlertMessage_replaces_message(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    setAlertMessage(pkt, "First message");
    setAlertMessage(pkt, "Second message");
    TEST_ASSERT(strcmp(pkt->body.alertMessage, "Second message") == 0,
                "setAlertMessage() replaces the previous message correctly");
    freePacket(pkt);
}

// Test 11: setAlertMessage with NULL packet returns -1
static void test_setAlertMessage_null_packet_safe(void) {
    int result = setAlertMessage(NULL, "test");
    TEST_ASSERT(result == -1, "setAlertMessage(NULL, msg) returns -1");
}

// Test 12: setAlertMessage with NULL message returns -1 and does not crash
static void test_setAlertMessage_null_message_safe(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    int result = setAlertMessage(pkt, NULL);
    TEST_ASSERT(result == -1, "setAlertMessage(pkt, NULL) returns -1");
    freePacket(pkt);
}

// Test 13: setAlertMessage with empty string stores an empty string
static void test_setAlertMessage_empty_string(void) {
    FuelPacket *pkt = createPacket(1, FUEL_STATUS);
    TEST_ASSERT(pkt != NULL, "createPacket() succeeds");
    int result = setAlertMessage(pkt, "");
    TEST_ASSERT(result == 0, "setAlertMessage(pkt, \"\") returns 0");
    TEST_ASSERT(pkt->body.alertMessage != NULL,
                "setAlertMessage() sets alertMessage to non-NULL for empty string");
    TEST_ASSERT(strcmp(pkt->body.alertMessage, "") == 0,
                "setAlertMessage() stores an empty string correctly");
    freePacket(pkt);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
    printf("=== Packet Unit Tests ===\n\n");

    RUN_TEST(test_createPacket_returns_non_null);
    RUN_TEST(test_createPacket_sets_aircraftID);
    RUN_TEST(test_createPacket_sets_type);
    RUN_TEST(test_createPacket_sets_timestamp);
    RUN_TEST(test_createPacket_increments_packetID);
    RUN_TEST(test_createPacket_body_defaults_to_zero);

    RUN_TEST(test_freePacket_null_safe);
    RUN_TEST(test_freePacket_frees_alert_message);

    RUN_TEST(test_setAlertMessage_stores_message);
    RUN_TEST(test_setAlertMessage_replaces_message);
    RUN_TEST(test_setAlertMessage_null_packet_safe);
    RUN_TEST(test_setAlertMessage_null_message_safe);
    RUN_TEST(test_setAlertMessage_empty_string);

    printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed, tests_run);
    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
