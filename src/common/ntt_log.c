/*
 * ntt_log.c
 * This file is part of the NTT Library.
 *
 * Copyright 2026 Francesco Rollo <eferollo@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _POSIX_C_SOURCE 200809L

#include "ntt_internal.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * Only the level flag needs thread-safety here. Concurrent
 * fprintf() calls from multiple threads is already line-buffered
 * safely by the C library on POSIX.
 */
static _Atomic ntt_log_level g_ntt_log_level = NTT_LOG_ERROR;

void ntt_log_set_level(ntt_log_level level)
{
    switch(level) {
        case NTT_LOG_NONE:
        case NTT_LOG_ERROR:
        case NTT_LOG_INFO:
        case NTT_LOG_DEBUG:
            break;
        default:
            return;
    }
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

/**
 * @brief Returns the final path component of @p path (file name only).
 */
static const char *file_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    return (slash != NULL) ? slash + 1 : path;
}

void ntt__log_write(ntt_log_level level,
                    const char *file,
                    int line,
                    const char *func,
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

    /* Log timestamp, severity, source file, and function. */
    fprintf(stderr,
            "[%s.%03ld][%-5s] %s (%s:%d): ",
            timebuf,
            ts.tv_nsec / 1000000L,
            level_str(level),
            func,
            file_basename(file),
            line);

    /* Print the user-provided printf-style log message. */
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
