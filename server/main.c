/*
 * server/main.c
 * Aircraft Fuel Monitoring System — Server TCP Connection
 *
 * Implements:
 *   initServer()       — REQ-COM-040: TCP socket creation and bind
 *   acceptClient()     — REQ-SVR-010: accept incoming aircraft connections
 *   performHandshake() — REQ-COM-050: handshake before any data packets
 *   validatePacket()   — REQ-COM-060: packet integrity validation
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/server.h"
#include "../common/packet.h"

#ifdef _WIN32
    /* Winsock error code for a failed recv/send due to timeout */
    #define SOCK_TIMEOUT_ERR WSAETIMEDOUT
#else
    #include <errno.h>
    #define SOCK_TIMEOUT_ERR EAGAIN
#endif

/* ── Connected client table (module-private) ──────────────────────────────── */
static ConnectedClient clients[MAX_CLIENTS];

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/* Returns the index of the slot occupied by aircraftID, or -1 if not found. */
static int findClientByAircraftID(int aircraftID) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].aircraftID == aircraftID) return i;
    }
    return -1;
}

/* Returns the index of the first empty slot (aircraftID == 0), or -1 if full. */
static int findEmptySlot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].aircraftID == 0) return i;
    }
    return -1;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

/* resetClients — zero the entire client table.
 * Called between unit tests to restore clean state. */
void resetClients(void) {
    memset(clients, 0, sizeof(clients));
}

/*
 * initServer — REQ-COM-040
 * Initialise Winsock (Windows), create a TCP socket, set SO_REUSEADDR,
 * bind to SERVER_PORT on all interfaces, and start listening.
 * Returns the listening socket fd, or INVALID_SOCK on any failure.
 */
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

    /* Allow fast restart without TIME_WAIT blocking the bind. */
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

/*
 * acceptClient — REQ-SVR-010
 * Blocks until a client connects. Sets a receive timeout on the new socket
 * so that performHandshake() does not block forever.
 * Logs the client's IP address.
 * Returns the accepted socket fd, or INVALID_SOCK on error.
 */
socket_t acceptClient(socket_t serverSocket) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    socket_t clientFD = accept(serverSocket, (struct sockaddr *)&clientAddr, &addrLen);
    if (clientFD == INVALID_SOCK) {
        LOG_ERROR(0, "accept() failed");
        return INVALID_SOCK;
    }

    /* Set receive timeout so a slow/silent client doesn't stall the server. */
#ifdef _WIN32
    DWORD timeout = HANDSHAKE_TIMEOUT * 1000;
    setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec  = HANDSHAKE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "Connection attempt from %s", clientIP);
    LOG_INFO(0, logMsg);

    return clientFD;
}

/*
 * performHandshake — REQ-COM-050
 * Reads the first message from the client and validates it as a handshake
 * request.  Any data that is not a HANDSHAKE_REQ string (e.g. a raw
 * FuelPacket) causes immediate rejection — enforcing REQ-COM-050.
 *
 * On success: registers the client in the tracking table and sends
 *             HANDSHAKE_ACK.  Returns the positive aircraftID.
 * On failure: sends the appropriate HANDSHAKE_REJ_* reply and returns -1.
 */
int performHandshake(socket_t clientSocket) {
    char buf[HANDSHAKE_MSG_MAX_LEN];
    memset(buf, 0, sizeof(buf));

    int received = (int)recv(clientSocket, buf, sizeof(buf) - 1, 0);
    if (received <= 0) {
        /* Timeout, disconnection, or error. */
        LOG_ERROR(0, "Handshake recv() failed — client disconnected or timed out");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    /* REQ-COM-050: the very first message must be the handshake prefix. */
    if (strncmp(buf, HANDSHAKE_REQ_PREFIX, strlen(HANDSHAKE_REQ_PREFIX)) != 0) {
        LOG_ERROR(0, "Handshake failed — data received before handshake (REQ-COM-050)");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    /* Parse the aircraftID that follows the prefix. */
    const char *idStr    = buf + strlen(HANDSHAKE_REQ_PREFIX);
    int         aircraftID = (int)strtol(idStr, NULL, 10);
    if (aircraftID <= 0) {
        LOG_ERROR(0, "Handshake failed — invalid aircraftID");
        send(clientSocket, HANDSHAKE_REJ_INVALID, strlen(HANDSHAKE_REJ_INVALID), 0);
        return -1;
    }

    /* REQ-SVR-010: reject duplicate connections from the same aircraft. */
    if (findClientByAircraftID(aircraftID) != -1) {
        LOG_ERROR(aircraftID, "Handshake failed — duplicate aircraftID");
        send(clientSocket, HANDSHAKE_REJ_DUP, strlen(HANDSHAKE_REJ_DUP), 0);
        return -1;
    }

    /* Find a free slot in the tracking table. */
    int slot = findEmptySlot();
    if (slot == -1) {
        LOG_ERROR(aircraftID, "Handshake failed — server is full");
        send(clientSocket, HANDSHAKE_REJ_FULL, strlen(HANDSHAKE_REJ_FULL), 0);
        return -1;
    }

    /* Register client. */
    clients[slot].aircraftID       = aircraftID;
    clients[slot].socketFD         = clientSocket;
    clients[slot].handshakeComplete = true;
    clients[slot].connectedAt      = time(NULL);

    send(clientSocket, HANDSHAKE_ACK, strlen(HANDSHAKE_ACK), 0);
    LOG_INFO(aircraftID, "Handshake complete — client registered");

    return aircraftID;
}

/*
 * validatePacket — REQ-COM-060
 * Returns true only if all of the following hold:
 *   - packet is not NULL
 *   - header.aircraftID > 0
 *   - body.fuelLevel is in [0.0, 100.0]
 *   - header.timestamp is not 0
 */
bool validatePacket(const FuelPacket *packet) {
    if (packet == NULL)                            return false;
    if (packet->header.aircraftID <= 0)            return false;
    if (packet->body.fuelLevel < 0.0f ||
        packet->body.fuelLevel > 100.0f)           return false;
    if (packet->header.timestamp == 0)             return false;
    return true;
}

/* ── Main server loop ─────────────────────────────────────────────────────── */
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
            /* Handshake failed — close connection immediately. */
            CLOSE_SOCKET(clientFD);
            continue;
        }

        /*
         * TODO (Sprint 2): Move each accepted client to a thread or integrate
         * with select()/poll() for concurrent multi-aircraft packet handling.
         * For now, the main loop blocks on the next accept() call.
         */

        /*
         * TODO (Sprint 2): Implement packet receive loop here.
         * Receive FuelPacket, call validatePacket(), process fuel state,
         * send ACK_DIVERT or LANDED_SAFE responses as needed.
         */
    }

    /* Unreachable in current form; Sprint 2 will add signal handling. */
    CLOSE_SOCKET(serverFD);

#ifdef _WIN32
    WSACleanup();
#endif

    return EXIT_SUCCESS;
}
#endif /* TESTING */
