/*
 * barrett.c
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
 * @brief Reduces a bounded 64-bit integer using Barrett reduction.
 *
 * Uses the precomputed reciprocal
 * @f$\mu = \lfloor 2^{64}/q \rfloor@f$ to estimate the quotient without
 * hardware integer division. For this scalar backend the function is used
 * with @p x < q^2, so the quotient estimate is sufficiently accurate that a
 * single final conditional subtraction produces a canonical remainder.
 *
 * This single-word reciprocal only holds when @f$q^2 < 2^{64}@f$, i.e. for
 * moduli smaller than @f$2^{32}@f$. This is the "32-bit fast path" used for
 * post-quantum parameter sets; larger moduli are routed to Montgomery
 * reduction with @f$R = 2^{64}@f$.
 *
 * @param[in] x  Value to reduce, satisfying @p x < @p q squared.
 * @param[in] q  Prime modulus smaller than @f$2^{32}@f$.
 * @param[in] mu Precomputed Barrett reciprocal
 *               @f$\mu = \lfloor 2^{64}/q \rfloor@f$.
 *
 * @return @p x modulo @p q in canonical representation.
 */
uint64_t ntt_scalar_barrett_reduce_u64(uint64_t x, uint32_t q, uint64_t mu)
{
    uint64_t qhat = scalar_mulhi_u64(x, mu);
    uint64_t r = x - qhat * q;

    if (r >= q) {
        r -= q;
    }

    return r;
}

/**
 * @brief Multiplies two canonical residues using Barrett reduction.
 *
 * Forms the full 64-bit product of @p a and @p b and reduces it through
 * ntt_scalar_barrett_reduce_u64(). No integer division or modulo operator is
 * executed on this performance-critical multiplication path. Both operands
 * are canonical residues modulo @p q with @p q < 2^32, so the product fits in
 * 64 bits.
 *
 * @param a  First canonical residue.
 * @param b  Second canonical residue.
 * @param q  Modulus smaller than @f$2^{32}@f$.
 * @param mu Precomputed Barrett reciprocal.
 *
 * @return The canonical residue @f$(a\cdot b)\bmod q@f$.
 */
uint64_t ntt_scalar_barrett_mul(uint64_t a, uint64_t b, uint32_t q, uint64_t mu)
{
    return ntt_scalar_barrett_reduce_u64(a * b, q, mu);
}

/**
 * @brief Computes modular exponentiation using Barrett multiplication.
 *
 * Evaluates @f$base^{exp}\bmod q@f$ with binary exponentiation. Every modular
 * multiplication is routed through Barrett reduction, making this routine
 * suitable for setup-time computation of roots and inverse constants without
 * using the modulo operator in the arithmetic implementation.
 *
 * @param base Base value.
 * @param exp  Non-negative exponent.
 * @param q    Modulus smaller than @f$2^{32}@f$.
 * @param mu   Precomputed Barrett reciprocal.
 *
 * @return The canonical residue @f$base^{exp}\bmod q@f$.
 */
uint64_t
ntt_scalar_barrett_modpow(uint64_t base, uint64_t exp, uint32_t q, uint64_t mu)
{
    uint64_t result = 1;
    base = ntt_scalar_barrett_reduce_u64(base, q, mu);

    while (exp != 0) {
        if (exp & 1u) {
            result = ntt_scalar_barrett_mul(result, base, q, mu);
        }
        base = ntt_scalar_barrett_mul(base, base, q, mu);
        exp >>= 1;
    }
    return result;
}

/**
 * @brief Computes the Barrett reciprocal used by the scalar backend.
 *
 * Computes @f$\mu = \lfloor 2^{64}/q \rfloor@f$ without requiring a native
 * 128-bit integer type. Since the scalar adapter requires an odd prime
 * modulus greater than two, @p q does not divide @f$2^{64}@f$ and the
 * reciprocal can be derived from the quotient and remainder of
 * @f$UINT64_MAX/q@f$.
 *
 * @param[in] q Prime modulus greater than two and smaller than @f$2^{32}@f$.
 *
 * @return Barrett reciprocal @f$\mu = \lfloor 2^{64}/q \rfloor@f$.
 */
uint64_t ntt_scalar_barrett_mu(uint32_t q)
{
    uint64_t quotient = UINT64_MAX / q;
    uint64_t remainder = UINT64_MAX - quotient * q;

    return quotient + (remainder == q - 1u);
}

