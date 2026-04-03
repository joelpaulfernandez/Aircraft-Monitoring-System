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

// Aircraft fuel tracking table (module-private)
static AircraftRecord aircraftRecords[MAX_AIRCRAFT];

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

// ─── Aircraft record helpers ────────────────────────────────────────

// Returns the index of the slot with aircraftID, or -1 if not found.
static int findAircraftRecord(int aircraftID) {
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (aircraftRecords[i].aircraftID == aircraftID) return i;
    }
    return -1;
}

// Returns the index of the first empty aircraft record slot, or -1 if full.
static int findEmptyAircraftSlot(void) {
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (aircraftRecords[i].aircraftID == 0) return i;
    }
    return -1;
}

// Returns a human-readable string for a FuelState enum value.
static const char *fuelStateToString(FuelState state) {
    switch (state) {
        case STATE_NORMAL_CRUISE:    return "NORMAL_CRUISE";
        case STATE_LOW_FUEL:         return "LOW_FUEL";
        case STATE_CRITICAL_FUEL:    return "CRITICAL_FUEL";
        case STATE_EMERGENCY_DIVERT: return "EMERGENCY_DIVERT";
        case STATE_LANDED_SAFE:      return "LANDED_SAFE";
        default:                     return "UNKNOWN";
    }
}

// Determine fuel state from fuel level using threshold constants.
// Boundary logic matches client state machine:
//   fuel > 25.0 -> NORMAL_CRUISE
//   fuel > 15.0 -> LOW_FUEL
//   fuel <= 15.0 -> CRITICAL_FUEL
static FuelState determineFuelState(float fuelLevel) {
    if (fuelLevel > FUEL_THRESHOLD_LOW)      return STATE_NORMAL_CRUISE;
    if (fuelLevel > FUEL_THRESHOLD_CRITICAL)  return STATE_LOW_FUEL;
    return STATE_CRITICAL_FUEL;
}

// Zero the aircraft records table. Called between unit tests.
void resetAircraftRecords(void) {
    memset(aircraftRecords, 0, sizeof(aircraftRecords));
}

// Look up an AircraftRecord by aircraftID. Returns pointer or NULL.
AircraftRecord *getAircraftRecord(int aircraftID) {
    int idx = findAircraftRecord(aircraftID);
    if (idx == -1) return NULL;
    return &aircraftRecords[idx];
}

// checkFuelThresholds — REQ-STM-010, REQ-STM-020, REQ-STM-030, REQ-LOG-030
// Determine FuelState from packet fuel level. If a record exists for this
// aircraft, detect and log state transitions.
FuelState checkFuelThresholds(const FuelPacket *packet) {
    if (packet == NULL) return STATE_NORMAL_CRUISE;

    FuelState newState = determineFuelState(packet->body.fuelLevel);

    int idx = findAircraftRecord(packet->header.aircraftID);
    if (idx != -1) {
        FuelState oldState = aircraftRecords[idx].currentState;
        if (oldState != newState) {
            char logMsg[128];
            snprintf(logMsg, sizeof(logMsg),
                     "State transition: %s -> %s | Fuel: %.1f%%",
                     fuelStateToString(oldState),
                     fuelStateToString(newState),
                     packet->body.fuelLevel);

            if (newState == STATE_CRITICAL_FUEL) {
                LOG_ERROR(packet->header.aircraftID, logMsg);
            } else if (newState == STATE_LOW_FUEL) {
                LOG_WARNING(packet->header.aircraftID, logMsg);
            } else {
                LOG_INFO(packet->header.aircraftID, logMsg);
            }
        }
    }

    return newState;
}

// updateAircraftRecord — REQ-SVR-020
// Update or create the server-side AircraftRecord for the aircraft in the packet.
// Returns 0 on success, -1 if the table is full or packet is NULL.
int updateAircraftRecord(const FuelPacket *packet) {
    if (packet == NULL) return -1;

    int idx = findAircraftRecord(packet->header.aircraftID);
    if (idx == -1) {
        idx = findEmptyAircraftSlot();
        if (idx == -1) return -1;
        aircraftRecords[idx].aircraftID = packet->header.aircraftID;
    }

    // Detect and log state transition before updating the record
    FuelState newState = checkFuelThresholds(packet);

    aircraftRecords[idx].currentState        = newState;
    aircraftRecords[idx].lastFuelLevel       = packet->body.fuelLevel;
    aircraftRecords[idx].nearestAirportID    = packet->body.nearestAirportID;
    aircraftRecords[idx].destinationAirportID = packet->body.destinationAirportID;
    aircraftRecords[idx].flightTimeRemaining = packet->body.flightTimeRemaining;
    aircraftRecords[idx].timeToDestination   = packet->body.timeToDestination;
    aircraftRecords[idx].isActive            = true;

    return 0;
}

// evaluateDivertDecision — REQ-SVR-030
// Apply US5 divert decision logic: only divert when in CRITICAL_FUEL and
// flightTimeRemaining < timeToDestination. Returns true if divert is needed.
bool evaluateDivertDecision(int aircraftID) {
    int idx = findAircraftRecord(aircraftID);
    if (idx == -1) return false;

    AircraftRecord *rec = &aircraftRecords[idx];

    // Only evaluate divert for CRITICAL_FUEL state
    if (rec->currentState != STATE_CRITICAL_FUEL) return false;

    // US5: divert if aircraft cannot reach destination
    if (rec->flightTimeRemaining < rec->timeToDestination) return true;

    return false;
}

// broadcastDivertCommand — REQ-SVR-050, REQ-LOG-040
// Issue a divert command for the given aircraft. Updates record state,
// records timestamp, and sends DIVERT_CMD over TCP if connected.
// Returns 0 on success, -1 on failure.
int broadcastDivertCommand(int aircraftID) {
    int idx = findAircraftRecord(aircraftID);
    if (idx == -1) return -1;

    AircraftRecord *rec = &aircraftRecords[idx];

    // Do not re-issue if already awaiting ACK
    if (rec->awaitingACK) return -1;

    // Update record to EMERGENCY_DIVERT state
    rec->currentState     = STATE_EMERGENCY_DIVERT;
    rec->awaitingACK      = true;
    rec->divertCommandTime = time(NULL);

    // Log the divert command (REQ-LOG-040)
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg),
             "DIVERT_CMD | AssignedAirport: %d",
             rec->nearestAirportID);
    LOG_WARNING(aircraftID, logMsg);

    // Send DIVERT_CMD over TCP to client if connected
    int clientIdx = findClientByAircraftID(aircraftID);
    if (clientIdx != -1 && clients[clientIdx].handshakeComplete) {
        const char *cmd = "DIVERT_CMD\n";
        send(clients[clientIdx].socketFD, cmd, (int)strlen(cmd), 0);
    }

    return 0;
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
