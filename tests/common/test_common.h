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

#include <stdlib.h>
#include <string.h>

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

#ifdef __cplusplus
}
#endif

#endif /* NTT_TEST_COMMON_H */