/*
 * ---------------------------------------------------------------
 * Two-word Barrett reduction (general path, q <= 2^63 - 1).
 * ---------------------------------------------------------------
 *
 * The single-word reciprocal cannot reduce a 128-bit product, so for moduli
 * at or above 2^32 the reciprocal is widened to mu = floor(2^128 / q), which
 * is itself a 128-bit value carried as (mu_hi, mu_lo). The quotient estimate
 *
 *     qhat = floor(x * mu / 2^128)
 *
 * then differs from floor(x / q) by at most one for every x in [0, 2^128),
 * so a single conditional subtraction yields the canonical remainder.
 * Computing qhat only needs the high half of the 128x128 product, which is
 * obtained with 64x64 multiply-high/low ops and carry propagation.
 */

/**
 * @brief Computes the upper 128 bits of a 128-by-128-bit product.
 *
 * Decomposes the operands into pairs of 64-bit words and accumulates the
 * middle carries so that the two most-significant digits of the 256-bit
 * product are recovered without a native 128-bit integer type.
 *
 * @param[in] a_hi High word of the first operand.
 * @param[in] a_lo Low word of the first operand.
 * @param[in] b_hi High word of the second operand.
 * @param[in] b_lo Low word of the second operand.
 * @param[out] r_hi High word of the upper half of the product.
 * @param[out] r_lo Low word of the upper half of the product.
 */
static void barrett_mulhi128(uint64_t a_hi,
                             uint64_t a_lo,
                             uint64_t b_hi,
                             uint64_t b_lo,
                             uint64_t *r_hi,
                             uint64_t *r_lo)
{
    uint64_t c0 = scalar_mulhi_u64(a_lo, b_lo);
    uint64_t d1 = a_lo * b_hi;
    uint64_t d2 = scalar_mulhi_u64(a_lo, b_hi);
    uint64_t d3 = a_hi * b_lo;
    uint64_t d4 = scalar_mulhi_u64(a_hi, b_lo);
    uint64_t d5 = a_hi * b_hi;
    uint64_t d6 = scalar_mulhi_u64(a_hi, b_hi);

    uint64_t s1 = c0 + d1;
    uint64_t c1 = (s1 < c0) ? 1u : 0u;
    uint64_t s2 = s1 + d3;
    uint64_t c2 = (s2 < s1) ? 1u : 0u;
    uint64_t carry1 = c1 + c2;

    uint64_t m = d2 + d4;
    uint64_t c3 = (m < d2) ? 1u : 0u;
    uint64_t m2 = m + d5;
    uint64_t c4 = (m2 < m) ? 1u : 0u;
    uint64_t m3 = m2 + carry1;
    uint64_t c5 = (m3 < m2) ? 1u : 0u;
    uint64_t carry2 = c3 + c4 + c5;

    *r_lo = m3;
    *r_hi = d6 + carry2;
}

/* Dispatch helper for the hot multiplication/reduction path. */
static uint64_t barrett_reduce_u128(uint64_t x_hi,
                                    uint64_t x_lo,
                                    uint64_t q,
                                    uint64_t mu_hi,
                                    uint64_t mu_lo)
{
    uint64_t q3_hi, q3_lo;
    barrett_mulhi128(x_hi, x_lo, mu_hi, mu_lo, &q3_hi, &q3_lo);

    /*
     * t = q3 * q. Since q3 <= floor(x / q), q3 * q <= x < 2^128, so the
     * true product fits in two words and neither intermediate overflows.
     */
    uint64_t t_hi = scalar_mulhi_u64(q3_lo, q) + q3_hi * q;
    uint64_t t_lo = q3_lo * q;

    uint64_t r_lo = x_lo - t_lo;
    uint64_t borrow = (x_lo < t_lo) ? 1u : 0u;
    uint64_t r_hi = x_hi - t_hi - borrow;

    (void)r_hi;
    uint64_t r = r_lo;
    if (r >= q) {
        r -= q;
    }
    return r;
}

/**
 * @brief Computes the 128-bit Barrett reciprocal for the general path.
 *
 * Computers mu = floor(2^128 / q) as two words without performing a 128/64
 * integer division. Uses the identity
 *
 *     floor(2^128 / q) = 2^64 * w + floor(2^64 * r / q),
 *
 * where w = floor(2^64 / q) and r = 2^64 mod q. The single-word w is derived
 * from a UINT64_MAX division and the second term by a 64-step binary long
 * division of the two-word dividend (r, 0).
 *
 * @param[in]  q      Modulus, q <= 2^63 - 1.
 * @param[out] mu_hi  High word of floor(2^128 / q).
 * @param[out] mu_lo  Low word of floor(2^128 / q).
 */
