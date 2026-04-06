/*
client/demo_client.c
Aircraft Fuel Monitoring System — Interactive Demo Client

Usage:
  ./bin/demo_client <aircraftID> [serverIP] [port]

  aircraftID  : integer, must be unique per running instance
  serverIP    : optional, default 127.0.0.1
  port        : optional, default 8080

At the prompt, enter fuel parameters:
  fuel> <fuelLevel> <flightTimeRemaining> <timeToDestination> [nearestAirportID]

Special commands:
  land   — send LANDED_SAFE and exit
  quit   — disconnect and exit
  help   — show this help

Example (run in 3 separate terminals):
  ./bin/demo_client 101
  ./bin/demo_client 102
  ./bin/demo_client 103
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/packet.h"
#include "include/aircraft_state_machine.h"
#include "include/client.h"

#ifndef SERVER_PORT
#define SERVER_PORT 8080
#endif
#define SERVER_ADDR "127.0.0.1"

// Helpers

static const char *stateName(FuelState s) {
  switch (s) {
  case STATE_NORMAL_CRUISE:
    return "NORMAL_CRUISE";
  case STATE_LOW_FUEL:
    return "LOW_FUEL";
  case STATE_CRITICAL_FUEL:
    return "CRITICAL_FUEL";
  case STATE_EMERGENCY_DIVERT:
    return "EMERGENCY_DIVERT";
  case STATE_LANDED_SAFE:
    return "LANDED_SAFE";
  default:
    return "UNKNOWN";
  }
}

static void printHelp(void) {
  printf("\n  Commands:\n");
  printf("    <fuelLevel> <flightTimeRemaining> <timeToDestination> "
         "[nearestAirportID]\n");
  printf("      fuelLevel           : 0.0 – 100.0  (%%)\n");
  printf("      flightTimeRemaining : minutes of fuel left\n");
  printf("      timeToDestination   : minutes to destination\n");
  printf("      nearestAirportID    : integer (default 1)\n");
  printf("\n    land  — send LANDED_SAFE and exit\n");
  printf("    quit  — disconnect and exit\n");
  printf("    help  — show this help\n\n");
  printf("  Divert rule: server issues DIVERT_CMD when\n");
  printf("    flightTimeRemaining < timeToDestination\n\n");
}

// Main

int main(int argc, char *argv[]) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return EXIT_FAILURE;
  }
#endif

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <aircraftID> [serverIP] [port]\n", argv[0]);
    return EXIT_FAILURE;
  }

  int aircraftID = atoi(argv[1]);
  if (aircraftID <= 0) {
    fprintf(stderr, "Error: aircraftID must be a positive integer\n");
    return EXIT_FAILURE;
  }

  const char *serverAddr = (argc >= 3) ? argv[2] : SERVER_ADDR;
  int port = (argc >= 4) ? atoi(argv[3]) : SERVER_PORT;

  // Connect
  printf("\n[Aircraft %d] Connecting to %s:%d ...\n", aircraftID, serverAddr,
         port);

  socket_t fd = connectToServer(serverAddr, port);
  if (fd == INVALID_SOCK) {
    fprintf(stderr, "[Aircraft %d] Connection failed.\n", aircraftID);
    return EXIT_FAILURE;
  }

  if (sendHandshake(fd, aircraftID) != 0) {
    fprintf(stderr, "[Aircraft %d] Handshake failed.\n", aircraftID);
    disconnectFromServer(fd);
    return EXIT_FAILURE;
  }

  printf("[Aircraft %d] Connected.\n", aircraftID);
  printHelp();

  // State machine
  AircraftStateMachine sm;
  smInit(&sm, aircraftID);

  int packetID = 1;

  // Interactive loop
  char line[256];
  while (1) {
    printf("[AC-%d] fuelLevel flightTimeRemaining timeToDestination "
           "[nearestAirportID] > ",
           aircraftID);
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL)
      break; // EOF

    // Strip newline
    line[strcspn(line, "\r\n")] = '\0';

    if (strlen(line) == 0)
      continue;

    // Special commands
    if (strcmp(line, "quit") == 0 || strcmp(line, "q") == 0) {
      printf("[AC-%d] Disconnecting.\n", aircraftID);
      break;
    }

    if (strcmp(line, "help") == 0) {
      printHelp();
      continue;
    }

    if (strcmp(line, "land") == 0) {
      FuelPacket pkt;
      memset(&pkt, 0, sizeof(pkt));
      pkt.header.packetID = packetID++;
      pkt.header.type = LANDED_SAFE;
      pkt.header.aircraftID = aircraftID;
      pkt.header.timestamp = time(NULL);
      pkt.body.fuelLevel = sm.fuelLevel;
      pkt.body.currentState = smGetState(&sm);

      if (sendFuelPacket(fd, &pkt) == 0)
        printf("[AC-%d] LANDED_SAFE sent. Disconnecting.\n", aircraftID);
      else
        printf("[AC-%d] Failed to send LANDED_SAFE.\n", aircraftID);
      break;
    }

    // Parse fuel parameters
    float fuelLevel, flightTimeRemaining, timeToDestination;
    int nearestAirportID = 1;

    int parsed = sscanf(line, "%f %f %f %d", &fuelLevel, &flightTimeRemaining,
                        &timeToDestination, &nearestAirportID);

    if (parsed < 3) {
      printf("  Invalid input. Type 'help' for usage.\n");
      continue;
    }

    if (fuelLevel < 0.0f || fuelLevel > 100.0f) {
      printf("  fuelLevel must be 0–100.\n");
      continue;
    }

    // Sync state machine with user-supplied fuel level
    smUpdateFuel(&sm, fuelLevel);

    // Build and send FUEL_STATUS
    FuelPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.packetID = packetID++;
    pkt.header.type = FUEL_STATUS;
    pkt.header.aircraftID = aircraftID;
    pkt.header.timestamp = time(NULL);
    pkt.body.fuelLevel = fuelLevel;
    pkt.body.flightTimeRemaining = flightTimeRemaining;
    pkt.body.timeToDestination = timeToDestination;
    pkt.body.nearestAirportID = nearestAirportID;
    pkt.body.destinationAirportID = 99;
    pkt.body.currentState = smGetState(&sm);

    printf("  → FUEL_STATUS: fuel=%.1f%% flight_rem=%.1fmin dest=%.1fmin "
           "airport=%d\n",
           fuelLevel, flightTimeRemaining, timeToDestination, nearestAirportID);

    if (sendFuelPacket(fd, &pkt) != 0) {
      printf("  Send failed — server may have disconnected.\n");
      break;
    }

    // Receive server response
    FuelPacket resp;
    if (recvServerResponse(fd, &resp) != 0) {
      printf("  No response — server disconnected.\n");
      break;
    }

    if (resp.header.type == DIVERT_CMD) {
      printf("  ← DIVERT_CMD! Nearest airport: %d\n",
             resp.body.nearestAirportID);
      smSetEmergencyDivert(&sm);

      if (sendAckDivert(fd, aircraftID) == 0)
        printf("  → ACK_DIVERT sent. State: EMERGENCY_DIVERT\n");
      else
        printf("  → Failed to send ACK_DIVERT.\n");

    } else if (resp.header.type == FUEL_STATUS) {
      printf("  ← Status ack: %s\n", stateName(resp.body.currentState));

    } else {
      printf("  ← Unexpected response type: %d\n", resp.header.type);
    }
  }

  disconnectFromServer(fd);

#ifdef _WIN32
  WSACleanup();
#endif

  return EXIT_SUCCESS;
}
