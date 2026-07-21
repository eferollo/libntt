#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include "ntt/ntt_log.h"

/**
 * Only the level flag needs thread-safety here. Concurrent
 * fprintf() calls from multiple threads is already line-buffered
 * safely by the C library on POSIX.
 */
static _Atomic ntt_log_level g_ntt_log_level = NTT_LOG_ERROR;

void ntt_log_set_level(ntt_log_level level)
{
    atomic_store(&g_ntt_log_level, level);
}

static const char *level_str(ntt_log_level level)
{
    switch (level) {
    case NTT_LOG_ERROR:
        return "ERROR";
    case NTT_LOG_INFO:
        return "INFO";
    case NTT_LOG_DEBUG:
        return "DEBUG";
    default:
        return "?";
    }
}

void ntt__log_write(ntt_log_level level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...)
{
    if (level == NTT_LOG_NONE || level > atomic_load(&g_ntt_log_level)) {
        return;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    /* Convert the timestamp to local time. */
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char timebuf[16] = {0};
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    /* Log timestamp, severity, source file, and line number */
    fprintf(stderr,
            "[%s.%03ld][%-5s] %s:%d: ",
            timebuf,
            ts.tv_nsec / 1000000L,
            level_str(level),
            file,
            line);

    /* Print the user-provided printf-style log message. */
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
