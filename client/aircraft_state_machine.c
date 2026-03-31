/*
aircraft_state_machine.c
Fuel-based state machine for a single aircraft.

State transitions driven by fuel level:
  fuel > FUEL_THRESHOLD_LOW (25%)       => STATE_NORMAL_CRUISE
  fuel > FUEL_THRESHOLD_CRITICAL (15%)  => STATE_LOW_FUEL
  fuel <= FUEL_THRESHOLD_CRITICAL       => STATE_CRITICAL_FUEL

STATE_EMERGENCY_DIVERT is a protocol state, not fuel-triggered.
It is entered only via smSetEmergencyDivert(), which is called when
the client receives an ACK_DIVERT packet from the server.
*/

#include <stdio.h>
#include "include/aircraft_state_machine.h"

void smInit(AircraftStateMachine *sm, int aircraftID) {
  sm->aircraftID    = aircraftID;
  sm->fuelLevel     = 100.0f;
  sm->landed        = false;
  sm->currentState  = STATE_NORMAL_CRUISE;
}

int smUpdateFuel(AircraftStateMachine *sm, float fuel) {
  if (fuel < 0.0f || fuel > 100.0f) {
    fprintf(stderr, "[SM] aircraft %d: invalid fuel value %.2f — state unchanged\n",
            sm->aircraftID, (double)fuel);
    return -1;
  }

  sm->fuelLevel = fuel;

  // Do not overwrite protocol-driven or terminal states.
  if (sm->landed || sm->currentState == STATE_EMERGENCY_DIVERT)
    return 0;

  if (fuel > FUEL_THRESHOLD_LOW)
    sm->currentState = STATE_NORMAL_CRUISE;
  else if (fuel > FUEL_THRESHOLD_CRITICAL)
    sm->currentState = STATE_LOW_FUEL;
  else
    sm->currentState = STATE_CRITICAL_FUEL;

  return 0;
}

void smSetEmergencyDivert(AircraftStateMachine *sm) {
  fprintf(stderr, "[SM] aircraft %d: emergency divert acknowledged (fuel %.1f%%)\n",
          sm->aircraftID, (double)sm->fuelLevel);
  sm->currentState = STATE_EMERGENCY_DIVERT;
}

void smSetLanded(AircraftStateMachine *sm, bool landed) {
  if (landed &&
      (sm->currentState == STATE_EMERGENCY_DIVERT ||
       sm->currentState == STATE_CRITICAL_FUEL)) {
    fprintf(stderr, "[SM] aircraft %d: landed during emergency state (%s)\n",
            sm->aircraftID, smGetStateString(sm));
  }
  sm->landed       = landed;
  sm->currentState = landed ? STATE_LANDED_SAFE : STATE_NORMAL_CRUISE;
}

FuelState smGetState(const AircraftStateMachine *sm) {
  return sm->currentState;
}

const char *smGetStateString(const AircraftStateMachine *sm) {
  switch (sm->currentState) {
    case STATE_NORMAL_CRUISE:    return "Normal Cruise";
    case STATE_LOW_FUEL:         return "Low Fuel Warning";
    case STATE_CRITICAL_FUEL:    return "Critical Fuel";
    case STATE_EMERGENCY_DIVERT: return "Emergency Divert";
    case STATE_LANDED_SAFE:      return "Landed Safe";
    default:
      fprintf(stderr, "[SM] aircraft %d: unknown state %d\n",
              sm->aircraftID, (int)sm->currentState);
      return "Unknown";
  }
}
