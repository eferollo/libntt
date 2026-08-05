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
 * @brief Computes the negative modular inverse of an odd modulus modulo
 *        2^32 using Newton iteration.
 *
 * The value is required by Montgomery REDC and is valid only for odd moduli,
 * for which the inverse modulo @f$2^{32}@f$ exists.
 *
 * @param[in] q Odd modulus.
 *
 * @return @f$-q^{-1}\bmod2^{32}@f$.
 */
static uint32_t mont_qinv32(uint32_t q)
{
    /* Newton iteration for q^(-1) modulo 2^32, then negate. */
    uint32_t x = q;
    x *= 2u - q * x;
    x *= 2u - q * x;
    x *= 2u - q * x;
    x *= 2u - q * x;
    x *= 2u - q * x;
    return 0u - x;
}

/**
 * @brief Computes the negative modular inverse of an odd modulus modulo
 *        2^64 using Newton iteration.
 *
 * The value is required by Montgomery REDC and is valid only for odd moduli,
 * for which the inverse modulo @f$2^{64}@f$ exists.
 *
 * @param[in] q Odd modulus.
 *
 * @return @f$-q^{-1}\bmod2^{64}@f$.
 */
static uint64_t mont_qinv64(uint64_t q)
{
    /* Newton iteration for q^(-1) modulo 2^64, then negate. */
    uint64_t x = q;
    x *= 2ull - q * x;
    x *= 2ull - q * x;
    x *= 2ull - q * x;
    x *= 2ull - q * x;
    x *= 2ull - q * x;
    x *= 2ull - q * x;
    return 0ull - x;
}

/**
 * @brief Computes the Montgomery negative modular inverse of the modulus.
 *
 * Dispatches between the 32-bit and 64-bit variants depending on @p q32.
 *
 * @param[in] q   Odd Montgomery modulus.
 * @param[in] q32 true for moduli smaller than 2^32 (R = 2^32), false for
 *                moduli up to 2^63 (R = 2^64).
 *
 * @return The negative modular inverse used by Montgomery REDC.
 */
uint64_t ntt_scalar_mont_qinv(uint64_t q, bool q32)
{
    if (q32) {
        return (uint32_t)mont_qinv32((uint32_t)q);
    }
    return mont_qinv64(q);
}

/**
 * @brief Computes @f$2^{k}\bmod q@f$ by repeated doubling.
 *
 * Repeated doubling computes the power without integer division or the modulo
 * operator, which keeps the constant precomputation free of 64-bit divisions.
 *
 * @param[in] q    Odd modulus.
 * @param[in] bits Number of doublings (@p R^2 with @f$R=2^{bits/2}@f$).
 *
 * @return @f$2^{bits}\bmod q@f$.
 */
static uint64_t mont_r2_double(uint64_t q, unsigned int bits)
{
    uint64_t r2 = 1;
    for (unsigned int i = 0; i < bits; i++) {
        /*
         * Each step computes r2 = 2*r2 mod q without overflow: since r2 < q
         * and q <= 2^63, 2*r2 fits in 64 bits.
         */
        uint64_t doubled = r2 << 1;
        r2 = doubled >= q ? doubled - q : doubled;
    }
    return r2;
}

/**
 * @brief Computes the Montgomery constant @f$R^2\bmod q@f$.
 *
 * For the 32-bit path @f$R=2^{32}@f$ and @p r2 = @f$2^{64}\bmod q@f$; for the
 * 64-bit path @f$R=2^{64}@f$ and @p r2 = @f$2^{128}\bmod q@f$. The result is
 * used by ntt_scalar_mont_encode() to convert canonical values into
 * Montgomery representation.
 *
 * @param[in] q   Odd Montgomery modulus.
 * @param[in] q32 Selects the 32-bit or 64-bit Montgomery radix.
 *
 * @return @f$R^2\bmod q@f$.
 */
uint64_t ntt_scalar_mont_r2(uint64_t q, bool q32)
{
    return q32 ? mont_r2_double(q, 64u) : mont_r2_double(q, 128u);
}

/**
 * @brief Montgomery REDC with radix @f$R=2^{32}@f$.
 *
 * Given @p t, computes the Montgomery reduction
 * @f$t\cdot R^{-1}\bmod q@f$. The implementation uses the precomputed
 * @f$q_{inv}=-q^{-1}\bmod R@f$ and explicitly tracks the carry of the 64-bit
 * addition so that the result is correct across the full supported 32-bit
 * modulus range.
 *
 * @param[in] t    64-bit value to reduce.
 * @param[in] q    Odd modulus smaller than 2^32.
 * @param[in] qinv @f$-q^{-1}\bmod 2^{32}@f$.
 *
 * @return @f$t\cdot R^{-1}\bmod q@f$.
 */
