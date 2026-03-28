/*
 * common/logger.c
 * Aircraft Fuel Monitoring System — Logger
 *
 * Implements:
 *   logInit()  — open/create the .log file
 *   logWrite() — REQ-LOG-060: write formatted entry to file and stderr
 *   logClose() — close the log file
 *
 * Log entry format (REQ-LOG-060):
 *   DateTime | TYPE    | AircraftID | Details
 *   e.g.: 2026-03-27 14:05:00 | INFO    | AC-1  | Fuel status received
 */

#include "logger.h"

#include <stdio.h>
#include <time.h>

static FILE *logFile = NULL;

// logInit — open (or create) the log file for appending.
// Returns 0 on success, -1 on failure.
int logInit(const char *filename) {
    logFile = fopen(filename, "a");
    if (logFile == NULL) return -1;
    setvbuf(logFile, NULL, _IONBF, 0); // write immediately, no buffering
    return 0;
}

// logWrite — REQ-LOG-060
// Writes to both the log file and stderr so nothing is lost.
void logWrite(int aircraftID, LogLevel level, const char *details) {
    time_t now = time(NULL);
    char timeBuf[20];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    const char *levelStr;
    switch (level) {
        case LOG_LEVEL_INFO:    levelStr = "INFO   "; break;
        case LOG_LEVEL_WARNING: levelStr = "WARNING"; break;
        case LOG_LEVEL_ERROR:   levelStr = "ERROR  "; break;
        default:                levelStr = "UNKNOWN"; break;
    }

    // Write to file if initialised
    if (logFile != NULL) {
        fprintf(logFile, "%s | %s | AC-%-3d | %s\n",
                timeBuf, levelStr, aircraftID, details);
    }

    // Always mirror to stderr so the console still shows output
    fprintf(stderr, "%s | %s | AC-%-3d | %s\n",
            timeBuf, levelStr, aircraftID, details);
}

// logClose — flush and close the log file.
void logClose(void) {
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
}
