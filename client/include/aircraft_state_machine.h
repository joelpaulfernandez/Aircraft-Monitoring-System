#ifndef AIRCRAFT_STATE_MACHINE_H
#define AIRCRAFT_STATE_MACHINE_H

#include <stdbool.h>
#include "../../common/packet.h"

// State machine instance — one per aircraft
typedef struct {
  int       aircraftID;
  FuelState currentState;
  float     fuelLevel;
  bool      landed;
} AircraftStateMachine;

// Initialize sm for the given aircraft. Sets fuel to 100% and state to STATE_NORMAL_CRUISE.
void smInit(AircraftStateMachine *sm, int aircraftID);

// Update fuel level. Returns 0 on success, -1 if fuel is out of range [0, 100].
// State is not modified on invalid input.
int smUpdateFuel(AircraftStateMachine *sm, float fuel);

// Set emergency divert state. Called when an ACK_DIVERT packet is received from
// the server. Once set, fuel updates no longer change the state.
void smSetEmergencyDivert(AircraftStateMachine *sm);

// Mark the aircraft as landed. Transitions state to STATE_LANDED_SAFE.
// If called during an emergency state, a warning is printed to stderr.
void smSetLanded(AircraftStateMachine *sm, bool landed);

// Return the current state.
FuelState smGetState(const AircraftStateMachine *sm);

// Return a human-readable label for the current state.
const char *smGetStateString(const AircraftStateMachine *sm);

#endif /* AIRCRAFT_STATE_MACHINE_H */
