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
- **Communication**: TCP/IP (Winsock2 on Windows / sys/socket on Linux/macOS)
- **Testing**: Custom C Unit Testing Harness (integrated in Makefile)
- **Version Control**: GitHub

## Project Structure
```
Aircraft-Monitoring-System/
├── bin/                          # Compiled binaries (git ignored)
├── client/
│   ├── include/
│   │   ├── client.h              # Client API declarations
│   │   └── aircraft_state_machine.h
│   ├── aircraft_state_machine.c  # Fuel state machine (REQ-STM-010~040)
│   ├── client.c                  # TCP connection, send/recv helpers
│   ├── main.c                    # Client entry point (telemetry loop)
│   └── demo_client.c             # Interactive demo client (manual testing)
├── server/
│   ├── include/
│   │   └── server.h              # Server API declarations
│   └── main.c                    # Server entry point, recv loop, divert logic
├── common/
│   ├── packet.h / packet.c       # FuelPacket struct, PacketType enum, utilities
│   └── logger.h / logger.c       # Logging interface (REQ-LOG-060)
├── tests/
│   └── unit/
│       ├── test_server_connection.c   # Server TCP/handshake tests
│       ├── test_client_connection.c   # Client TCP/handshake tests
│       ├── test_fuel_system.c         # State machine unit tests
│       ├── test_packet.c              # Packet struct/validation tests
│       ├── test_logger.c              # Logger unit tests
│       ├── test_fuel_threshold.c      # Fuel threshold detection tests
│       ├── test_divert_command.c      # Divert command unit tests
│       └── test_divert_integration.c  # End-to-end divert integration tests
├── Makefile
└── README.md
```

## Communication Protocol

### Packet Types
```c
typedef enum {
    FUEL_STATUS,   // Client → Server: telemetry update
    LANDED_SAFE,   // Client → Server: aircraft landed, end session
    ACK_DIVERT,    // Client → Server: divert command acknowledged
    DIVERT_CMD     // Server → Client: emergency divert command
} PacketType;
```

### Request-Reply Flow (one response per packet)
```
Client                              Server
  |                                   |
  |-- HANDSHAKE_REQ:<id>\n ---------->|
  |<-- HANDSHAKE_ACK ----------------|
  |                                   |
  |-- FUEL_STATUS (FuelPacket) ------>|  updateAircraftRecord()
  |                                   |  checkFuelThresholds()
  |                                   |  evaluateDivertDecision()
  |<-- DIVERT_CMD (FuelPacket) -------|    if divert needed
  |   OR FUEL_STATUS ack ------------|    if normal
  |                                   |
  |-- ACK_DIVERT (FuelPacket) ------->|  rec->awaitingACK = false
  |                                   |
  |-- LANDED_SAFE (FuelPacket) ------>|  server closes session
```

