/*
 * redc.h
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

#ifndef REDC_H
#define REDC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Computes the high 64 bits of a 64-by-64-bit multiplication.
 *
 * Returns the upper half of the mathematical 128-bit product @p a*@p b using
 * only standard C11 64-bit arithmetic. The operands are decomposed into 32-bit
 * limbs, avoiding any dependency on a compiler-provided 128-bit integer type.
 *
 * @param[in] a First 64-bit operand.
 * @param[in] b Second 64-bit operand.
 *
 * @return The upper 64 bits of the mathematical product @p a*@p b.
 */
static inline uint64_t scalar_mulhi_u64(uint64_t a, uint64_t b)
{
    uint64_t a0 = (uint32_t)a;
    uint64_t a1 = a >> 32;
    uint64_t b0 = (uint32_t)b;
    uint64_t b1 = b >> 32;

    uint64_t p0 = a0 * b0;
    uint64_t p1 = a0 * b1;
    uint64_t p2 = a1 * b0;
    uint64_t p3 = a1 * b1;

    uint64_t middle = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;

    return p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
}

/* BARRETT REDUCTION
 * - single-word path (64-bit product), valid for q < 2^32.
 * - two-word path (128-bit product), valid for q <= 2^63 - 1.
 * Both are implemented without a compiler-provided 128-bit integer type.
 */

uint64_t ntt_scalar_barrett_mu(uint32_t q);
uint64_t ntt_scalar_barrett_reduce_u64(uint64_t x, uint32_t q, uint64_t mu);
uint64_t
ntt_scalar_barrett_mul(uint64_t a, uint64_t b, uint32_t q, uint64_t mu);
uint64_t
ntt_scalar_barrett_modpow(uint64_t base, uint64_t exp, uint32_t q, uint64_t mu);

/* Two-word (general-path) Barrett, for q <= 2^63 - 1. */

void ntt_scalar_barrett_mu128(uint64_t q, uint64_t *mu_hi, uint64_t *mu_lo);
uint64_t ntt_scalar_barrett_reduce_u128(uint64_t x_hi,
                                        uint64_t x_lo,
                                        uint64_t q,
                                        uint64_t mu_hi,
                                        uint64_t mu_lo);
uint64_t ntt_scalar_barrett_mul_u128(uint64_t a,
                                     uint64_t b,
                                     uint64_t q,
                                     uint64_t mu_hi,
                                     uint64_t mu_lo);
uint64_t ntt_scalar_barrett_modpow_u128(uint64_t base,
                                        uint64_t exp,
                                        uint64_t q,
                                        uint64_t mu_hi,
                                        uint64_t mu_lo);

/* MONTGOMERY REDUCTION (R = 2^32 when q32, R = 2^64 otherwise) */

uint64_t ntt_scalar_mont_qinv(uint64_t q, bool q32);
uint64_t ntt_scalar_mont_r2(uint64_t q, bool q32);
uint64_t ntt_scalar_mont_reduce(uint64_t thi,
                                uint64_t tlo,
                                uint64_t q,
                                uint64_t qinv,
                                bool q32);
uint64_t ntt_scalar_mont_mul(uint64_t a,
                             uint64_t b,
                             uint64_t q,
                             uint64_t qinv,
                             bool q32);
uint64_t ntt_scalar_mont_encode(uint64_t a,
                                uint64_t q,
                                uint64_t qinv,
                                uint64_t r2,
                                bool q32);
uint64_t
ntt_scalar_mont_decode(uint64_t a, uint64_t q, uint64_t qinv, bool q32);
uint64_t ntt_scalar_mont_modpow(uint64_t base,
                                uint64_t exp,
                                uint64_t q,
                                uint64_t qinv,
                                uint64_t r2,
                                bool q32);

#endif /* REDC_H */
