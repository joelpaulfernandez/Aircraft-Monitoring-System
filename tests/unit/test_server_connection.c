/*
 test_server_connection.c
 Unit tests for server TCP connection (server/main.c)

 Compiled with -DTESTING so that server/main.c excludes its own main().
 Uses TEST_PORT (18080) instead of SERVER_PORT (8080) to avoid conflicts
 with a running production server.

 Test harness: minimal assert-based runner — no external dependencies.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SERVER_PORT is overridden to 18080 via -DSERVER_PORT=18080 compiler flag
 * (set in the Makefile test target) so that all translation units agree on
 * the same test port without conflicting with a running production server. */
#include "../../common/packet.h"
#include "../../server/include/server.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* Test harness */

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
    resetClients();                                                            \
  } while (0)

/* Helper: open a client socket and connect to TEST_PORT on localhost */
static socket_t connectToServer(void) {
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

/* Test 1: Server socket initializes successfully on SERVER_PORT */
static void test_server_socket_init_success(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK,
              "initServer() returns a valid socket fd");
  CLOSE_SOCKET(serverFD);
}

/* Test 2: Server socket fails gracefully if port is already in use */
static void test_server_socket_port_in_use(void) {
  socket_t first = initServer();
  TEST_ASSERT(first != INVALID_SOCK, "First initServer() call succeeds");

  /* Second call must fail: the port is already bound. */
  socket_t second = initServer();
  TEST_ASSERT(second == INVALID_SOCK,
              "Second initServer() call returns INVALID_SOCK (port in use)");

  CLOSE_SOCKET(first);
  /* 'second' is INVALID_SOCK, nothing to close */
}

/* Test 3: Server rejects a data packet received before handshake */
/* REQ-COM-050: connection verification before any data packets. */
static void test_reject_data_before_handshake(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK,
              "Server started for pre-handshake test");

  socket_t clientFD = connectToServer();
  TEST_ASSERT(clientFD != INVALID_SOCK,
              "Client connected for pre-handshake test");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted the connection");

  /* Send raw FuelPacket bytes instead of the handshake string. */
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 42;
  pkt.header.type = FUEL_STATUS;
  pkt.body.fuelLevel = 80.0f;
  send(clientFD, (const char *)&pkt, sizeof(pkt), 0);

  /* Server must reject: performHandshake() should return -1. */
  int result = performHandshake(acceptedFD);
  TEST_ASSERT(
      result == -1,
      "performHandshake() returns -1 when raw FuelPacket is sent first");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

/* Test 4: Server accepts a client connection after successful handshake */
static void test_accept_client_after_handshake(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started for handshake test");

  socket_t clientFD = connectToServer();
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected for handshake test");

  socket_t acceptedFD = acceptClient(serverFD);
  TEST_ASSERT(acceptedFD != INVALID_SOCK, "Server accepted the connection");

  /* Client sends the correct handshake request. */
  const char *req = "HANDSHAKE_REQ:101\n";
  send(clientFD, req, (int)strlen(req), 0);

  int result = performHandshake(acceptedFD);
  TEST_ASSERT(
      result == 101,
      "performHandshake() returns aircraftID 101 after valid handshake");

  /* Client should receive HANDSHAKE_ACK. */
  char buf[HANDSHAKE_MSG_MAX_LEN];
  memset(buf, 0, sizeof(buf));
  recv(clientFD, buf, sizeof(buf) - 1, 0);
  TEST_ASSERT(strncmp(buf, "HANDSHAKE_ACK", strlen("HANDSHAKE_ACK")) == 0,
              "Client receives HANDSHAKE_ACK after successful handshake");

  CLOSE_SOCKET(acceptedFD);
  CLOSE_SOCKET(clientFD);
  CLOSE_SOCKET(serverFD);
}

