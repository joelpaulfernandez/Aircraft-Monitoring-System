/*
test_robustness.c
Robustness and negative / boundary-value tests for server and client modules.

Validates that the system handles malformed input, edge cases, and error
conditions gracefully without crashing or producing undefined behaviour.

Compiled with -DTESTING -DSERVER_PORT=18080 and -pthread.
*/

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../client/include/client.h"
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

// Helpers

// Raw TCP connect to server; does NOT use the client API.
static socket_t rawConnect(void) {
  socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == INVALID_SOCK)
    return INVALID_SOCK;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SERVER_PORT);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    CLOSE_SOCKET(fd);
    return INVALID_SOCK;
  }
  return fd;
}

// Group A: Malformed handshake strings

// Test 1: Handshake with no aircraft ID — "HANDSHAKE_REQ:\n"
static void test_handshake_missing_id(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *msg = "HANDSHAKE_REQ:\n";
  send(clientFD, msg, (int)strlen(msg), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1,
              "performHandshake rejects HANDSHAKE_REQ with missing ID");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Test 2: Handshake with non-numeric ID — "HANDSHAKE_REQ:abc\n"
static void test_handshake_non_numeric_id(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *msg = "HANDSHAKE_REQ:abc\n";
  send(clientFD, msg, (int)strlen(msg), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1, "performHandshake rejects non-numeric aircraft ID");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Test 3: Handshake with negative ID — "HANDSHAKE_REQ:-5\n"
static void test_handshake_negative_id(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *msg = "HANDSHAKE_REQ:-5\n";
  send(clientFD, msg, (int)strlen(msg), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1, "performHandshake rejects negative aircraft ID");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Test 4: Handshake with zero ID — "HANDSHAKE_REQ:0\n"
static void test_handshake_zero_id(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *msg = "HANDSHAKE_REQ:0\n";
  send(clientFD, msg, (int)strlen(msg), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1, "performHandshake rejects zero aircraft ID");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Test 5: Completely garbage data instead of handshake
static void test_handshake_garbage_data(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *garbage = "THIS_IS_GARBAGE_DATA_12345\n";
  send(clientFD, garbage, (int)strlen(garbage), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1, "performHandshake rejects completely garbage data");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Test 6: Empty handshake message (send a single newline)
static void test_handshake_empty_message(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  socket_t clientFD = rawConnect();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted");

  const char *msg = "\n";
  send(clientFD, msg, (int)strlen(msg), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(result == -1, "performHandshake rejects empty message");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

// Group B: validatePacket boundary values

// Test 7: validatePacket with timestamp == 0
static void test_validate_packet_zero_timestamp(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 1;
  pkt.body.fuelLevel = 50.0f;
  pkt.header.timestamp = 0;

  TEST_ASSERT(validatePacket(&pkt) == false,
              "validatePacket rejects timestamp == 0");
}

// Test 8: validatePacket with fuelLevel exactly 0.0 (valid boundary)
static void test_validate_packet_fuel_zero(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 1;
  pkt.header.timestamp = (time_t)1000;
  pkt.body.fuelLevel = 0.0f;

  TEST_ASSERT(validatePacket(&pkt) == true,
              "validatePacket accepts fuelLevel == 0.0");
}

// Test 9: validatePacket with fuelLevel exactly 100.0 (valid boundary)
static void test_validate_packet_fuel_hundred(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 1;
  pkt.header.timestamp = (time_t)1000;
  pkt.body.fuelLevel = 100.0f;

  TEST_ASSERT(validatePacket(&pkt) == true,
              "validatePacket accepts fuelLevel == 100.0");
}

// Test 10: validatePacket with NULL pointer
static void test_validate_packet_null(void) {
  TEST_ASSERT(validatePacket(NULL) == false,
              "validatePacket rejects NULL pointer");
}

// Group C: Client API robustness

// Test 11: recvServerResponse with NULL buffer returns -1
static void test_recv_response_null_buffer(void) {
  int result = recvServerResponse(INVALID_SOCK, NULL);
  TEST_ASSERT(result == -1, "recvServerResponse returns -1 for NULL buffer");
}

// Test 12: connectToServer with invalid address
static void test_connect_invalid_address(void) {
  socket_t fd = connectToServer("999.999.999.999", SERVER_PORT);
  TEST_ASSERT(fd == INVALID_SOCK,
              "connectToServer returns INVALID_SOCK for invalid address");
}

// Test 13: connectToServer with empty string address
static void test_connect_empty_address(void) {
  socket_t fd = connectToServer("", SERVER_PORT);
  TEST_ASSERT(fd == INVALID_SOCK,
              "connectToServer returns INVALID_SOCK for empty address");
}

// Group D: State machine robustness

// Test 14: checkFuelThresholds with extreme fuel values
static void test_threshold_extreme_values(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.packetID = 1;
  pkt.header.type = FUEL_STATUS;
  pkt.header.aircraftID = 101;
  pkt.header.timestamp = time(NULL);

  // Exactly at 0.0 — should be CRITICAL
  pkt.body.fuelLevel = 0.0f;
  FuelState state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_CRITICAL_FUEL,
              "checkFuelThresholds(0.0) returns CRITICAL_FUEL");

  // Exactly at 15.0 — should be CRITICAL (boundary: <= 15)
  pkt.body.fuelLevel = 15.0f;
  state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_CRITICAL_FUEL,
              "checkFuelThresholds(15.0) returns CRITICAL_FUEL");

  // Just above 15.0 — should be LOW_FUEL
  pkt.body.fuelLevel = 15.01f;
  state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_LOW_FUEL,
              "checkFuelThresholds(15.01) returns LOW_FUEL");

  // Exactly at 25.0 — should be LOW_FUEL (boundary: <= 25)
  pkt.body.fuelLevel = 25.0f;
  state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_LOW_FUEL,
              "checkFuelThresholds(25.0) returns LOW_FUEL");

  // Just above 25.0 — should be NORMAL
  pkt.body.fuelLevel = 25.01f;
  state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_NORMAL_CRUISE,
              "checkFuelThresholds(25.01) returns NORMAL_CRUISE");

  // Exactly at 100.0 — should be NORMAL
  pkt.body.fuelLevel = 100.0f;
  state = checkFuelThresholds(&pkt);
  TEST_ASSERT(state == STATE_NORMAL_CRUISE,
              "checkFuelThresholds(100.0) returns NORMAL_CRUISE");
}

// Main

int main(void) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return EXIT_FAILURE;
  }
#endif

  printf("=== Robustness Tests ===\n\n");

  // Group A: malformed handshake
  RUN_TEST(test_handshake_missing_id);
  RUN_TEST(test_handshake_non_numeric_id);
  RUN_TEST(test_handshake_negative_id);
  RUN_TEST(test_handshake_zero_id);
  RUN_TEST(test_handshake_garbage_data);
  RUN_TEST(test_handshake_empty_message);

  // Group B: validatePacket boundaries
  RUN_TEST(test_validate_packet_zero_timestamp);
  RUN_TEST(test_validate_packet_fuel_zero);
  RUN_TEST(test_validate_packet_fuel_hundred);
  RUN_TEST(test_validate_packet_null);

  // Group C: client API robustness
  RUN_TEST(test_recv_response_null_buffer);
  RUN_TEST(test_connect_invalid_address);
  RUN_TEST(test_connect_empty_address);

  // Group D: state machine robustness
  RUN_TEST(test_threshold_extreme_values);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);

#ifdef _WIN32
  WSACleanup();
#endif

  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
