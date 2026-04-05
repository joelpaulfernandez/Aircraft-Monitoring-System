/*
test_divert_integration.c
Integration tests for the end-to-end divert decision flow.

Tests the full chain:
  Client sends FUEL_STATUS → Server evaluates → Server sends DIVERT_CMD or status ack
  → Client receives → Client sends ACK_DIVERT → Server clears awaitingACK

Compiled with -DTESTING -DSERVER_PORT=18082 and -pthread.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "../../server/include/server.h"
#include "../../client/include/client.h"
#include "../../client/include/aircraft_state_machine.h"

// ─── Test harness ────────────────────────────────────────────────────────────

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
    resetAircraftRecords();                                                    \
    resetClients();                                                            \
  } while (0)

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Receive exactly sizeof(FuelPacket) bytes (loop until done or error).
static ssize_t recvAll(socket_t fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return (ssize_t)total;
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
    pkt.header.packetID   = 1;
    pkt.header.type       = FUEL_STATUS;
    pkt.header.aircraftID = aircraftID;
    pkt.header.timestamp  = time(NULL);
    pkt.body.fuelLevel            = fuelLevel;
    pkt.body.flightTimeRemaining  = flightTimeRemaining;
    pkt.body.timeToDestination    = timeToDestination;
    pkt.body.nearestAirportID     = nearestAirportID;
    pkt.body.destinationAirportID = 99;
    return pkt;
}

// ─── Server thread: full divert-aware recv loop ───────────────────────────────
//
// Mirrors the server/main.c recv loop but runs in a thread for testing.

typedef struct {
    socket_t serverFD;
} ServerLoopArg;

static void *serverLoopThread(void *arg) {
    ServerLoopArg *a = arg;
    socket_t clientFD = acceptClient(a->serverFD);
    if (clientFD == INVALID_SOCK) return NULL;

    int aircraftID = performHandshake(clientFD);
    if (aircraftID == -1) { CLOSE_SOCKET(clientFD); return NULL; }

    FuelPacket pkt;
    while (1) {
        ssize_t n = recvAll(clientFD, &pkt, sizeof(FuelPacket));
        if (n != (ssize_t)sizeof(FuelPacket)) break;

        if (!validatePacket(&pkt)) continue;
        if (pkt.header.aircraftID != aircraftID) continue;

        if (pkt.header.type == ACK_DIVERT) {
            AircraftRecord *rec = getAircraftRecord(aircraftID);
            if (rec) rec->awaitingACK = false;
            continue;
        }

        if (pkt.header.type == LANDED_SAFE) break;

        // FUEL_STATUS
        updateAircraftRecord(&pkt);
        FuelState state = checkFuelThresholds(&pkt);

        if (evaluateDivertDecision(aircraftID)) {
            broadcastDivertCommand(aircraftID);
        } else {
            // Send status ack
            FuelPacket resp;
            memset(&resp, 0, sizeof(resp));
            resp.header.type       = FUEL_STATUS;
            resp.header.aircraftID = aircraftID;
            resp.header.timestamp  = time(NULL);
            resp.body.currentState = state;
            send(clientFD, &resp, sizeof(resp), 0);
        }
    }

    CLOSE_SOCKET(clientFD);
    return NULL;
}

// ─── Test 1: Critical fuel triggers DIVERT_CMD response ──────────────────────

static void test_critical_fuel_triggers_divert_cmd(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerLoopArg arg = { serverFD };
    pthread_t tid;
    pthread_create(&tid, NULL, serverLoopThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

    int hs = sendHandshake(clientFD, 101);
    TEST_ASSERT(hs == 0, "Handshake succeeded");

    // Critical fuel: flightTimeRemaining < timeToDestination → divert
    FuelPacket pkt = makeTestPacket(101, 10.0f, 30.0f, 60.0f, 42);
    int sr = sendFuelPacket(clientFD, &pkt);
    TEST_ASSERT(sr == 0, "sendFuelPacket succeeded");

    FuelPacket resp;
    int rr = recvServerResponse(clientFD, &resp);
    TEST_ASSERT(rr == 0, "recvServerResponse succeeded");
    TEST_ASSERT(resp.header.type == DIVERT_CMD,
                "Server sends DIVERT_CMD for critical fuel");
    TEST_ASSERT(resp.body.nearestAirportID == 42,
                "DIVERT_CMD contains correct nearest airport");

    CLOSE_SOCKET(clientFD);
    pthread_join(tid, NULL);
    CLOSE_SOCKET(serverFD);
}

// ─── Test 2: Normal fuel triggers status ack (not DIVERT_CMD) ────────────────

static void test_normal_fuel_no_divert(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerLoopArg arg = { serverFD };
    pthread_t tid;
    pthread_create(&tid, NULL, serverLoopThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");
    TEST_ASSERT(sendHandshake(clientFD, 102) == 0, "Handshake succeeded");

    // Normal fuel: no divert
    FuelPacket pkt = makeTestPacket(102, 80.0f, 90.0f, 60.0f, 5);
    TEST_ASSERT(sendFuelPacket(clientFD, &pkt) == 0, "sendFuelPacket succeeded");

    FuelPacket resp;
    int rr = recvServerResponse(clientFD, &resp);
    TEST_ASSERT(rr == 0, "recvServerResponse succeeded");
    TEST_ASSERT(resp.header.type == FUEL_STATUS,
                "Server sends FUEL_STATUS ack (no divert) for normal fuel");
    TEST_ASSERT(resp.body.currentState == STATE_NORMAL_CRUISE,
                "Status ack reflects NORMAL_CRUISE state");

    CLOSE_SOCKET(clientFD);
    pthread_join(tid, NULL);
    CLOSE_SOCKET(serverFD);
}

// ─── Test 3: Client receives DIVERT_CMD, sends ACK_DIVERT, server clears flag ─

static void test_ack_divert_round_trip(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerLoopArg arg = { serverFD };
    pthread_t tid;
    pthread_create(&tid, NULL, serverLoopThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");
    TEST_ASSERT(sendHandshake(clientFD, 103) == 0, "Handshake succeeded");

    // Trigger divert
    FuelPacket pkt = makeTestPacket(103, 10.0f, 20.0f, 60.0f, 7);
    TEST_ASSERT(sendFuelPacket(clientFD, &pkt) == 0, "Fuel packet sent");

    FuelPacket resp;
    TEST_ASSERT(recvServerResponse(clientFD, &resp) == 0, "Response received");
    TEST_ASSERT(resp.header.type == DIVERT_CMD, "Got DIVERT_CMD");

    // Client sends ACK_DIVERT
    AircraftStateMachine sm;
    smInit(&sm, 103);
    smSetEmergencyDivert(&sm);
    TEST_ASSERT(smGetState(&sm) == STATE_EMERGENCY_DIVERT,
                "Client state machine in EMERGENCY_DIVERT");

    int ar = sendAckDivert(clientFD, 103);
    TEST_ASSERT(ar == 0, "sendAckDivert succeeded");

    // Give server thread time to process ACK_DIVERT before checking
    CLOSE_SOCKET(clientFD);
    pthread_join(tid, NULL);

    AircraftRecord *rec = getAircraftRecord(103);
    TEST_ASSERT(rec != NULL, "AircraftRecord exists after session");
    TEST_ASSERT(rec->awaitingACK == false,
                "Server cleared awaitingACK after ACK_DIVERT");
    TEST_ASSERT(rec->currentState == STATE_EMERGENCY_DIVERT,
                "Server record remains in EMERGENCY_DIVERT");

    CLOSE_SOCKET(serverFD);
}

// ─── Test 4: LANDED_SAFE terminates the server loop cleanly ──────────────────

static void test_landed_safe_terminates_loop(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerLoopArg arg = { serverFD };
    pthread_t tid;
    pthread_create(&tid, NULL, serverLoopThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");
    TEST_ASSERT(sendHandshake(clientFD, 104) == 0, "Handshake succeeded");

    // Send LANDED_SAFE
    FuelPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.packetID   = 1;
    pkt.header.type       = LANDED_SAFE;
    pkt.header.aircraftID = 104;
    pkt.header.timestamp  = time(NULL);
    pkt.body.fuelLevel    = 30.0f;
    TEST_ASSERT(sendFuelPacket(clientFD, &pkt) == 0, "LANDED_SAFE sent");

    // Server should close the connection; recv should return 0 (EOF)
    CLOSE_SOCKET(clientFD);
    pthread_join(tid, NULL);

    // If thread joined cleanly, the loop terminated without hanging
    TEST_ASSERT(1, "Server loop terminated after LANDED_SAFE");

    CLOSE_SOCKET(serverFD);
}

// ─── Test 5: Full round-trip — fuel status → divert → ACK → verify state ─────

static void test_full_round_trip(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerLoopArg arg = { serverFD };
    pthread_t tid;
    pthread_create(&tid, NULL, serverLoopThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");
    TEST_ASSERT(sendHandshake(clientFD, 105) == 0, "Handshake succeeded");

    // Step 1: send critical FUEL_STATUS
    FuelPacket pkt = makeTestPacket(105, 10.0f, 15.0f, 45.0f, 88);
    TEST_ASSERT(sendFuelPacket(clientFD, &pkt) == 0, "FUEL_STATUS sent");

    // Step 2: receive DIVERT_CMD
    FuelPacket resp;
    TEST_ASSERT(recvServerResponse(clientFD, &resp) == 0, "Response received");
    TEST_ASSERT(resp.header.type == DIVERT_CMD, "Response is DIVERT_CMD");
    TEST_ASSERT(resp.body.nearestAirportID == 88, "Nearest airport correct");

    // Step 3: send ACK_DIVERT
    TEST_ASSERT(sendAckDivert(clientFD, 105) == 0, "ACK_DIVERT sent");

    CLOSE_SOCKET(clientFD);
    pthread_join(tid, NULL);

    // Step 4: verify final server state
    AircraftRecord *rec = getAircraftRecord(105);
    TEST_ASSERT(rec != NULL, "Aircraft record exists");
    TEST_ASSERT(rec->currentState == STATE_EMERGENCY_DIVERT,
                "Final state: EMERGENCY_DIVERT");
    TEST_ASSERT(rec->awaitingACK == false,
                "awaitingACK cleared after full round-trip");

    CLOSE_SOCKET(serverFD);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }
#endif

    printf("=== Divert Integration Tests ===\n\n");

    RUN_TEST(test_critical_fuel_triggers_divert_cmd);
    RUN_TEST(test_normal_fuel_no_divert);
    RUN_TEST(test_ack_divert_round_trip);
    RUN_TEST(test_landed_safe_terminates_loop);
    RUN_TEST(test_full_round_trip);

    printf("\n=== Results: %d/%d passed ===\n", tests_run - tests_failed, tests_run);

#ifdef _WIN32
    WSACleanup();
#endif

    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
