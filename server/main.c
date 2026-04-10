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

#ifndef TESTING
#include <pthread.h>
#endif

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

// sendStatusResponse — Request-Reply: send current fuel state back to client
// Called after FUEL_STATUS processing when no divert is issued.
static int sendStatusResponse(socket_t clientFD, int aircraftID, FuelState state) {
    FuelPacket resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.type       = FUEL_STATUS;
    resp.header.aircraftID = aircraftID;
    resp.header.timestamp  = time(NULL);
    resp.body.currentState = state;
    ssize_t sent = send(clientFD, (const char *)&resp, sizeof(resp), 0);
    return (sent == (ssize_t)sizeof(resp)) ? 0 : -1;
}

// handleAckDivert — clear awaitingACK when client confirms divert
static void handleAckDivert(int aircraftID) {
    AircraftRecord *rec = getAircraftRecord(aircraftID);
    if (rec == NULL) return;
    rec->awaitingACK = false;
    LOG_INFO(aircraftID, "ACK_DIVERT received — divert confirmed");
}

// removeClientSlot — clear connected client entry on disconnect
static void removeClientSlot(int aircraftID) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].aircraftID == aircraftID) {
            clients[i].aircraftID        = 0;
            clients[i].socketFD          = INVALID_SOCK;
            clients[i].handshakeComplete = false;
            break;
        }
    }
}

// evaluateDivertDecision — REQ-SVR-030
// Apply US5 divert decision logic: divert whenever the aircraft cannot reach
// its destination (flightTimeRemaining < timeToDestination), regardless of
// fuel state. Returns true if divert is needed.
bool evaluateDivertDecision(int aircraftID) {
    int idx = findAircraftRecord(aircraftID);
    if (idx == -1) return false;

    AircraftRecord *rec = &aircraftRecords[idx];

    // Do not re-issue if already in emergency divert or awaiting ACK
    if (rec->currentState == STATE_EMERGENCY_DIVERT) return false;
    if (rec->awaitingACK) return false;

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

    // Send DIVERT_CMD packet over TCP to client if connected
    int clientIdx = findClientByAircraftID(aircraftID);
    if (clientIdx != -1 && clients[clientIdx].handshakeComplete) {
        FuelPacket divertPkt;
        memset(&divertPkt, 0, sizeof(divertPkt));
        divertPkt.header.type        = DIVERT_CMD;
        divertPkt.header.aircraftID  = aircraftID;
        divertPkt.header.timestamp   = time(NULL);
        divertPkt.body.nearestAirportID = rec->nearestAirportID;
        send(clients[clientIdx].socketFD, (const char *)&divertPkt, sizeof(divertPkt), 0);
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

// ─── Multi-client threading ──────────────────────────────────────────────────
#ifndef TESTING

static int sendTelemetryFile(socket_t clientFD, int aircraftID);

// Protects clients[] and aircraftRecords[] from concurrent access.
static pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    socket_t clientFD;
    int      aircraftID;
} ClientArgs;

static void *clientThreadFunc(void *arg) {
    ClientArgs *ca         = (ClientArgs *)arg;
    socket_t    clientFD   = ca->clientFD;
    int         aircraftID = ca->aircraftID;
    free(ca);

    FuelPacket pkt;
    while (1) {
        ssize_t n = recv(clientFD, &pkt, sizeof(FuelPacket), MSG_WAITALL);
        if (n != (ssize_t)sizeof(FuelPacket)) break;

        pthread_mutex_lock(&tableMutex);

        if (!validatePacket(&pkt)) {
            LOG_WARNING(aircraftID, "Invalid packet — skipping");
            pthread_mutex_unlock(&tableMutex);
            continue;
        }
        if (pkt.header.aircraftID != aircraftID) {
            LOG_WARNING(aircraftID, "Packet aircraftID mismatch — skipping");
            pthread_mutex_unlock(&tableMutex);
            continue;
        }

        if (pkt.header.type == ACK_DIVERT) {
            handleAckDivert(aircraftID);
            pthread_mutex_unlock(&tableMutex);
            continue;
        }

        if (pkt.header.type == LANDED_SAFE) {
            LOG_INFO(aircraftID, "Aircraft reported LANDED_SAFE");
            pthread_mutex_unlock(&tableMutex);
            break;
        }

        // FUEL_STATUS: update record, decide divert
        updateAircraftRecord(&pkt);
        FuelState state    = checkFuelThresholds(&pkt);
        bool      dodivert = evaluateDivertDecision(aircraftID);

        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg),
                 "PACKET_RECV | Fuel: %.1f%% | FlightRem: %.1fmin | DestTime: %.1fmin | State: %s",
                 pkt.body.fuelLevel,
                 pkt.body.flightTimeRemaining,
                 pkt.body.timeToDestination,
                 fuelStateToString(state));
        LOG_INFO(aircraftID, logMsg);

        if (dodivert) {
            broadcastDivertCommand(aircraftID);
            sendTelemetryFile(clientFD, aircraftID);
        }
        pthread_mutex_unlock(&tableMutex);

        if (!dodivert) sendStatusResponse(clientFD, aircraftID, state);
    }

    CLOSE_SOCKET(clientFD);

    pthread_mutex_lock(&tableMutex);
    removeClientSlot(aircraftID);
    pthread_mutex_unlock(&tableMutex);

    return NULL;
}

// sendTelemetryFile — send telemetry.bin to a connected client
static int sendTelemetryFile(socket_t clientFD, int aircraftID) {
    FILE *file = fopen("telemetry.bin", "rb");
    if (!file) {
        logWrite(aircraftID, LOG_LEVEL_ERROR, "Telemetry file not found");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = (size_t)ftell(file);
    rewind(file);

    send(clientFD, (const char *)&file_size, sizeof(file_size), 0);

    char buffer[4096];

    logWrite(aircraftID, LOG_LEVEL_INFO, "SEND_FILE_START telemetry.bin");

    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(clientFD, buffer, (int)bytes, 0);
    }

    fclose(file);

    logWrite(aircraftID, LOG_LEVEL_INFO, "SEND_FILE_COMPLETE telemetry.bin");

    return 0;
}

// Main server loop
int main(void) {
    logInit("server.log");
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

        // Clear the handshake recv timeout so the packet loop blocks indefinitely.
#ifdef _WIN32
        DWORD noTimeout = 0;
        setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, (const char *)&noTimeout, sizeof(noTimeout));
#else
        struct timeval noTimeout = { 0, 0 };
        setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &noTimeout, sizeof(noTimeout));
#endif

        // Spawn a detached thread to handle this client concurrently.
        ClientArgs *ca = malloc(sizeof(ClientArgs));
        if (ca == NULL) {
            LOG_ERROR(aircraftID, "malloc failed for client thread args");
            CLOSE_SOCKET(clientFD);
            continue;
        }
        ca->clientFD   = clientFD;
        ca->aircraftID = aircraftID;

        pthread_t tid;
        if (pthread_create(&tid, NULL, clientThreadFunc, ca) != 0) {
            LOG_ERROR(aircraftID, "pthread_create failed");
            free(ca);
            CLOSE_SOCKET(clientFD);
            continue;
        }
        pthread_detach(tid);
    }

    CLOSE_SOCKET(serverFD);

#ifdef _WIN32
    WSACleanup();
#endif

    return EXIT_SUCCESS;
}
#endif
