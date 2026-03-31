/*
common/packet.c
Aircraft Fuel Monitoring System — Packet Utilities

Implements: createPacket, freePacket, setAlertMessage
*/

#include "packet.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// NOTE: not thread-safe — assumes single-threaded use.
// Use _Atomic or a mutex if concurrent packet creation is needed.
static int nextPacketID = 1;

// createPacket — allocate and zero a FuelPacket, fill header fields.
// Body fields default to 0. Returns NULL on allocation failure.
FuelPacket *createPacket(int aircraftID, PacketType type) {
    FuelPacket *packet = (FuelPacket *)calloc(1, sizeof(FuelPacket));
    if (packet == NULL) return NULL;

    packet->header.packetID   = nextPacketID++;
    packet->header.type       = type;
    packet->header.aircraftID = aircraftID; // REQ-PKT-010
    packet->header.timestamp  = time(NULL);

    return packet;
}

// freePacket — free alertMessage (REQ-PKT-070) then the packet.
void freePacket(FuelPacket *packet) {
    if (packet == NULL) return;
    if (packet->body.alertMessage != NULL) {
        free(packet->body.alertMessage);
        packet->body.alertMessage = NULL;
    }
    free(packet);
}

// setAlertMessage — REQ-PKT-070
// Allocate-then-swap: old message is preserved on malloc failure.
// Returns 0 on success, -1 on failure.
int setAlertMessage(FuelPacket *packet, const char *message) {
    if (packet == NULL || message == NULL) return -1;

    char *newMsg = (char *)malloc(strlen(message) + 1);
    if (newMsg == NULL) return -1;

    strcpy(newMsg, message);
    free(packet->body.alertMessage);
    packet->body.alertMessage = newMsg;
    return 0;
}
