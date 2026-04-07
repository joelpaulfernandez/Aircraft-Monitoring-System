/*
common/logger.c
Aircraft Fuel Monitoring System — Logger

Implements: logInit, logWrite, logClose
Log format (REQ-LOG-060): DateTime | TYPE    | AircraftID | Details
*/

#include "logger.h"

#include <stdio.h>
#include <time.h>

static FILE *logFile = NULL;

// logInit — open the log file for appending; closes any previously open handle.
// Returns 0 on success, -1 on failure.
int logInit(const char *filename) {
    if (filename == NULL) return -1;
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
    logFile = fopen(filename, "a");
    if (logFile == NULL) return -1;
    setvbuf(logFile, NULL, _IONBF, 0); // write immediately, no buffering
    return 0;
}

// logWrite — REQ-LOG-060
// Write a formatted entry to the log file and mirror it to stderr.
void logWrite(int aircraftID, LogLevel level, const char *details) {
    if (details == NULL) details = "(null)";

    time_t now = time(NULL);
    char timeBuf[20];
    struct tm tmBuf;
#ifdef _WIN32
    struct tm *tm = (localtime_s(&tmBuf, &now) == 0) ? &tmBuf : NULL;
#else
    struct tm *tm = localtime_r(&now, &tmBuf);
#endif
    if (tm != NULL) {
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "0000-00-00 00:00:00");
    }

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
