#ifndef CLIENT_H
#define CLIENT_H

#include "../../common/packet.h"

// Platform socket abstraction
// Guarded to avoid redefinition when server.h is also included.
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

// Public API

// Connect to serverAddr:port. Returns valid fd, or INVALID_SOCK on failure.
socket_t connectToServer(const char *serverAddr, int port);

// Send "HANDSHAKE_REQ:<aircraftID>\n" and wait for response.
// Returns 0 on HANDSHAKE_ACK, -1 otherwise.
int sendHandshake(socket_t fd, int aircraftID);

// Send a FuelPacket. Returns 0 on success, -1 on failure.
int sendFuelPacket(socket_t fd, const FuelPacket *packet);

// Blocking recv of a FuelPacket response from server. Returns 0 on success, -1 on error.
int recvServerResponse(socket_t fd, FuelPacket *response);

// Send ACK_DIVERT packet to server. Returns 0 on success, -1 on failure.
int sendAckDivert(socket_t fd, int aircraftID);

// Close the socket.
void disconnectFromServer(socket_t fd);

#endif /* CLIENT_H */
