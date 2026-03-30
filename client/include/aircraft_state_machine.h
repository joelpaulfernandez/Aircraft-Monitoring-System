#ifndef AIRCRAFT_STATE_MACHINE_H
#define AIRCRAFT_STATE_MACHINE_H

#include <string>

// Aircraft states
enum class AircraftState {
    NORMAL_CRUISE,
    LOW_FUEL_WARNING,
    CRITICAL_FUEL,
    EMERGENCY_DIVERT,
    LANDED_SAFE
};

// State machine class
class AircraftStateMachine {
private:
    AircraftState currentState;
    double fuelLevel;
    bool landed;

public:
    AircraftStateMachine();

    void updateFuel(double fuel);
    void setLanded(bool status);

    AircraftState getState() const;
    std::string getStateString() const;

private:
    void updateState();
};

#endif