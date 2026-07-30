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
 * adapter uses canonical uint32_t coefficients in [0, q) and computes all
 * modular arithmetic with generic scalar operations.
 */
typedef struct {
    uint32_t q;            /* prime modulus */
    uint32_t n;            /* transform size */
    uint32_t stages;       /* log2(n) number of NTT stages */
    uint32_t omega;        /* primitive n-th root of unity mod q */
    uint32_t omega_inv;    /* inverse of omega mod q */
    uint32_t n_inv;        /* inverse of n mod q */
    uint32_t psi;          /* primitive 2n-th root of unity mod q */
    uint32_t psi_inv;      /* inverse primitive 2n-th root of unity mod q */
    uint32_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint32_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */
} ntt_scalar_toy_state;

uint32_t ntt__reduce(uint32_t a, uint32_t q);
uint32_t ntt__addmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__submod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__mulmod(uint32_t a, uint32_t b, uint32_t q);
uint32_t ntt__modpow(uint32_t base, uint32_t exp, uint32_t q);
uint32_t ntt__modinv(uint32_t a, uint32_t q);

bool ntt__validate_modulus(const ntt_config *config);
void *ntt__adapter_setup(const ntt_config *config);
void ntt__adapter_teardown(void *state);

int ntt__forward(void *state, uint32_t *a);
int ntt__inverse(void *state, uint32_t *a);
int ntt__negacyclic_mul(void *state, uint32_t *a, uint32_t *b, uint32_t *c);

#endif /* NTT_SCALAR_TOY_INTERNAL_H */
