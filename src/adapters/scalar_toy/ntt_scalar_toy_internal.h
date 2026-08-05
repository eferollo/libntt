/*
 * ntt_scalar_toy_internal.h
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

#ifndef NTT_SCALAR_TOY_INTERNAL_H
#define NTT_SCALAR_TOY_INTERNAL_H

#include "ntt_internal.h"

/**
 * @brief Private state owned by the scalar toy NTT adapter.
 *
 * The common NTT context intentionally does not expose these fields. This
 * adapter uses canonical uint64_t coefficients in [0, q) and computes all
 * modular arithmetic with generic scalar operations.
 */
typedef struct {
    uint64_t q;            /* prime modulus */
    uint32_t n;            /* transform size */
    uint32_t stages;       /* log2(n) number of NTT stages */
    uint64_t omega;        /* primitive n-th root of unity mod q */
    uint64_t omega_inv;    /* inverse of omega mod q */
    uint64_t n_inv;        /* inverse of n mod q */
    uint64_t psi;          /* primitive 2n-th root of unity mod q */
    uint64_t psi_inv;      /* inverse primitive 2n-th root of unity mod q */
    uint64_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint64_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */
} ntt_scalar_toy_state;

uint64_t ntt__reduce(uint64_t a, uint64_t q);
uint64_t ntt__addmod(uint64_t a, uint64_t b, uint64_t q);
uint64_t ntt__submod(uint64_t a, uint64_t b, uint64_t q);
uint64_t ntt__mulmod(uint64_t a, uint64_t b, uint64_t q);
uint64_t ntt__modpow(uint64_t base, uint64_t exp, uint64_t q);
uint64_t ntt__modinv(uint64_t a, uint64_t q);

bool ntt__validate_modulus(const ntt_config *config);
void *ntt__adapter_setup(const ntt_config *config);
void ntt__adapter_teardown(void *state);

int ntt__forward(void *state, uint64_t *a);
int ntt__inverse(void *state, uint64_t *a);
int ntt__negacyclic_mul(void *state, uint64_t *a, uint64_t *b, uint64_t *c);

#endif /* NTT_SCALAR_TOY_INTERNAL_H */
