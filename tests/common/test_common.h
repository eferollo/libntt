/*
 * test_common.h
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

#ifndef NTT_TEST_COMMON_H
#define NTT_TEST_COMMON_H

#include "ntt/ntt_log.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures the logging threshold from the NTT_LOG_LEVEL environment
 *        variable.
 *
 * Many tests deliberately trigger error paths, so logging is silenced by
 * default. Set NTT_LOG_LEVEL=none|error|info|debug to override, e.g.,
 *
 *     NTT_LOG_LEVEL=debug ./<test_binary>
 *
 * @note This is a no-op for test binaries compiled without NTT_LOG_ENABLED,
 *       since NTT_LOG() expands to nothing there.
 */
static inline void ntt_test_set_log_level(void)
{
    ntt_log_set_level(NTT_LOG_NONE);
    const char *level = getenv("NTT_LOG_LEVEL");
    if (level != NULL) {
        if (strcmp(level, "error") == 0) {
            ntt_log_set_level(NTT_LOG_ERROR);
        } else if (strcmp(level, "info") == 0) {
            ntt_log_set_level(NTT_LOG_INFO);
        } else if (strcmp(level, "debug") == 0) {
            ntt_log_set_level(NTT_LOG_DEBUG);
        }
    }
}

/*
 * Splitmix64 deterministic PRNG shared by the differential tests. Each test
 * binary owns its seed as a per-file uint64_t state so it can reseed from the
 * CSPRNG in stress mode; these helpers only advance and draw from that state.
 */
static inline uint64_t ntt_test_prng_next_u64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/** @brief Returns a value strictly smaller than @p bound. */
static inline uint64_t ntt_test_prng_range(uint64_t *state, uint64_t bound)
{
    return ntt_test_prng_next_u64(state) % bound;
}

/** @brief Returns the low 32 bits of a PRNG value. */
static inline uint32_t ntt_test_prng_next_u32(uint64_t *state)
{
    return (uint32_t)ntt_test_prng_next_u64(state);
}

/**
 * @brief Sets or removes an environment variable for the duration of a test.
 *
 * Portable wrapper around setenv()/unsetenv() on Windows. Implemented in
 * test_common.c, where the feature-test macros for the POSIX names are
 * defined. Return values are deliberately ignored.
 *
 * @param[in] name  Variable name.
 * @param[in] value NULL to remove the variable, otherwise its new value.
 */
void test_set_env(const char *name, const char *value);

/** @brief Removes @p name from the environment.
 *
 * @param[in] name  Variable name.
 */
void test_unset_env(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NTT_TEST_COMMON_H */