void ntt_scalar_barrett_mu128(uint64_t q, uint64_t *mu_hi, uint64_t *mu_lo)
{
    uint64_t w = UINT64_MAX / q;
    uint64_t w_rem = UINT64_MAX - w * q;
    w += (w_rem == q - 1u);

    /*
     * r = 2^64 mod q. w * q < 2^64 for any odd q, so negation wraps to
     * 2^64 - w*q without overflow.
     */
    uint64_t r = 0u - w * q;

    uint64_t m = 0;
    uint64_t rem = 0;
    for (int i = 63; i >= 0; i--) {
        /* High-half dividend bit (position 64+i) is bit i of r. */
        uint64_t nr = (rem << 1) | ((r >> i) & 1u);
        if (nr >= q) {
            nr -= q;
        }
        rem = nr;
    }
    for (int i = 63; i >= 0; i--) {
        /* Low-half dividend bits are zero; quotient bits 63..0 form m. */
        uint64_t nr = rem << 1;
        if (nr >= q) {
            nr -= q;
            m |= UINT64_C(1) << i;
        }
        rem = nr;
    }

    *mu_hi = w;
    *mu_lo = m;
}

/**
 * @brief Reduces an arbitrary 128-bit value modulo q using Barrett.
 *
 * @param[in] x_hi   High word of the 128-bit value to reduce.
 * @param[in] x_lo   Low word of the 128-bit value to reduce.
 * @param[in] q      Modulus, q <= 2^63 - 1.
 * @param[in] mu_hi  High word of floor(2^128 / q).
 * @param[in] mu_lo  Low word of floor(2^128 / q).
 *
 * @return @p (x_hi, x_lo) modulo @p q in canonical representation.
 */
uint64_t ntt_scalar_barrett_reduce_u128(uint64_t x_hi,
                                        uint64_t x_lo,
                                        uint64_t q,
                                        uint64_t mu_hi,
                                        uint64_t mu_lo)
{
    return barrett_reduce_u128(x_hi, x_lo, q, mu_hi, mu_lo);
}

/**
 * @brief Multiplies two canonical residues using two-word Barrett reduction.
 *
 * Forms the full 128-bit product of @p a and @p b and reduces it. Both
 * operands are canonical residues modulo @p q with q <= 2^63 - 1.
 *
 * @param[in] a      First canonical residue.
 * @param[in] b      Second canonical residue.
 * @param[in] q      Modulus, q <= 2^63 - 1.
 * @param[in] mu_hi  High word of floor(2^128 / q).
 * @param[in] mu_lo  Low word of floor(2^128 / q).
 *
 * @return The canonical residue @f$(a\cdot b)\bmod q@f$.
 */
uint64_t ntt_scalar_barrett_mul_u128(uint64_t a,
                                     uint64_t b,
                                     uint64_t q,
                                     uint64_t mu_hi,
                                     uint64_t mu_lo)
{
    return barrett_reduce_u128(scalar_mulhi_u64(a, b), a * b, q, mu_hi, mu_lo);
}

/**
 * @brief Computes modular exponentiation using two-word Barrett reduction.
 *
 * @param[in] base   Base value.
 * @param[in] exp    Non-negative exponent.
 * @param[in] q      Modulus, q <= 2^63 - 1.
 * @param[in] mu_hi  High word of floor(2^128 / q).
 * @param[in] mu_lo  Low word of floor(2^128 / q).
 *
 * @return The canonical residue @f$base^{exp}\bmod q@f$.
 */
uint64_t ntt_scalar_barrett_modpow_u128(uint64_t base,
                                        uint64_t exp,
                                        uint64_t q,
                                        uint64_t mu_hi,
                                        uint64_t mu_lo)
{
    uint64_t result = 1;
    base = ntt_scalar_barrett_reduce_u128(0, base, q, mu_hi, mu_lo);

    while (exp != 0) {
        if (exp & 1u) {
            result = ntt_scalar_barrett_mul_u128(result, base, q, mu_hi, mu_lo);
        }
        base = ntt_scalar_barrett_mul_u128(base, base, q, mu_hi, mu_lo);
        exp >>= 1;
    }
    return result;
}
