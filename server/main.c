/*
server/main.c
Aircraft Fuel Monitoring System — Server TCP Connection

Implements: initServer, acceptClient, performHandshake, validatePacket
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/server.h"
#include "../common/packet.h"

#ifdef _WIN32
    #define SOCK_TIMEOUT_ERR WSAETIMEDOUT
#else
    #include <errno.h>
    #define SOCK_TIMEOUT_ERR EAGAIN
#endif

// Connected client table (module-private)
static ConnectedClient clients[MAX_CLIENTS];

// Returns the index of the slot with aircraftID, or -1 if not found.
static int findClientByAircraftID(int aircraftID) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].aircraftID == aircraftID) return i;
    }
    return -1;
}

// Returns the index of the first empty slot, or -1 if full.
static int findEmptySlot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].aircraftID == 0) return i;
    }
    return -1;
}

// Zero the client table. Called between unit tests.
void resetClients(void) {
    memset(clients, 0, sizeof(clients));
}

// initServer — REQ-COM-040
// Create TCP socket, bind to SERVER_PORT, start listening.
// Returns listening fd, or INVALID_SOCK on failure.
socket_t initServer(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR(0, "WSAStartup failed");
        return INVALID_SOCK;
    }
#endif

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) {
        LOG_ERROR(0, "socket() failed");
        return INVALID_SOCK;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERROR(0, "bind() failed — port may already be in use");
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

    if (listen(fd, MAX_CLIENTS) != 0) {
        LOG_ERROR(0, "listen() failed");
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

    LOG_INFO(0, "Server listening on port 8080");
    return fd;
}

// acceptClient — REQ-SVR-010
// Block until a client connects. Sets a recv timeout for the handshake.
// Returns the accepted fd, or INVALID_SOCK on error.
socket_t acceptClient(socket_t serverSocket) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    socket_t clientFD = accept(serverSocket, (struct sockaddr *)&clientAddr, &addrLen);
    if (clientFD == INVALID_SOCK) {
        LOG_ERROR(0, "accept() failed");
        return INVALID_SOCK;
    }

#ifdef _WIN32
    DWORD timeout = HANDSHAKE_TIMEOUT * 1000;
    setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv = { HANDSHAKE_TIMEOUT, 0 };
    setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "Connection attempt from %s", clientIP);
    LOG_INFO(0, logMsg);

    return clientFD;
}

// performHandshake — REQ-COM-050
// Reads the first message. Rejects anything that isn't HANDSHAKE_REQ.
// On success: registers client, sends HANDSHAKE_ACK, returns aircraftID.
// On failure: sends HANDSHAKE_REJ_*, returns -1.
int performHandshake(socket_t clientSocket) {
    char buf[HANDSHAKE_MSG_MAX_LEN];
    memset(buf, 0, sizeof(buf));

    int received = (int)recv(clientSocket, buf, sizeof(buf) - 1, 0);
    if (received <= 0) {
        LOG_ERROR(0, "Handshake recv() failed — client disconnected or timed out");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    if (strncmp(buf, HANDSHAKE_REQ_PREFIX, strlen(HANDSHAKE_REQ_PREFIX)) != 0) {
        LOG_ERROR(0, "Handshake failed — data received before handshake (REQ-COM-050)");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    const char *idStr    = buf + strlen(HANDSHAKE_REQ_PREFIX);
    int         aircraftID = (int)strtol(idStr, NULL, 10);
    if (aircraftID <= 0) {
        LOG_ERROR(0, "Handshake failed — invalid aircraftID");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    if (findClientByAircraftID(aircraftID) != -1) {
        LOG_ERROR(aircraftID, "Handshake failed — duplicate aircraftID");
        send(clientSocket, HANDSHAKE_REJ_DUP, strlen(HANDSHAKE_REJ_DUP), 0);
        return -1;
    }

    int slot = findEmptySlot();
    if (slot == -1) {
        LOG_ERROR(aircraftID, "Handshake failed — server is full");
        send(clientSocket, HANDSHAKE_REJ_FULL, strlen(HANDSHAKE_REJ_FULL), 0);
        return -1;
    }

    clients[slot].aircraftID        = aircraftID;
    clients[slot].socketFD          = clientSocket;
    clients[slot].handshakeComplete = true;
    clients[slot].connectedAt       = time(NULL);

    send(clientSocket, HANDSHAKE_ACK, strlen(HANDSHAKE_ACK), 0);
    LOG_INFO(aircraftID, "Handshake complete — client registered");

    return aircraftID;
}

// validatePacket — REQ-COM-060
// Returns true if: packet != NULL, aircraftID > 0, fuelLevel in [0,100], timestamp != 0.
bool validatePacket(const FuelPacket *packet) {
    if (packet == NULL)                            return false;
    if (packet->header.aircraftID <= 0)            return false;
    if (packet->body.fuelLevel < 0.0f ||
        packet->body.fuelLevel > 100.0f)           return false;
    if (packet->header.timestamp == 0)             return false;
    return true;
}

// Main server loop
#ifndef TESTING
int main(void) {
    socket_t serverFD = initServer();
    if (serverFD == INVALID_SOCK) {
        fprintf(stderr, "Failed to start server. Exiting.\n");
        return EXIT_FAILURE;
    }

    printf("Aircraft Fuel Monitoring Server started. Waiting for connections...\n");

    while (1) {
        socket_t clientFD = acceptClient(serverFD);
        if (clientFD == INVALID_SOCK) continue;

        int aircraftID = performHandshake(clientFD);
        if (aircraftID == -1) {
            CLOSE_SOCKET(clientFD);
            continue;
        }

        // TODO (Sprint 2): handle concurrent clients with threads or select/poll.
        // TODO (Sprint 2): receive FuelPacket loop, validate, send ACK_DIVERT or LANDED_SAFE.
    }

    CLOSE_SOCKET(serverFD);

#ifdef _WIN32
    WSACleanup();
#endif

    return EXIT_SUCCESS;
}
#endif
