/*
 * ntt_log.h
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

#ifndef NTT_LOG_H
#define NTT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logging severity levels.
 */
typedef enum {
    NTT_LOG_NONE = 0,  /* logging disabled */
    NTT_LOG_ERROR = 1, /* unrecoverable failures */
    NTT_LOG_INFO = 2,  /* context lifecycle */
    NTT_LOG_DEBUG = 3, /* per-call tracing debug */
} ntt_log_level;

/**
 * @brief Sets the runtime logging threshold.
 *
 * Messages with a higher severity than the configured level are discarded,
 * so selecting a level keeps it and every level below it.
 *
 * @param[in] level Logging severity threshold.
 *
 * @note The default logging level is NTT_LOG_ERROR. Out-of-range values
 * are discarded, leaving the currently configured level unchanged.
 *
 * @note Unless this function is called, the threshold is taken from the
 * NTT_LOG_LEVEL environment variable (values: none, error, info, debug).
 * A call to ntt_log_set_level() overrides the environment variable.
 *
 * @see ntt_log_level
 */
void ntt_log_set_level(ntt_log_level level);

/**
 * Internal logging function. This function should not be called directly.
 * Use the NTT_LOG() macro instead
 */
void ntt__log_write(ntt_log_level level,
                    const char *file,
                    int line,
                    const char *func,
                    const char *fmt,
                    ...);

/**
 * NTT_LOG_ENABLED is a compile-time option (set via CMake option
 * NTT_ENABLE_LOGGING). When it's off, NTT_LOG() expands to nothing, so it
 * costs literally zero at runtime and is safe to leave scattered through the
 * code, including near hot paths, for release or benchmark builds.
 */
#if defined(NTT_LOG_ENABLED)
#define NTT_LOG(level, ...) \
    ntt__log_write((level), __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define NTT_LOG(level, ...) \
    do {                    \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* NTT_LOG_H */
