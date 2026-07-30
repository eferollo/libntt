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
 * @brief Computes the high 64 bits of a 64-by-64-bit multiplication.
 *
 * Computes the upper half of the mathematical 128-bit product @p a*@p b
 * using only standard C11 64-bit arithmetic. The operands are decomposed into
 * 32-bit limbs, avoiding any dependency on a compiler-provided 128-bit integer
 * type.
 *
 * @param[in] a First 64-bit operand.
 * @param[in] b Second 64-bit operand.
 *
 * @return The upper 64 bits of the mathematical product @p a*@p b.
 */
static uint64_t scalar_mulhi_u64(uint64_t a, uint64_t b)
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

/**
 * @brief Reduces a bounded 64-bit integer using Barrett reduction.
 *
 * Uses the precomputed reciprocal
 * @f$\mu = \lfloor 2^{64}/q \rfloor@f$ to estimate the quotient without
 * hardware integer division. For this scalar backend, the function is used
 * with @p x < q^2, so the quotient estimate is sufficiently accurate that a
 * single final conditional subtraction produces a canonical remainder.
 *
 * @param[in] x  Value to reduce, satisfying @p x < @p q squared.
 * @param[in] q  Prime modulus.
 * @param[in] mu Precomputed Barrett reciprocal
 *               @f$\mu = \lfloor 2^{64}/q \rfloor@f$.
 *
 * @return @p x modulo @p q in canonical representation.
 */
uint32_t ntt_scalar_barrett_reduce_u64(uint64_t x, uint32_t q, uint64_t mu)
{
    uint64_t qhat = scalar_mulhi_u64(x, mu);
    uint64_t r = x - qhat * q;

    if (r >= q) {
        r -= q;
    }

    return (uint32_t)r;
}

/**
 * @brief Multiplies two canonical residues using Barrett reduction.
 *
 * Forms the full 64-bit product of @p a and @p b and reduces it through
 * ntt_scalar_barrett_reduce_u64(). No integer division or modulo operator is
 * executed on this performance-critical multiplication path.
 *
 * @param a  First canonical residue.
 * @param b  Second canonical residue.
 * @param q  Modulus.
 * @param mu Precomputed Barrett reciprocal.
 *
 * @return The canonical residue @f$(a\cdot b)\bmod q@f$.
 */
uint32_t ntt_scalar_barrett_mul(uint32_t a, uint32_t b, uint32_t q, uint64_t mu)
{
    return ntt_scalar_barrett_reduce_u64((uint64_t)a * b, q, mu);
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
 * @param q    Modulus.
 * @param mu   Precomputed Barrett reciprocal.
 *
 * @return The canonical residue @f$base^{exp}\bmod q@f$.
 */
uint32_t
ntt_scalar_barrett_modpow(uint32_t base, uint32_t exp, uint32_t q, uint64_t mu)
{
    uint32_t result = 1;
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
 * @param[in] q Prime modulus greater than two.
 *
 * @return Barrett reciprocal @f$\mu = \lfloor 2^{64}/q \rfloor@f$.
 */
uint64_t ntt_scalar_barrett_mu(uint32_t q)
{
    uint64_t quotient = UINT64_MAX / q;
    uint64_t remainder = UINT64_MAX - quotient * q;

    return quotient + (remainder == q - 1u);
}
