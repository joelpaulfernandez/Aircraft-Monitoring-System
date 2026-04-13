/*
test_system_multi_client.c
System-level tests for multi-client concurrent sessions.

Validates end-to-end system behaviour when multiple aircraft clients
connect simultaneously. Tests the full server threading model, protocol
lifecycle, and correct per-client state isolation.

Compiled with -DTESTING -DSERVER_PORT=18083 -Iclient/include and -pthread.
*/

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

// Receive exactly len bytes, looping until done or error.
static ssize_t recvAll(socket_t fd, void *buf, size_t len) {
  size_t total = 0;
  char *p = buf;
  while (total < len) {
    ssize_t n = recv(fd, p + total, len - total, 0);
    if (n <= 0)
      return (ssize_t)total;
    total += (size_t)n;
  }
  return (ssize_t)total;
}

static FuelPacket makeTestPacket(int aircraftID, float fuelLevel,
                                 float flightTimeRemaining,
                                 float timeToDestination,
                                 int nearestAirportID) {
  FuelPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header.packetID = 1;
  pkt.header.type = FUEL_STATUS;
  pkt.header.aircraftID = aircraftID;
  pkt.header.timestamp = time(NULL);
  pkt.body.fuelLevel = fuelLevel;
  pkt.body.flightTimeRemaining = flightTimeRemaining;
  pkt.body.timeToDestination = timeToDestination;
  pkt.body.nearestAirportID = nearestAirportID;
  pkt.body.destinationAirportID = 99;
  return pkt;
}

// Server loop thread — mirrors server/main.c recv loop for one client.
typedef struct {
  socket_t serverFD;
} ServerLoopArg;

static void *serverLoopThread(void *arg) {
  ServerLoopArg *a = arg;
  socket_t clientFD = acceptClient(a->serverFD);
  if (clientFD == INVALID_SOCK)
    return NULL;

  int aircraftID = performHandshake(clientFD);
  if (aircraftID == -1) {
    CLOSE_SOCKET(clientFD);
    return NULL;
  }

  FuelPacket pkt;
  while (1) {
    ssize_t n = recvAll(clientFD, &pkt, sizeof(FuelPacket));
    if (n != (ssize_t)sizeof(FuelPacket))
      break;

    if (!validatePacket(&pkt))
      continue;
    if (pkt.header.aircraftID != aircraftID)
      continue;

    if (pkt.header.type == ACK_DIVERT) {
      AircraftRecord *rec = getAircraftRecord(aircraftID);
      if (rec)
        rec->awaitingACK = false;
      continue;
    }

    if (pkt.header.type == LANDED_SAFE)
      break;

    // FUEL_STATUS
    updateAircraftRecord(&pkt);
    FuelState state = checkFuelThresholds(&pkt);

    if (evaluateDivertDecision(aircraftID)) {
      broadcastDivertCommand(aircraftID);
    } else {
      FuelPacket resp;
      memset(&resp, 0, sizeof(resp));
      resp.header.type = FUEL_STATUS;
      resp.header.aircraftID = aircraftID;
      resp.header.timestamp = time(NULL);
      resp.body.currentState = state;
      send(clientFD, &resp, sizeof(resp), 0);
    }
  }

  CLOSE_SOCKET(clientFD);
  return NULL;
}

// Test 1: Three clients connect, handshake, send fuel status

static void test_three_clients_concurrent(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  // Launch 3 server threads
  ServerLoopArg arg = {serverFD};
  pthread_t tid1, tid2, tid3;
  pthread_create(&tid1, NULL, serverLoopThread, &arg);
  pthread_create(&tid2, NULL, serverLoopThread, &arg);
  pthread_create(&tid3, NULL, serverLoopThread, &arg);

  // Client 1 — normal fuel
  socket_t c1 = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(c1 != INVALID_SOCK, "Client 1 connected");
  TEST_ASSERT(sendHandshake(c1, 201) == 0, "Client 1 handshake succeeded");

  // Client 2 — normal fuel
  socket_t c2 = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(c2 != INVALID_SOCK, "Client 2 connected");
  TEST_ASSERT(sendHandshake(c2, 202) == 0, "Client 2 handshake succeeded");

  // Client 3 — normal fuel
  socket_t c3 = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(c3 != INVALID_SOCK, "Client 3 connected");
  TEST_ASSERT(sendHandshake(c3, 203) == 0, "Client 3 handshake succeeded");

  // Each sends a FUEL_STATUS and gets an ack
  FuelPacket pkt1 = makeTestPacket(201, 80.0f, 120.0f, 60.0f, 10);
  TEST_ASSERT(sendFuelPacket(c1, &pkt1) == 0, "Client 1 sent fuel status");
  FuelPacket resp1;
  TEST_ASSERT(recvServerResponse(c1, &resp1) == 0,
              "Client 1 received response");
  TEST_ASSERT(resp1.header.type == FUEL_STATUS, "Client 1 got FUEL_STATUS ack");

  FuelPacket pkt2 = makeTestPacket(202, 60.0f, 100.0f, 50.0f, 20);
  TEST_ASSERT(sendFuelPacket(c2, &pkt2) == 0, "Client 2 sent fuel status");
  FuelPacket resp2;
  TEST_ASSERT(recvServerResponse(c2, &resp2) == 0,
              "Client 2 received response");
  TEST_ASSERT(resp2.header.type == FUEL_STATUS, "Client 2 got FUEL_STATUS ack");

  FuelPacket pkt3 = makeTestPacket(203, 40.0f, 90.0f, 45.0f, 30);
  TEST_ASSERT(sendFuelPacket(c3, &pkt3) == 0, "Client 3 sent fuel status");
  FuelPacket resp3;
  TEST_ASSERT(recvServerResponse(c3, &resp3) == 0,
              "Client 3 received response");
  TEST_ASSERT(resp3.header.type == FUEL_STATUS, "Client 3 got FUEL_STATUS ack");

  // Disconnect all
  CLOSE_SOCKET(c1);
  CLOSE_SOCKET(c2);
  CLOSE_SOCKET(c3);
  pthread_join(tid1, NULL);
  pthread_join(tid2, NULL);
  pthread_join(tid3, NULL);
  CLOSE_SOCKET(serverFD);
}

