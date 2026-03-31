#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <time.h>
#include "../../common/packet.h"

// Platform socket abstraction
// Guarded to avoid redefinition when client.h is also included.
#ifndef SOCKET_PLATFORM_DEFINED
#define SOCKET_PLATFORM_DEFINED
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
#endif

// Server constants
#ifndef SERVER_PORT
#define SERVER_PORT           8080
#endif
#define MAX_CLIENTS           10
#define HANDSHAKE_TIMEOUT     5  // seconds

// Handshake strings
// Client sends:  "HANDSHAKE_REQ:<aircraftID>\n"
// Server replies: "HANDSHAKE_ACK\n" or "HANDSHAKE_REJ:<reason>\n"
#define HANDSHAKE_REQ_PREFIX  "HANDSHAKE_REQ:"
#define HANDSHAKE_ACK         "HANDSHAKE_ACK\n"
#define HANDSHAKE_REJ_DUP     "HANDSHAKE_REJ:DUPLICATE\n"
#define HANDSHAKE_REJ_FULL    "HANDSHAKE_REJ:SERVER_FULL\n"
#define HANDSHAKE_REJ_INVALID "HANDSHAKE_REJ:INVALID\n"
#define HANDSHAKE_MSG_MAX_LEN 64

// Connected client record
typedef struct {
    int       aircraftID;        // 0 = empty slot
    socket_t  socketFD;
    bool      handshakeComplete;
    time_t    connectedAt;
} ConnectedClient;

// Use the common logger for all log output
#include "../../common/logger.h"

// Public API

// Create TCP socket, bind to SERVER_PORT, start listening.
// Returns listening fd, or INVALID_SOCK on error.
socket_t initServer(void);

// Block until a client connects. Returns accepted fd, or INVALID_SOCK on error.
socket_t acceptClient(socket_t serverSocket);

// Handshake with a newly accepted client (REQ-COM-050).
// Expects "HANDSHAKE_REQ:<aircraftID>\n" as the first message.
// Returns aircraftID (> 0) on success, -1 on failure.
int performHandshake(socket_t clientSocket);

// Validate a FuelPacket (REQ-COM-060).
// Returns true if: packet != NULL, aircraftID > 0, fuelLevel in [0,100], timestamp != 0.
bool validatePacket(const FuelPacket *packet);

// Zero the client tracking array. Call between unit tests.
void resetClients(void);

#endif /* SERVER_H */
