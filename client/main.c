/*
client/main.c
Aircraft Fuel Monitoring System — Client Main Loop

Connects to the server, performs handshake, then enters a request-reply loop:
  sendFuelPacket → recvServerResponse → handle DIVERT_CMD or status ack
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include/client.h"
#include "include/aircraft_state_machine.h"
#include "../common/packet.h"

#ifndef SERVER_PORT
#define SERVER_PORT 8080
#endif
#define SERVER_ADDR "127.0.0.1"

// Build a FUEL_STATUS packet from the current state machine.
static FuelPacket buildFuelPacket(const AircraftStateMachine *sm, int packetID) {
    FuelPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.packetID   = packetID;
    pkt.header.type       = FUEL_STATUS;
    pkt.header.aircraftID = sm->aircraftID;
    pkt.header.timestamp  = time(NULL);
    pkt.body.fuelLevel    = sm->fuelLevel;
    pkt.body.currentState = smGetState(sm);
    return pkt;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return EXIT_FAILURE;
    }
#endif

    const int aircraftID = 1;

    AircraftStateMachine sm;
    smInit(&sm, aircraftID);

    socket_t fd = connectToServer(SERVER_ADDR, SERVER_PORT);
    if (fd == INVALID_SOCK) {
        fprintf(stderr, "Failed to connect to server.\n");
        return EXIT_FAILURE;
    }

    if (sendHandshake(fd, aircraftID) != 0) {
        fprintf(stderr, "Handshake failed.\n");
        disconnectFromServer(fd);
        return EXIT_FAILURE;
    }

    printf("Connected. Starting fuel telemetry loop...\n");

    int packetID = 1;
    while (1) {
        FuelPacket pkt = buildFuelPacket(&sm, packetID++);

        // Send FUEL_STATUS (or LANDED_SAFE)
        if (smGetState(&sm) == STATE_LANDED_SAFE) {
            pkt.header.type = LANDED_SAFE;
            sendFuelPacket(fd, &pkt);
            printf("Sent LANDED_SAFE. Disconnecting.\n");
            break;
        }

        if (sendFuelPacket(fd, &pkt) != 0) {
            fprintf(stderr, "Failed to send fuel packet.\n");
            break;
        }

        // Wait for server response (always 1 reply per packet)
        FuelPacket resp;
        if (recvServerResponse(fd, &resp) != 0) {
            fprintf(stderr, "Server disconnected.\n");
            break;
        }

        if (resp.header.type == DIVERT_CMD) {
            printf("DIVERT_CMD received — nearest airport: %d\n",
                   resp.body.nearestAirportID);
            smSetEmergencyDivert(&sm);
            if (sendAckDivert(fd, aircraftID) != 0) {
                fprintf(stderr, "Failed to send ACK_DIVERT.\n");
                break;
            }
            printf("ACK_DIVERT sent.\n");
        }
        // FUEL_STATUS response is a state ack — no action needed
    }

    disconnectFromServer(fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return EXIT_SUCCESS;
}