// Test 2: Mixed fuel states — one client gets DIVERT_CMD

static void test_mixed_states_selective_divert(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  // Launch 2 server threads
  ServerLoopArg arg = {serverFD};
  pthread_t tid1, tid2;
  pthread_create(&tid1, NULL, serverLoopThread, &arg);
  pthread_create(&tid2, NULL, serverLoopThread, &arg);

  // Client A — normal fuel, can reach destination
  socket_t cA = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(cA != INVALID_SOCK, "Client A connected");
  TEST_ASSERT(sendHandshake(cA, 301) == 0, "Client A handshake");

  // Client B — critical fuel, CANNOT reach destination → should get DIVERT
  socket_t cB = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(cB != INVALID_SOCK, "Client B connected");
  TEST_ASSERT(sendHandshake(cB, 302) == 0, "Client B handshake");

  // Client A: normal
  FuelPacket pktA = makeTestPacket(301, 80.0f, 120.0f, 60.0f, 10);
  sendFuelPacket(cA, &pktA);
  FuelPacket respA;
  recvServerResponse(cA, &respA);
  TEST_ASSERT(respA.header.type == FUEL_STATUS,
              "Client A receives FUEL_STATUS ack (no divert)");

  // Client B: critical, flightTime < destTime
  FuelPacket pktB = makeTestPacket(302, 8.0f, 20.0f, 60.0f, 55);
  sendFuelPacket(cB, &pktB);
  FuelPacket respB;
  recvServerResponse(cB, &respB);
  TEST_ASSERT(respB.header.type == DIVERT_CMD, "Client B receives DIVERT_CMD");
  TEST_ASSERT(respB.body.nearestAirportID == 55,
              "DIVERT_CMD has correct nearestAirportID for Client B");

  CLOSE_SOCKET(cA);
  CLOSE_SOCKET(cB);
  pthread_join(tid1, NULL);
  pthread_join(tid2, NULL);
  CLOSE_SOCKET(serverFD);
}

// ─── Test 3: Full lifecycle — handshake → telemetry → divert → ACK → LANDED ─

static void test_full_lifecycle(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  ServerLoopArg arg = {serverFD};
  pthread_t tid;
  pthread_create(&tid, NULL, serverLoopThread, &arg);

  socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");
  TEST_ASSERT(sendHandshake(clientFD, 401) == 0, "Handshake succeeded");

  // Step 1: normal fuel status
  FuelPacket pkt1 = makeTestPacket(401, 80.0f, 120.0f, 60.0f, 10);
  TEST_ASSERT(sendFuelPacket(clientFD, &pkt1) == 0, "Normal FUEL_STATUS sent");
  FuelPacket resp1;
  TEST_ASSERT(recvServerResponse(clientFD, &resp1) == 0,
              "Normal response received");
  TEST_ASSERT(resp1.header.type == FUEL_STATUS, "Response is FUEL_STATUS ack");

  // Step 2: fuel drops, cannot reach destination → DIVERT
  FuelPacket pkt2 = makeTestPacket(401, 10.0f, 15.0f, 50.0f, 77);
  TEST_ASSERT(sendFuelPacket(clientFD, &pkt2) == 0,
              "Critical FUEL_STATUS sent");
  FuelPacket resp2;
  TEST_ASSERT(recvServerResponse(clientFD, &resp2) == 0,
              "Divert response received");
  TEST_ASSERT(resp2.header.type == DIVERT_CMD, "Response is DIVERT_CMD");
  TEST_ASSERT(resp2.body.nearestAirportID == 77, "Nearest airport correct");

  // Step 3: client ACKs the divert
  TEST_ASSERT(sendAckDivert(clientFD, 401) == 0, "ACK_DIVERT sent");

  // Step 4: send LANDED_SAFE
  FuelPacket landPkt;
  memset(&landPkt, 0, sizeof(landPkt));
  landPkt.header.packetID = 3;
  landPkt.header.type = LANDED_SAFE;
  landPkt.header.aircraftID = 401;
  landPkt.header.timestamp = time(NULL);
  landPkt.body.fuelLevel = 5.0f;
  TEST_ASSERT(sendFuelPacket(clientFD, &landPkt) == 0, "LANDED_SAFE sent");

  CLOSE_SOCKET(clientFD);
  pthread_join(tid, NULL);

  // Step 5: verify final server state
  AircraftRecord *rec = getAircraftRecord(401);
  TEST_ASSERT(rec != NULL, "Aircraft record exists after lifecycle");
  TEST_ASSERT(rec->awaitingACK == false,
              "awaitingACK cleared after full lifecycle");

  CLOSE_SOCKET(serverFD);
}

