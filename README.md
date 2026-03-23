# Aircraft Fuel Monitoring System
Group 1 - CSCN74000

## Overview
A distributed client-server system that monitors aircraft fuel levels in real time,
detects critical fuel states, and issues emergency divert commands when necessary.

## Team
- Katarina Lukic
- Chris Chae (Youngsu Chae)
- Joel Paul Fernandez

## Tech Stack
- Language: C
- Communication: TCP/IP (Winsock2 on Windows / sys/socket on Linux)
- Testing: MSTest
- Version Control: GitHub

## Project Structure
```
aircraft-fuel-monitoring-system/
├── client/
│   ├── main.c              # Client entry point
│   └── src/
│       ├── state_machine.h # Fuel state machine (REQ-STM-010~040)
│       └── state_machine.c
├── server/
│   └── main.c              # Server entry point
├── packet/
│   ├── packet.h            # FuelPacket struct definition
│   ├── packet.c            # Packet utility functions
│   ├── logger.h            # Logging interface (REQ-LOG-060)
│   └── logger.c            # Logging implementation
├── docs/
│   └── requirements/       # Project requirements documents
├── tests/
│   ├── unit/               # MSTest unit tests
│   └── integration/        # Integration tests
├── logs/                   # Runtime log files (git ignored)
│   └── .gitkeep
└── README.md
```

## Fuel State Thresholds
| State | Condition |
|-------|-----------|
| Normal Cruise | Fuel > 25% |
| Low Fuel Warning | Fuel < 25% |
| Critical Fuel | Fuel < 15% |
| Emergency Divert | Divert command received from server |
| Landed Safe | Aircraft confirmed landing |

## Data Packet Structure
```c
FuelPacket {
    PacketHeader {
        packetID, type, aircraftID, timestamp
    }
    PacketBody {
        fuelLevel, consumptionRate, flightTimeRemaining,
        nearestAirportID, destinationAirportID,
        timeToDestination, currentState,
        emergencyFlag, alertMessage*  // dynamically allocated
    }
}
```

## Sprint Plan
### Sprint 1
- TCP/IP connection setup
- 3-way handshake verification
- Data packet structure
- State machine skeleton
- Basic logging
- Basic tests

### Sprint 2
- Fuel threshold detection logic
- Emergency divert command and ACK handling
- Divert decision logic (flightTimeRemaining vs timeToDestination)
- 1MB telemetry file transfer
- Logging format finalization
- UI implementation
- Full unit tests

## Log Format
```
DateTime | TYPE | AircraftID | Details
2026-03-04 10:32:11 | PACKET_RECV | AC-101 | Fuel: 12.4% | State: CRITICAL_FUEL
2026-03-04 10:32:12 | DIVERT | AC-101 | DIVERT_CMD | AssignedAirport: 1002
```