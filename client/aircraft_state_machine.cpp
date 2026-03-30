#include "aircraft_state_machine.h"

// Thresholds (can be tuned)
const double LOW_FUEL_THRESHOLD = 50.0;
const double CRITICAL_FUEL_THRESHOLD = 20.0;
const double EMERGENCY_FUEL_THRESHOLD = 5.0;

// Constructor
AircraftStateMachine::AircraftStateMachine() {
    currentState = AircraftState::NORMAL_CRUISE;
    fuelLevel = 100.0;
    landed = false;
}

// Update fuel value
void AircraftStateMachine::updateFuel(double fuel) {
    fuelLevel = fuel;
    updateState();
}

// Set landed status
void AircraftStateMachine::setLanded(bool status) {
    landed = status;
    updateState();
}

// Get current state
AircraftState AircraftStateMachine::getState() const {
    return currentState;
}

// Convert state to string (useful for logging/debugging)
std::string AircraftStateMachine::getStateString() const {
    switch (currentState) {
        case AircraftState::NORMAL_CRUISE: return "Normal Cruise";
        case AircraftState::LOW_FUEL_WARNING: return "Low Fuel Warning";
        case AircraftState::CRITICAL_FUEL: return "Critical Fuel";
        case AircraftState::EMERGENCY_DIVERT: return "Emergency Divert";
        case AircraftState::LANDED_SAFE: return "Landed Safe";
        default: return "Unknown";
    }
}

// Core state transition logic
void AircraftStateMachine::updateState() {

    if (landed) {
        currentState = AircraftState::LANDED_SAFE;
        return;
    }

    if (fuelLevel > LOW_FUEL_THRESHOLD) {
        currentState = AircraftState::NORMAL_CRUISE;
    }
    else if (fuelLevel > CRITICAL_FUEL_THRESHOLD) {
        currentState = AircraftState::LOW_FUEL_WARNING;
    }
    else if (fuelLevel > EMERGENCY_FUEL_THRESHOLD) {
        currentState = AircraftState::CRITICAL_FUEL;
    }
    else {
        currentState = AircraftState::EMERGENCY_DIVERT;
    }
}