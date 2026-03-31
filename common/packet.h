#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <time.h>

// Packet types
typedef enum {
  FUEL_STATUS, // Regular fuel status report
  LANDED_SAFE, // Aircraft landed safely
  ACK_DIVERT   // Acknowledgement of emergency divert command
} PacketType;

// Aircraft fuel states
typedef enum {
  STATE_NORMAL_CRUISE,    // Fuel above 25%
  STATE_LOW_FUEL,         // Fuel below 25%
  STATE_CRITICAL_FUEL,    // Fuel below 15%
  STATE_EMERGENCY_DIVERT, // Divert command received and ACK sent
  STATE_LANDED_SAFE       // Aircraft landed
} FuelState;

// Packet header
typedef struct {
  int packetID;     // Unique packet identifier
  PacketType type;
  int aircraftID;   // REQ-PKT-010
  time_t timestamp; // Packet creation time (client side)
} PacketHeader;

// Packet body
typedef struct {
  float fuelLevel;            // REQ-PKT-020: current fuel level (0–100%)
  float consumptionRate;      // REQ-PKT-030: fuel consumption per minute
  float flightTimeRemaining;  // REQ-PKT-040: estimated flight time remaining
  int nearestAirportID;       // REQ-PKT-050
  int destinationAirportID;
  float timeToDestination;    // Estimated time to destination (minutes)
  FuelState currentState;
  bool emergencyFlag;         // REQ-PKT-060: true if in emergency fuel state
  char *alertMessage;         // REQ-PKT-070: dynamically allocated alert message
} PacketBody;

// Full packet
typedef struct {
  PacketHeader header;
  PacketBody body;
} FuelPacket;

// Fuel thresholds
#define FUEL_THRESHOLD_LOW       25.0f // REQ-STM-020
#define FUEL_THRESHOLD_CRITICAL  15.0f // REQ-STM-030

// Utility functions
FuelPacket *createPacket(int aircraftID, PacketType type);
void freePacket(FuelPacket *packet);
int setAlertMessage(FuelPacket *packet, const char *message);

#endif /* PACKET_H */
