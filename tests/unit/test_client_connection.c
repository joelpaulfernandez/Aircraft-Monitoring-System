/*
test_client_connection.c
Unit tests for client TCP connection (client/client.c).

Compiled with -DTESTING and -DSERVER_PORT=18081.
Uses pthreads for a server-side helper thread to avoid deadlock.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Include server.h first so SOCKET_PLATFORM_DEFINED is set before client.h.
#include "../../server/include/server.h"
#include "../../client/include/client.h"

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
    resetClients();                                                            \
  } while (0)

// Server-side helper thread
// Accepts numClients connections and runs performHandshake on each.
// If receivePacket is set, receives one FuelPacket from the last client.
typedef struct {
    socket_t   serverFD;
    int        numClients;
    int        receivePacket;
    FuelPacket receivedPacket;
    ssize_t    receivedBytes;
} ServerArg;

static void *serverThread(void *arg) {
    ServerArg *a = arg;
    for (int i = 0; i < a->numClients; i++) {
        socket_t fd = acceptClient(a->serverFD);
        if (fd == INVALID_SOCK) break;

        int id = performHandshake(fd);

        if (a->receivePacket && i == a->numClients - 1 && id > 0) {
            char   *buf   = (char *)&a->receivedPacket;
            size_t  total = 0;
            while (total < sizeof(FuelPacket)) {
                ssize_t n = recv(fd, buf + total, sizeof(FuelPacket) - total, 0);
                if (n <= 0) break;
                total += (size_t)n;
            }
            a->receivedBytes = (ssize_t)total;
        }

        CLOSE_SOCKET(fd);
    }
    return NULL;
}

// Test 1: connectToServer() succeeds when server is listening
static void test_connect_success(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK,
                "connectToServer() returns valid fd when server is listening");

    CLOSE_SOCKET(clientFD);
    CLOSE_SOCKET(serverFD);
}

// Test 2: connectToServer() returns INVALID_SOCK when no server is running
static void test_connect_no_server(void) {
    socket_t fd = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(fd == INVALID_SOCK,
                "connectToServer() returns INVALID_SOCK when no server");
}

// Test 3: sendHandshake() returns 0 on HANDSHAKE_ACK
static void test_handshake_success(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerArg arg = { serverFD, 1, 0, {0}, 0 };
    pthread_t tid;
    pthread_create(&tid, NULL, serverThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

    int result = sendHandshake(clientFD, 501);
    TEST_ASSERT(result == 0,
                "sendHandshake() returns 0 when server sends HANDSHAKE_ACK");

    pthread_join(tid, NULL);
    CLOSE_SOCKET(clientFD);
    CLOSE_SOCKET(serverFD);
}

// Test 4: sendHandshake() returns -1 on HANDSHAKE_REJ for duplicate ID
static void test_handshake_rejected_duplicate(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerArg arg = { serverFD, 2, 0, {0}, 0 };
    pthread_t tid;
    pthread_create(&tid, NULL, serverThread, &arg);

    socket_t client1 = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(client1 != INVALID_SOCK, "First client connected");
    int r1 = sendHandshake(client1, 601);
    TEST_ASSERT(r1 == 0, "First sendHandshake() with ID 601 succeeds");

    socket_t client2 = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(client2 != INVALID_SOCK, "Second client connected");
    int r2 = sendHandshake(client2, 601);
    TEST_ASSERT(r2 == -1,
                "sendHandshake() returns -1 when server sends HANDSHAKE_REJ");

    pthread_join(tid, NULL);
    CLOSE_SOCKET(client1);
    CLOSE_SOCKET(client2);
    CLOSE_SOCKET(serverFD);
}

// Test 5: sendFuelPacket() returns 0 and server receives the correct bytes
static void test_send_fuel_packet(void) {
    socket_t serverFD = initServer();
    TEST_ASSERT(serverFD != INVALID_SOCK, "Server started");

    ServerArg arg = { serverFD, 1, 1, {0}, 0 };
    pthread_t tid;
    pthread_create(&tid, NULL, serverThread, &arg);

    socket_t clientFD = connectToServer("127.0.0.1", SERVER_PORT);
    TEST_ASSERT(clientFD != INVALID_SOCK, "Client connected");

    int hs = sendHandshake(clientFD, 701);
    TEST_ASSERT(hs == 0, "Handshake succeeded before packet send");

    FuelPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.aircraftID = 701;
    pkt.header.type       = FUEL_STATUS;
    pkt.header.timestamp  = (time_t)2000;
    pkt.body.fuelLevel    = 60.0f;
    pkt.body.alertMessage = NULL;

    int result = sendFuelPacket(clientFD, &pkt);
    TEST_ASSERT(result == 0, "sendFuelPacket() returns 0 on success");

    pthread_join(tid, NULL);

    TEST_ASSERT(arg.receivedBytes == (ssize_t)sizeof(FuelPacket),
                "Server received sizeof(FuelPacket) bytes");
    TEST_ASSERT(arg.receivedPacket.header.aircraftID == 701,
                "Server received correct aircraftID in packet");
    TEST_ASSERT(arg.receivedPacket.body.fuelLevel == 60.0f,
                "Server received correct fuelLevel in packet");

    CLOSE_SOCKET(clientFD);
    CLOSE_SOCKET(serverFD);
}

int main(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }
#endif

    printf("=== Client Connection Unit Tests ===\n\n");

    RUN_TEST(test_connect_success);
    RUN_TEST(test_connect_no_server);
    RUN_TEST(test_handshake_success);
    RUN_TEST(test_handshake_rejected_duplicate);
    RUN_TEST(test_send_fuel_packet);

    printf("\n=== Results: %d/%d passed ===\n",
           tests_run - tests_failed, tests_run);

#ifdef _WIN32
    WSACleanup();
#endif

    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
