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
- **Language**: C (C11)
- **Communication**: TCP/IP (Winsock2 on Windows / sys/socket on Linux)
- **Testing**: Custom C Unit Testing Harness (Integrated in Makefile)
- **Version Control**: GitHub

## Project Structure
```
Aircraft-Monitoring-System/
├── bin/                    # Compiled binaries (git ignored)
├── client/
│   ├── include/            # Client-specific headers
│   ├── aircraft_state_machine.c # Fuel state machine logic (REQ-STM-010~040)
│   ├── client.c            # Client communication logic
│   └── main.c              # Client entry point
├── server/
│   ├── include/            # Server-specific headers
│   └── main.c              # Server entry point / logic
├── common/
│   ├── packet.h/c          # FuelPacket struct and utility functions
│   └── logger.h/c          # Logging interface and implementation (REQ-LOG-060)
├── tests/
│   └── unit/               # Unit tests for each module
│       ├── test_fuel_system.c
│       ├── test_client_connection.c
│       └── test_server_connection.c
├── Makefile                # Build system
└── README.md
```

## Fuel State Thresholds
| State | Condition |
|-------|-----------|
| Normal Cruise | Fuel > 25% |
| Low Fuel Warning | Fuel <= 25% |
| Critical Fuel | Fuel <= 15% |
| Emergency Divert | Divert command received from server |
| Landed Safe | Aircraft confirmed landing |

## Data Packet Structure
```c
typedef struct {
    PacketHeader header;    // ID, type, aircraftID, timestamp
    PacketBody body;        // fuelLevel, consumptionRate, states, etc.
} FuelPacket;
```

## Build and Test Instructions

### Build Server
```bash
# Build server
make server
```

### Run Unit Tests
Tests are implemented using a custom C testing harness to ensure compatibility and lightweight execution without external dependencies.

```bash
# Run specific tests
make test_fuel_system
make test_client_connection
make test_server_connection
make test_packet
make test_logger
```

## Project Progress (Phase 1)
- [x] TCP/IP Socket connection setup
- [x] 3-way handshake verification logic
- [x] Data packet (FuelPacket) structure definition
- [x] Fuel State Machine skeleton and threshold logic
- [x] Basic synchronous logging system
- [x] Unit tests for core modules (Fuel, Packet, Connection)

## Log Format
```
DateTime | TYPE | AircraftID | Details
2026-03-30 21:45:11 | PACKET_RECV | AC-101 | Fuel: 12.4% | State: CRITICAL_FUEL
```