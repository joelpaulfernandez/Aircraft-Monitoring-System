#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <time.h>
#include "../../common/packet.h"

/* ── Platform abstraction ─────────────────────────────────────────────────── */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCK    INVALID_SOCKET
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCK    (-1)
    #define CLOSE_SOCKET(s) close(s)
#endif

/* ── Server constants ─────────────────────────────────────────────────────── */
#ifndef SERVER_PORT
#define SERVER_PORT           8080
#endif
#define MAX_CLIENTS           10
#define HANDSHAKE_TIMEOUT     5       /* seconds to wait for handshake */

/* ── Handshake protocol strings ───────────────────────────────────────────── */
/* Client sends: "HANDSHAKE_REQ:<aircraftID>\n"                               */
/* Server replies: "HANDSHAKE_ACK\n"  or  "HANDSHAKE_REJ:<reason>\n"         */
#define HANDSHAKE_REQ_PREFIX  "HANDSHAKE_REQ:"
#define HANDSHAKE_ACK         "HANDSHAKE_ACK\n"
#define HANDSHAKE_REJ_DUP     "HANDSHAKE_REJ:DUPLICATE\n"
#define HANDSHAKE_REJ_FULL    "HANDSHAKE_REJ:SERVER_FULL\n"
#define HANDSHAKE_REJ_INVALID "HANDSHAKE_REJ:INVALID\n"
#define HANDSHAKE_MSG_MAX_LEN 64

/* ── Connected client record ──────────────────────────────────────────────── */
typedef struct {
    int       aircraftID;       /* 0 means the slot is empty */
    socket_t  socketFD;
    bool      handshakeComplete;
    time_t    connectedAt;
} ConnectedClient;

/* ── Logging macros (REQ-LOG-060) ─────────────────────────────────────────── */
/* Format: DateTime | TYPE | AircraftID | Details                              */
/* Replaced with real logger calls once common/logger is implemented.         */
#include <stdio.h>
#include <time.h>

#define LOG_INFO(aircraftID, details) do {                                     \
    time_t _t = time(NULL);                                                    \
    char _buf[20];                                                             \
    strftime(_buf, sizeof(_buf), "%Y-%m-%d %H:%M:%S", localtime(&_t));        \
    fprintf(stderr, "%s | INFO  | AC-%d | %s\n", _buf, (aircraftID), (details)); \
} while (0)

#define LOG_ERROR(aircraftID, details) do {                                    \
    time_t _t = time(NULL);                                                    \
    char _buf[20];                                                             \
    strftime(_buf, sizeof(_buf), "%Y-%m-%d %H:%M:%S", localtime(&_t));        \
    fprintf(stderr, "%s | ERROR | AC-%d | %s\n", _buf, (aircraftID), (details)); \
} while (0)

/* ── Public function declarations ─────────────────────────────────────────── */

/* Initialize Winsock (Windows only), create TCP socket, bind to SERVER_PORT,
 * and begin listening. Returns listening socket fd, or INVALID_SOCK on error. */
socket_t initServer(void);

/* Block until a client connects. Logs the client IP.
 * Returns the accepted client socket fd, or INVALID_SOCK on error. */
socket_t acceptClient(socket_t serverSocket);

/* Perform the connection handshake with the newly accepted client.
 * - Expects "HANDSHAKE_REQ:<aircraftID>\n" as the very first message (REQ-COM-050).
 * - Any other data (e.g. a FuelPacket) is treated as an invalid handshake.
 * - Checks for duplicate aircraftID among already-connected clients.
 * - Sends HANDSHAKE_ACK on success, HANDSHAKE_REJ_* on failure.
 * Returns aircraftID (> 0) on success, -1 on failure. */
int performHandshake(socket_t clientSocket);

/* Validate a received FuelPacket (REQ-COM-060).
 * Returns true if:
 *   - packet is not NULL
 *   - header.aircraftID > 0
 *   - body.fuelLevel is in [0.0, 100.0]
 *   - header.timestamp != 0
 * Returns false otherwise. */
bool validatePacket(const FuelPacket *packet);

/* Zero out the connected-client tracking array.
 * Used between unit tests to reset state. */
void resetClients(void);

#endif /* SERVER_H */