/* Test 5: Server rejects a duplicate connection from the same aircraftID */
static void test_reject_duplicate_aircraft_id(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started for duplicate ID test");

  /* First client: aircraftID 202 */
  socket_t client1 = connectToServer();
  socket_t accepted1 = acceptClient(serverFD);
  const char *req1 = "HANDSHAKE_REQ:202\n";
  send(client1, req1, (int)strlen(req1), 0);
  int result1 = performHandshake(accepted1);
  TEST_ASSERT(
      result1 == 202,
      "First client with aircraftID 202 completes handshake successfully");

  /* Drain ACK from client1's socket so it doesn't block. */
  char ack_buf[HANDSHAKE_MSG_MAX_LEN];
  recv(client1, ack_buf, sizeof(ack_buf) - 1, 0);

  /* Second client: same aircraftID 202 */
  socket_t client2 = connectToServer();
  socket_t accepted2 = acceptClient(serverFD);
  const char *req2 = "HANDSHAKE_REQ:202\n";
  send(client2, req2, (int)strlen(req2), 0);
  int result2 = performHandshake(accepted2);
  TEST_ASSERT(result2 == -1,
              "Second client with duplicate aircraftID 202 is rejected");

  /* Second client should receive HANDSHAKE_REJ:DUPLICATE. */
  char rej_buf[HANDSHAKE_MSG_MAX_LEN];
  memset(rej_buf, 0, sizeof(rej_buf));
  recv(client2, rej_buf, sizeof(rej_buf) - 1, 0);
  TEST_ASSERT(strncmp(rej_buf, "HANDSHAKE_REJ", strlen("HANDSHAKE_REJ")) == 0,
              "Duplicate client receives HANDSHAKE_REJ response");

  CLOSE_SOCKET(accepted2);
  CLOSE_SOCKET(client2);
  CLOSE_SOCKET(accepted1);
  CLOSE_SOCKET(client1);
  CLOSE_SOCKET(serverFD);
}

/* Test 6: validatePacket() returns false for aircraftID <= 0 */
static void test_validate_packet_invalid_aircraft_id(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.body.fuelLevel = 50.0f;
  pkt.header.timestamp = (time_t)1000;
  pkt.body.alertMessage = NULL;

  pkt.header.aircraftID = 0;
  TEST_ASSERT(validatePacket(&pkt) == false,
              "validatePacket() returns false for aircraftID == 0");

  pkt.header.aircraftID = -1;
  TEST_ASSERT(validatePacket(&pkt) == false,
              "validatePacket() returns false for aircraftID == -1");
}

/* Test 7: validatePacket() returns false for fuelLevel outside 0–100% */
static void test_validate_packet_invalid_fuel_level(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 1;
  pkt.header.timestamp = (time_t)1000;
  pkt.body.alertMessage = NULL;

  pkt.body.fuelLevel = -0.1f;
  TEST_ASSERT(validatePacket(&pkt) == false,
              "validatePacket() returns false for fuelLevel == -0.1");

  pkt.body.fuelLevel = 100.1f;
  TEST_ASSERT(validatePacket(&pkt) == false,
              "validatePacket() returns false for fuelLevel == 100.1");
}

/* Test 8: validatePacket() returns true for a valid FuelPacket */
static void test_validate_packet_valid(void) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.aircraftID = 101;
  pkt.header.timestamp = (time_t)1000;
  pkt.body.fuelLevel = 75.5f;
  pkt.body.alertMessage = NULL;

  TEST_ASSERT(validatePacket(&pkt) == true,
              "validatePacket() returns true for a valid FuelPacket");
}

/* Main */
int main(void) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return EXIT_FAILURE;
  }
#endif

  printf("=== Server Connection Unit Tests ===\n\n");

  /* validatePacket tests — no networking needed, run first */
  RUN_TEST(test_validate_packet_invalid_aircraft_id);
  RUN_TEST(test_validate_packet_invalid_fuel_level);
  RUN_TEST(test_validate_packet_valid);

  /* Network tests */
  RUN_TEST(test_server_socket_init_success);
  RUN_TEST(test_server_socket_port_in_use);
  RUN_TEST(test_reject_data_before_handshake);
  RUN_TEST(test_accept_client_after_handshake);
  RUN_TEST(test_reject_duplicate_aircraft_id);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);

#ifdef _WIN32
  WSACleanup();
#endif

  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