// Test 4: Duplicate aircraft ID rejection across clients

static void test_duplicate_id_rejected(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  ServerLoopArg arg = {serverFD};
  pthread_t tid1, tid2;
  pthread_create(&tid1, NULL, serverLoopThread, &arg);
  pthread_create(&tid2, NULL, serverLoopThread, &arg);

  // Client 1: connects with ID 501
  socket_t c1 = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(c1 != INVALID_SOCK, "Client 1 connected");
  TEST_ASSERT(sendHandshake(c1, 501) == 0,
              "Client 1 handshake with ID 501 succeeds");

  // Client 2: tries same ID 501 — should be rejected
  socket_t c2 = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(c2 != INVALID_SOCK, "Client 2 connected");
  int hs2 = sendHandshake(c2, 501);
  TEST_ASSERT(hs2 == -1, "Client 2 handshake with duplicate ID 501 rejected");

  CLOSE_SOCKET(c1);
  CLOSE_SOCKET(c2);
  pthread_join(tid1, NULL);
  pthread_join(tid2, NULL);
  CLOSE_SOCKET(serverFD);
}

// Test 5: Server state isolation — independent per client

static void test_state_isolation(void) {
  socket_t serverFD = initServer();
  TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

  ServerLoopArg arg = {serverFD};
  pthread_t tid1, tid2;
  pthread_create(&tid1, NULL, serverLoopThread, &arg);
  pthread_create(&tid2, NULL, serverLoopThread, &arg);

  // Client A: normal fuel
  socket_t cA = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(sendHandshake(cA, 601) == 0, "Client A handshake");

  // Client B: critical fuel but CAN reach destination (no divert)
  socket_t cB = connectToServer("127.0.0.1", SERVER_PORT);
  TEST_ASSERT(sendHandshake(cB, 602) == 0, "Client B handshake");

  // Client A sends normal
  FuelPacket pktA = makeTestPacket(601, 80.0f, 120.0f, 60.0f, 10);
  sendFuelPacket(cA, &pktA);
  FuelPacket respA;
  recvServerResponse(cA, &respA);

  // Client B sends critical but sufficient time
  FuelPacket pktB = makeTestPacket(602, 10.0f, 90.0f, 60.0f, 20);
  sendFuelPacket(cB, &pktB);
  FuelPacket respB;
  recvServerResponse(cB, &respB);

  // Verify independent states
  AircraftRecord *recA = getAircraftRecord(601);
  AircraftRecord *recB = getAircraftRecord(602);

  TEST_ASSERT(recA != NULL && recA->currentState == STATE_NORMAL_CRUISE,
              "Client A record is NORMAL_CRUISE");
  TEST_ASSERT(recB != NULL && recB->currentState == STATE_CRITICAL_FUEL,
              "Client B record is CRITICAL_FUEL (independent of A)");
  TEST_ASSERT(recA != NULL && recB != NULL &&
                  recA->currentState != recB->currentState,
              "States are independent across clients");

  CLOSE_SOCKET(cA);
  CLOSE_SOCKET(cB);
  pthread_join(tid1, NULL);
  pthread_join(tid2, NULL);
  CLOSE_SOCKET(serverFD);
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

  printf("=== System Multi-Client Tests ===\n\n");

  RUN_TEST(test_three_clients_concurrent);
  RUN_TEST(test_mixed_states_selective_divert);
  RUN_TEST(test_full_lifecycle);
  RUN_TEST(test_duplicate_id_rejected);
  RUN_TEST(test_state_isolation);

  printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed,
         tests_run);

#ifdef _WIN32
  WSACleanup();
#endif

  return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
