#ifndef LOGGER_H
#define LOGGER_H

// REQ-LOG-060: All log entries follow the format:
// DateTime | TYPE    | AircraftID | Details

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
} LogLevel;

// Open/create the log file for appending. Call once at startup.
// Returns 0 on success, -1 on failure.
int logInit(const char *filename);

// Write a formatted log entry to the file and stderr.
void logWrite(int aircraftID, LogLevel level, const char *details);

// Flush and close the log file. Call at shutdown.
void logClose(void);

// Convenience macros
#define LOG_INFO(aircraftID, details)    logWrite((aircraftID), LOG_LEVEL_INFO,    (details))
#define LOG_WARNING(aircraftID, details) logWrite((aircraftID), LOG_LEVEL_WARNING, (details))
#define LOG_ERROR(aircraftID, details)   logWrite((aircraftID), LOG_LEVEL_ERROR,   (details))

#endif // LOGGER_H
