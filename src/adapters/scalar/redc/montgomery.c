/*
 * montgomery.c
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

#include "redc.h"

/**
 * @brief Computes the Montgomery negative modular inverse of the modulus.
 *
 * Computes @f$q_{inv}=-q^{-1}\bmod2^{32}@f$ using Newton iteration. The value
 * is required by Montgomery REDC and is valid only for odd moduli, for which
 * the inverse modulo @f$2^{32}@f$ exists.
 *
 * @param[in] q Odd Montgomery modulus.
 *
 * @return @f$-q^{-1}\bmod2^{32}@f$.
 */
uint32_t ntt_scalar_mont_qinv(uint32_t q)
{
    /* Newton iteration for q^(-1) modulo 2^32, then negate. */
    uint32_t x = q;
    x *= 2u - q * x;
    x *= 2u - q * x;
    x *= 2u - q * x;
    x *= 2u - q * x;
    return 0u - x;
}

/**
 * @brief Performs Montgomery REDC on a 64-bit product.
 *
 * Given @p t, computes the Montgomery reduction
 * @f$T\cdot R^{-1}\bmod q@f$ with @f$R=2^{32}@f$. The implementation uses the
 * precomputed @f$q_{inv}=-q^{-1}\bmod R@f$ and explicitly tracks the carry of
 * the 64-bit addition so that the result is correct across the full supported
 * 32-bit modulus range.
 *
 * @param[in] t    64-bit Montgomery product or other valid REDC input.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 *
 * @return Canonical Montgomery-reduced value.
 */
uint32_t ntt_scalar_mont_reduce(uint64_t t, uint32_t q, uint32_t qinv)
{
    uint32_t m = (uint32_t)t * qinv;
    uint64_t mq = (uint64_t)m * q;
    uint64_t sum = t + mq;
    uint64_t carry = (sum < t);
    uint64_t u = (sum >> 32) + (carry << 32);

    if (u >= q) {
        u -= q;
    }
    return (uint32_t)u;
}

/**
 * @brief Multiplies two Montgomery-domain residues.
 *
 * Computes @f$a\cdot b\cdot R^{-1}\bmod q@f$ using Montgomery REDC. When both
 * operands represent @f$xR@f$ and @f$yR@f$, respectively, the result again
 * represents @f$xyR@f$, allowing an entire NTT to remain in Montgomery form.
 *
 * @param[in] a    First Montgomery residue.
 * @param[in] b    Second Montgomery residue.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 *
 * @return Montgomery representation of the product.
 */
uint32_t ntt_scalar_mont_mul(uint32_t a, uint32_t b, uint32_t q, uint32_t qinv)
{
    return ntt_scalar_mont_reduce((uint64_t)a * b, q, qinv);
}

/**
 * @brief Converts a canonical residue into Montgomery representation.
 *
 * Computes @f$aR\bmod q@f$ by multiplying @p a by the precomputed
 * @f$R^2\bmod q@f$. The conversion is performed once at the boundary of a
 * transform or when precomputing constants. Subsequent arithmetic stays in
 * Montgomery form.
 *
 * @param[in] a    Canonical input residue.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] r2   Precomputed Montgomery constant R^2 mod q.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 *
 * @return Montgomery representation of @p a.
 */
uint32_t
ntt_scalar_mont_encode(uint32_t a, uint32_t q, uint32_t r2, uint32_t qinv)
{
    return ntt_scalar_mont_mul(a, r2, q, qinv);
}

/**
 * @brief Converts a Montgomery residue back to canonical representation.
 *
 * Applies one Montgomery REDC operation to compute @f$aR^{-1}\bmod q@f$.
 *
 * @param[in] a    Montgomery-domain value.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 *
 * @return Canonical representation of the value.
 */
uint32_t ntt_scalar_mont_decode(uint32_t a, uint32_t q, uint32_t qinv)
{
    return ntt_scalar_mont_reduce(a, q, qinv);
}

/**
 * @brief Computes modular exponentiation using Montgomery arithmetic.
 *
 * Evaluates @f$base^{exp}\bmod q@f$ with binary exponentiation. The base and
 * multiplicative identity are encoded into Montgomery form, all intermediate
 * products use Montgomery multiplication, and the final result is decoded.
 *
 * @param[in] base Base value in canonical representation.
 * @param[in] exp  Non-negative exponent.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] r2   Precomputed Montgomery constant R^2 mod q.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 *
 * @return Canonical residue @f$base^{exp}\bmod q@f$.
 */
uint32_t ntt_scalar_mont_modpow(uint32_t base,
                                uint32_t exp,
                                uint32_t q,
                                uint32_t r2,
                                uint32_t qinv)
{
    uint32_t one = ntt_scalar_mont_encode(1, q, r2, qinv);
    uint32_t result = one;

    base = ntt_scalar_mont_encode(base, q, r2, qinv);

    while (exp != 0) {
        if (exp & 1u) {
            result = ntt_scalar_mont_mul(result, base, q, qinv);
        }
        base = ntt_scalar_mont_mul(base, base, q, qinv);
        exp >>= 1;
    }

    return ntt_scalar_mont_decode(result, q, qinv);
}

/**
 * @brief Computes the Montgomery constant @f$R^2\bmod q@f$.
 *
 * Uses @f$R=2^{32}@f$ and repeated doubling to compute
 * @f$2^{64}\bmod q@f$ without integer division or the modulo operator.
 * The result is used by ntt_scalar_mont_encode() to convert canonical values
 * into Montgomery representation.
 *
 * @param[in] q Odd Montgomery modulus.
 *
 * @return @f$R^2\bmod q@f$.
 */
uint32_t ntt_scalar_mont_r2(uint32_t q)
{
    uint32_t r2 = 1;

    for (unsigned i = 0; i < 64; i++) {
        uint64_t doubled = (uint64_t)r2 << 1;
        r2 = doubled >= q ? (uint32_t)(doubled - q) : (uint32_t)doubled;
    }

    return r2;
}
