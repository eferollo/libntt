/*
 * ntt_scalar_internal.h
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

#ifndef NTT_SCALAR_INTERNAL_H
#define NTT_SCALAR_INTERNAL_H

#include "ntt_internal.h"
#include "redc/redc.h"
#include "string.h"

/**
 * @brief Reduction backend used by the optimized scalar adapter.
 *
 * Note:
 *   - Barrett mode keeps values in canonical representation. For q < 2^32 a
 *     single-word reciprocal (mu) reduces 64-bit products; for the general
 *     path (q <= 2^63 - 1) a two-word reciprocal (mu_hi/mu_lo) reduces
 *     128-bit products.
 *   - Montgomery mode stores internal values as xR mod q. With q < 2^32 the
 *     radix is R = 2^32; for the general path (q <= 2^63) the radix is
 *     R = 2^64.
 */
typedef enum {
    /* Use reciprocal-based Barrett redction. */
    NTT_SCALAR_REDUCTION_BARRETT = 0,
    /* Use Montgomery REDC for odd moduli. */
    NTT_SCALAR_REDUCTION_MONTGOMERY = 1,
} ntt_scalar_redc;

/**
 * @brief Private state owned by the optimized scalar NTT adapter.
 *
 * Stores the configured NTT parameters, reduction-specific constants, and
 * precomputed twiddle/twist tables. The representation of all precomputed
 * arithmetic tables follows @ref ntt_scalar_redc so that the transform
 * hot path can operate without conversions.
 */
typedef struct {
    uint64_t q;            /* prime modulus, q <= 2^63 */
    uint32_t n;            /* transform size */
    uint32_t stages;       /* log2(n) number of NTT stages */
    uint64_t omega;        /* primitive n-th root of unity mod q */
    uint64_t omega_inv;    /* inverse of omega mod q */
    uint64_t n_inv;        /* inverse of n mod q */
    uint64_t psi;          /* primitive 2n-th root of unity mod q */
    uint64_t psi_inv;      /* inverse primitive 2n-th root of unity mod q */
    uint64_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint64_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */
    uint32_t *bitrev;      /* bitrev[i] = bit-reversed i over log2(n) bits,
                            * precomputed once at setup so forward() or
                            * inverse() never recompute it per call. */

    ntt_scalar_redc reduction; /* selected reduction mode */

    bool q32; /* true when q < 2^32 (32-bit fast path) */

    uint64_t barrett_mu;    /* Barrett q32: floor(2^64 / q) */
    uint64_t barrett_mu_hi; /* Barrett general: floor(2^128 / q), high word */
    uint64_t barrett_mu_lo; /* Barrett general: floor(2^128 / q), low word */

    uint64_t mont_qinv; /* Montgomery: -q^(-1) mod R */
    uint64_t mont_r2;   /* Montgomery: R^2 mod q */

    uint64_t *fwd_twiddle; /* pre-computed stage forward twiddles */
    uint64_t *inv_twiddle; /* pre-computed stage inverse twiddles */
} ntt_scalar_state;

bool ntt__scalar_validate_modulus(const ntt_config *config);
void *ntt__scalar_adapter_setup(const ntt_config *config);
void ntt__scalar_adapter_teardown(void *state);

int ntt__scalar_forward(void *state, uint64_t *a);
int ntt__scalar_inverse(void *state, uint64_t *a);
int ntt__scalar_negacyclic_mul(void *state,
                               uint64_t *a,
                               uint64_t *b,
                               uint64_t *c);

uint64_t ntt__scalar_canonicalize_value(uint64_t a,
                                        const ntt_scalar_state *state);
uint64_t ntt__scalar_mul(uint64_t a, uint64_t b, const ntt_scalar_state *state);
uint64_t ntt__scalar_add(uint64_t a, uint64_t b, const ntt_scalar_state *state);
uint64_t ntt__scalar_sub(uint64_t a, uint64_t b, const ntt_scalar_state *state);
uint64_t ntt__scalar_encode_value(uint64_t a, const ntt_scalar_state *state);
uint64_t ntt__scalar_decode_value(uint64_t a, const ntt_scalar_state *state);
uint64_t
ntt__scalar_modpow(uint64_t base, uint64_t exp, const ntt_scalar_state *state);

#endif /* NTT_SCALAR_INTERNAL_H */
