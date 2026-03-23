#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <time.h>


// Packet Types
typedef enum {
  FUEL_STATUS, // Regular fuel status report
  LANDED_SAFE, // Aircraft has landed safely
  ACK_DIVERT   // Acknowledgement of emergency divert command
} PacketType;

// Aircraft Fuel States
typedef enum {
  STATE_NORMAL_CRUISE,    // Fuel above 25%
  STATE_LOW_FUEL,         // Fuel below 25%
  STATE_CRITICAL_FUEL,    // Fuel below 15%
  STATE_EMERGENCY_DIVERT, // Divert command received and ACK sent
  STATE_LANDED_SAFE       // Aircraft has landed
} FuelState;

// Packet Header
typedef struct {
  int packetID;     // Unique packet identifier
  PacketType type;  // Type of packet being transmitted
  int aircraftID;   // REQ-PKT-010: Unique aircraft identifier
  time_t timestamp; // Time of packet creation on client side
} PacketHeader;

// Packet Body
typedef struct {
  float fuelLevel;           // REQ-PKT-020: Current fuel level (0-100%)
  float consumptionRate;     // REQ-PKT-030: Fuel consumption rate per minute
  float flightTimeRemaining; // REQ-PKT-040: Estimated flight time remaining
                             // (calculated by client)
  int nearestAirportID;      // REQ-PKT-050: ID of nearest available airport
  int destinationAirportID;  // Destination airport ID
  float timeToDestination;   // Estimated time to destination in minutes
                             // (calculated by client)
  FuelState currentState;    // Current fuel state of aircraft
  bool
      emergencyFlag; // REQ-PKT-060: True if aircraft is in emergency fuel state
  char *alertMessage; // REQ-PKT-070: Dynamically allocated alert or status
                      // message
} PacketBody;

// Full Packet
typedef struct {
  PacketHeader header; // Packet metadata
  PacketBody body;     // Fuel status payload
} FuelPacket;

// Fuel Thresholds
#define FUEL_THRESHOLD_LOW 25.0f      // REQ-STM-020
#define FUEL_THRESHOLD_CRITICAL 15.0f // REQ-STM-030

// Packet Utility Functions
FuelPacket *createPacket(int aircraftID, PacketType type);
void freePacket(FuelPacket *packet);
void setAlertMessage(FuelPacket *packet, const char *message);

#endif // PACKET_H