/*
client/client.c
Aircraft Fuel Monitoring System — Client TCP Connection

Implements: connectToServer, sendHandshake, sendFuelPacket, disconnectFromServer
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/client.h"
#include "../common/packet.h"
#include <stdint.h>

#define HANDSHAKE_BUF_LEN 64

// Create TCP socket and connect to serverAddr:port.
// Returns fd on success, INVALID_SOCK on failure.
socket_t connectToServer(const char *serverAddr, int port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, serverAddr, &addr.sin_addr) != 1) {
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

    return fd;
}

// Send handshake request and wait for server response.
// Returns 0 on HANDSHAKE_ACK, -1 otherwise.
int sendHandshake(socket_t fd, int aircraftID) {
    char req[HANDSHAKE_BUF_LEN];
    snprintf(req, sizeof(req), "HANDSHAKE_REQ:%d\n", aircraftID);

    if (send(fd, req, strlen(req), 0) < 0) return -1;

    char resp[HANDSHAKE_BUF_LEN];
    memset(resp, 0, sizeof(resp));
    if (recv(fd, resp, sizeof(resp) - 1, 0) <= 0) return -1;

    return (strncmp(resp, "HANDSHAKE_ACK", strlen("HANDSHAKE_ACK")) == 0) ? 0 : -1;
}

// Send raw FuelPacket bytes. Returns 0 on success, -1 on failure.
int sendFuelPacket(socket_t fd, const FuelPacket *packet) {
    ssize_t sent = send(fd, packet, sizeof(FuelPacket), 0);
    return (sent == (ssize_t)sizeof(FuelPacket)) ? 0 : -1;
}

// Close the socket.
void disconnectFromServer(socket_t fd) {
    CLOSE_SOCKET(fd);
}
