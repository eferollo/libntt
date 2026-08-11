/*
 * ntt_internal.h
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

#ifndef NTT_INTERNAL_H
#define NTT_INTERNAL_H

#include "adapter_internal.h"
#include "config_internal.h"
#include "ntt/ntt.h"
#include "ntt/ntt_utils.h"
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Internal representation of an NTT context.
 *
 * The context contains only state that is common to every backend. All NTT
 * parameters, arithmetic state, lookup tables, and implementation-specific
 * data are owned by the selected backend and stored behind the opaque state
 * pointer.
 */
struct ntt_ctx_s {
    uint64_t q;                 /* prime modulus */
    uint32_t n;                 /* transform size, must be a power of 2 */
    const ntt_adapter *adapter; /* selected NTT adapter */
    void *state;                /* opaque backend-specific state */
};

bool ntt__validate_transform_params(uint64_t q,
                                    uint32_t n,
                                    ntt_transform_type type);
bool ntt__resolve_roots(uint64_t q,
                        uint32_t n,
                        ntt_transform_type type,
                        uint64_t *omega,
                        uint64_t *psi);
bool ntt__is_primitive_root_of_order(uint64_t x, uint32_t order, uint64_t q);
bool ntt__distinct_prime_factors(uint64_t x,
                                 uint64_t *factors,
                                 size_t max_factors,
                                 size_t *count);

#define SAFE_FREE(ptr)       \
    do {                     \
        if ((ptr) != NULL) { \
            free(ptr);       \
            ptr = NULL;      \
        }                    \
    } while (0)

#define ZERO_STRUCTP(x)                   \
    do {                                  \
        if ((x) != NULL)                  \
            memset((x), 0, sizeof(*(x))); \
    } while (0)

#endif /* NTT_INTERNAL_H */
