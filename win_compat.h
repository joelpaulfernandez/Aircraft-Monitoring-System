/*
 * win_compat.h — Windows build compatibility shims.
 * Injected via -include during compilation; no source files are modified.
 */
#ifndef WIN_COMPAT_H
#define WIN_COMPAT_H

#ifdef _WIN32
#include <time.h>

/* localtime_r is POSIX-only; provide a thin wrapper around Windows localtime_s */
static inline struct tm *localtime_r(const time_t *timep, struct tm *result) {
    return (localtime_s(result, timep) == 0) ? result : NULL;
}
#endif /* _WIN32 */

#endif /* WIN_COMPAT_H */
