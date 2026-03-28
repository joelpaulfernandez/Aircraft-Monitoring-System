/*
 * common/packet.c
 * Aircraft Fuel Monitoring System — Packet Utilities
 *
 * Implements:
 *   createPacket()    — allocate and initialise a FuelPacket
 *   freePacket()      — release a FuelPacket and its dynamic fields
 *   setAlertMessage() — REQ-PKT-070: set the dynamically allocated alert message
 */

#include "packet.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static int nextPacketID = 1;

// createPacket
// Allocates a zeroed FuelPacket, fills header fields, and returns it.
// Body fields (fuelLevel, consumptionRate, etc.) are left at 0 for the caller to set.
// Returns NULL on allocation failure.
FuelPacket *createPacket(int aircraftID, PacketType type) {
    FuelPacket *packet = (FuelPacket *)calloc(1, sizeof(FuelPacket));
    if (packet == NULL) return NULL;

    packet->header.packetID   = nextPacketID++;
    packet->header.type       = type;
    packet->header.aircraftID = aircraftID; // REQ-PKT-010
    packet->header.timestamp  = time(NULL);

    return packet;
}

// freePacket
// Frees the dynamically allocated alertMessage (REQ-PKT-070) then the packet itself.
void freePacket(FuelPacket *packet) {
    if (packet == NULL) return;
    if (packet->body.alertMessage != NULL) {
        free(packet->body.alertMessage);
        packet->body.alertMessage = NULL;
    }
    free(packet);
}

// setAlertMessage — REQ-PKT-070
// Copies message into a newly allocated buffer on the packet.
// Frees any previously set message first.
void setAlertMessage(FuelPacket *packet, const char *message) {
    if (packet == NULL || message == NULL) return;

    free(packet->body.alertMessage);

    packet->body.alertMessage = (char *)malloc(strlen(message) + 1);
    if (packet->body.alertMessage != NULL) {
        strcpy(packet->body.alertMessage, message);
    }
}