### Divert Decision Logic
The server issues `DIVERT_CMD` when **all** of the following are true:
- `flightTimeRemaining < timeToDestination` (can't reach destination regardless of fuel state)
- The aircraft is not already in `STATE_EMERGENCY_DIVERT`
- `awaitingACK == false` (no unconfirmed divert pending)

Fuel state (Normal/Low/Critical) does **not** gate the divert decision — if the aircraft
cannot reach its destination, a divert is issued immediately.

The `DIVERT_CMD` packet carries `nearestAirportID` so the client knows where to divert.

## Fuel State Thresholds
| State | Condition |
|-------|-----------|
| Normal Cruise | Fuel > 25% |
| Low Fuel Warning | Fuel ≤ 25% |
| Critical Fuel | Fuel ≤ 15% |
| Emergency Divert | `DIVERT_CMD` received from server |
| Landed Safe | Aircraft confirmed landing |

## Data Packet Structure
```c
typedef struct {
    PacketHeader header;   // packetID, type (PacketType), aircraftID, timestamp
    PacketBody   body;     // fuelLevel, flightTimeRemaining, timeToDestination,
                           // nearestAirportID, destinationAirportID, currentState, ...
} FuelPacket;
```

## Build Instructions

### Prerequisites
- `gcc` (C11 support)
- `make`
- POSIX-compatible shell (Linux / macOS) **or** Windows with Winsock2

### Build binaries
```bash
make server       # output: bin/server
make demo_client  # output: bin/demo_client
```

### Demo: interactive multi-client testing
```bash
# Terminal 1 — start server
./bin/server

# Terminal 2, 3, 4 — each client with a unique aircraftID
./bin/demo_client 101
./bin/demo_client 102
./bin/demo_client 103
```

At the prompt, enter fuel parameters to send a `FUEL_STATUS` packet and see the server response:
```
[AC-101 | NORMAL_CRUISE] fuel> <fuelLevel> <flightTimeRemaining> <timeToDestination> [nearestAirportID]
[AC-101 | NORMAL_CRUISE] fuel> land   # send LANDED_SAFE and exit
[AC-101 | NORMAL_CRUISE] fuel> quit   # disconnect and exit
```

The server handles all clients concurrently (one pthread per client) and logs every
received packet to the server terminal.

## Running Tests

Each test suite runs in isolation on its own port to avoid conflicts.

```bash
# Individual test suites
make test                    # server connection tests       (port 18080)
make test_client             # client connection tests       (port 18081)
make test_packet             # packet struct/validation
make test_logger             # logger
make test_fuel_system        # state machine logic
make test_fuel_threshold     # fuel threshold detection      (port 18080)
make test_divert_command     # divert command unit tests     (port 18080)
make test_divert_integration # end-to-end divert flow        (port 18082)

# Run all tests at once
make test_all
```

Expected output for `make test_all`:
```
=== Server Connection Tests ===   ... 21/21 passed
=== Client Connection Tests ===   ... 18/18 passed
=== Packet Tests ===              ... 32/32 passed
=== Logger Tests ===              ... 21/21 passed
=== Fuel Threshold Tests ===      ... 41/41 passed
=== Divert Command Tests ===      ... 21/21 passed
=== Divert Integration Tests ===  ... 41/41 passed
```

## Project Progress

### Phase 1 — Foundation
- [x] TCP/IP socket connection setup (server + client)
- [x] 3-way handshake verification logic
- [x] `FuelPacket` struct definition (`common/packet.h`)
- [x] Fuel state machine skeleton and threshold logic
- [x] Basic synchronous logging system (REQ-LOG-060)
- [x] Unit tests for core modules (fuel, packet, connection, logger)

### Phase 2 — Divert Decision Logic (Sprint 2)
- [x] Fuel threshold detection logic (server-side `checkFuelThresholds`)
- [x] Emergency divert command and ACK handling (`evaluateDivertDecision`, `broadcastDivertCommand`)
- [x] `DIVERT_CMD` added to `PacketType` enum — server now sends structured `FuelPacket` instead of a raw string
- [x] Server recv loop: receives `FUEL_STATUS`, evaluates divert, sends `DIVERT_CMD` or status ack
- [x] `ACK_DIVERT` handling: server clears `awaitingACK` on receipt
- [x] `LANDED_SAFE` handling: server closes session cleanly
- [x] Client recv/ACK functions: `recvServerResponse()`, `sendAckDivert()`
- [x] Client main loop: full telemetry loop with divert handling
- [x] Multi-client support: server spawns one pthread per client connection
- [x] Interactive demo client (`demo_client`) for manual multi-client testing
- [x] Divert logic: triggers on `flightTimeRemaining < timeToDestination` regardless of fuel state
- [x] End-to-end integration tests (5 scenarios, 41 assertions)

### Phase 2 — Remaining
- [x] 1 MB telemetry file transfer
- [x] Logging format finalization
- [x] UI implementation (Replaced by interactive `demo_client` terminal UI)

## Log Format
```
DateTime | TYPE | AircraftID | Details
2026-03-30 21:45:11 | PACKET_RECV | AC-101 | Fuel: 12.4% | State: CRITICAL_FUEL
```
