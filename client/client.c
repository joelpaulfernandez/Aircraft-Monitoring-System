/*
client/client.c
Aircraft Fuel Monitoring System — Client TCP Connection

Implements: connectToServer, sendHandshake, sendFuelPacket, disconnectFromServer
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/client.h"
#include "../common/packet.h"
#include <stdint.h>
#include "../common/logger.h"

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

// Blocking recv of a FuelPacket response from the server.
// Returns 0 on success, -1 on disconnect or error.
int recvServerResponse(socket_t fd, FuelPacket *response) {
    if (response == NULL) return -1;

    ssize_t n = recv(fd, response, sizeof(FuelPacket), MSG_WAITALL);
    if (n != (ssize_t)sizeof(FuelPacket)) return -1;

  
    if (response->header.type == DIVERT_CMD) {
        logWrite(response->header.aircraftID, LOG_LEVEL_WARNING,
                 "DIVERT_CMD received — receiving telemetry file");

        receiveTelemetryFile(fd, response->header.aircraftID);
    }

    return 0;
}

// Send ACK_DIVERT packet to server. Returns 0 on success, -1 on failure.
int sendAckDivert(socket_t fd, int aircraftID) {
    FuelPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.type       = ACK_DIVERT;
    pkt.header.aircraftID = aircraftID;
    pkt.header.timestamp  = time(NULL);
    ssize_t sent = send(fd, &pkt, sizeof(FuelPacket), 0);
    return (sent == (ssize_t)sizeof(FuelPacket)) ? 0 : -1;
}

// Close the socket.
void disconnectFromServer(socket_t fd) {
    CLOSE_SOCKET(fd);
}

int receiveTelemetryFile(socket_t fd, int aircraftID) {
    size_t file_size;

    if (recv(fd, &file_size, sizeof(file_size), 0) <= 0) {
        logWrite(aircraftID, LOG_LEVEL_ERROR, "Failed to receive file size");
        return -1;
    }

    FILE *file = fopen("received_telemetry.bin", "wb");
    if (!file) {
        logWrite(aircraftID, LOG_LEVEL_ERROR, "Failed to open file");
        return -1;
    }

    char buffer[4096];
    size_t total_received = 0;

    logWrite(aircraftID, LOG_LEVEL_INFO, "RECEIVE_START telemetry.bin");

    while (total_received < file_size) {
        int bytes = recv(fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        fwrite(buffer, 1, bytes, file);
        total_received += bytes;
    }

    fclose(file);

    if (total_received == file_size) {
        logWrite(aircraftID, LOG_LEVEL_INFO, "RECEIVE_COMPLETE telemetry.bin");
        return 0;
    } else {
        logWrite(aircraftID, LOG_LEVEL_ERROR, "RECEIVE_INCOMPLETE telemetry.bin");
        return -1;
    }
}