static uint32_t mont_reduce_32(uint64_t t, uint32_t q, uint32_t qinv)
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
 * @brief Montgomery REDC with radix @f$R=2^{64}@f$.
 *
 * Reduces the 128-bit value @f$t = (thi \ll 64) + tlo@f$ by a single pass
 * using @f$m = tlo \cdot q_{inv} \bmod 2^{64}@f$. Because the general path is
 * restricted to @f$q \le 2^{63}@f$, the result is smaller than @f$2q \le
 * 2^{64}@f$ and a single conditional subtraction yields the canonical residue.
 *
 * @param[in] thi   High 64 bits of the value to reduce.
 * @param[in] tlo   Low 64 bits of the value to reduce.
 * @param[in] q     Odd modulus up to 2^63.
 * @param[in] qinv @f$-q^{-1}\bmod 2^{64}@f$.
 *
 * @return @f$t\cdot R^{-1}\bmod q@f$ in @f$[0,q)@f$.
 */
static uint64_t
mont_reduce_64(uint64_t thi, uint64_t tlo, uint64_t q, uint64_t qinv)
{
    uint64_t m = tlo * qinv;
    uint64_t mhi = scalar_mulhi_u64(m, q);
    uint64_t mlo = m * q;

    uint64_t lo = tlo + mlo;
    uint64_t carry = (lo < mlo);
    uint64_t hi = thi + mhi + carry;

    if (hi >= q) {
        hi -= q;
    }
    return hi;
}

/**
 * @brief Performs Montgomery REDC on a value.
 *
 * @param[in] thi   High 64 bits of the value to reduce (0 for the 32-bit path).
 * @param[in] tlo   Low 64 bits of the value to reduce.
 * @param[in] q     Odd Montgomery modulus.
 * @param[in] qinv  Precomputed negative modular inverse of q modulo R.
 * @param[in] q32   true for R = 2^32, false for R = 2^64.
 *
 * @return Canonical Montgomery-reduced value.
 */
uint64_t ntt_scalar_mont_reduce(uint64_t thi,
                                uint64_t tlo,
                                uint64_t q,
                                uint64_t qinv,
                                bool q32)
{
    if (q32) {
        return mont_reduce_32(tlo, (uint32_t)q, (uint32_t)qinv);
    }
    return mont_reduce_64(thi, tlo, q, qinv);
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
 * @param[in] q32  true for R = 2^32, false for R = 2^64.
 *
 * @return Montgomery representation of the product.
 */
uint64_t
ntt_scalar_mont_mul(uint64_t a, uint64_t b, uint64_t q, uint64_t qinv, bool q32)
{
    if (q32) {
        return mont_reduce_32((uint64_t)(uint32_t)a * (uint32_t)b,
                              (uint32_t)q,
                              (uint32_t)qinv);
    }
    return mont_reduce_64(scalar_mulhi_u64(a, b), a * b, q, qinv);
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
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 * @param[in] r2   Precomputed Montgomery constant R^2 mod q.
 * @param[in] q32  true for R = 2^32, false for R = 2^64.
 *
 * @return Montgomery representation of @p a.
 */
uint64_t ntt_scalar_mont_encode(uint64_t a,
                                uint64_t q,
                                uint64_t qinv,
                                uint64_t r2,
                                bool q32)
{
    return ntt_scalar_mont_mul(a, r2, q, qinv, q32);
}

/**
 * @brief Converts a Montgomery residue back to canonical representation.
 *
 * Applies one Montgomery REDC operation to compute @f$aR^{-1}\bmod q@f$.
 *
 * @param[in] a    Montgomery-domain value.
 * @param[in] q    Odd Montgomery modulus.
 * @param[in] qinv Precomputed negative modular inverse of q modulo R.
 * @param[in] q32  true for R = 2^32, false for R = 2^64.
 *
 * @return Canonical representation of the value.
 */
uint64_t ntt_scalar_mont_decode(uint64_t a,
                                uint64_t q,
                                uint64_t qinv,
                                bool q32)
{
    return ntt_scalar_mont_reduce(0ull, a, q, qinv, q32);
}

/**
 * @brief Computes modular exponentiation using Montgomery arithmetic.
 *
 * Evaluates @f$base^{exp}\bmod q@f$ with binary exponentiation. The base and
 * multiplicative identity are encoded into Montgomery form, all intermediate
 * products use Montgomery multiplication, and the final result is decoded.
 *
 * @param[in] base  Base value in canonical representation.
 * @param[in] exp   Non-negative exponent.
 * @param[in] q     Odd Montgomery modulus.
 * @param[in] qinv  Precomputed negative modular inverse of q modulo R.
 * @param[in] r2    Precomputed Montgomery constant R^2 mod q.
 * @param[in] q32   true for R = 2^32, false for R = 2^64.
 *
 * @return Canonical residue @f$base^{exp}\bmod q@f$.
 */
uint64_t ntt_scalar_mont_modpow(uint64_t base,
                                uint64_t exp,
                                uint64_t q,
                                uint64_t qinv,
                                uint64_t r2,
                                bool q32)
{
    uint64_t one = ntt_scalar_mont_encode(1, q, qinv, r2, q32);
    uint64_t result = one;

    base = ntt_scalar_mont_encode(base, q, qinv, r2, q32);

    while (exp != 0) {
        if (exp & 1u) {
            result = ntt_scalar_mont_mul(result, base, q, qinv, q32);
        }
        base = ntt_scalar_mont_mul(base, base, q, qinv, q32);
        exp >>= 1;
    }

    return ntt_scalar_mont_decode(result, q, qinv, q32);
}
