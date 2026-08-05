/*
 * ntt_arith.c
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

#include "ntt_scalar_toy_internal.h"

/**
 * @brief Reduces an integer modulo q.
 *
 * Computes (a mod q).
 *
 * This implementation uses the C remainder operator and is intended as a
 * simple, generic reference implementation. It does not use any
 * modulus-specific optimization.
 *
 * @param[in] a     Integer to reduce.
 * @param[in] q     Prime modulus.
 *
 * @return a reduced modulo the chosen modulus.
 */
uint64_t ntt__reduce(uint64_t a, uint64_t q)
{
    return a % q;
}

/**
 * @brief Computes modular addition.
 *
 * Computes (a + b) mod q.
 *
 * The operands are assumed to be reduced modulo q. The result is reduced
 * using a conditional subtraction to avoid the cost of a division operation.
 *
 * @param[in] a     First operand.
 * @param[in] b     Second operand.
 * @param[in] q     Prime modulus.
 *
 * @return (a + b) mod q.
 */
uint64_t ntt__addmod(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t s = a + b;
    return (s >= q) ? s - q : s;
}

/**
 * @brief Computes modular subtraction.
 *
 * Computes (a - b) mod q.
 *
 * The operands are assumed to be reduced modulo q. A conditional addition
 * of the modulus is used when the subtraction would otherwise produce a
 * negative result.
 *
 * @param[in] a     Minuend.
 * @param[in] b     Subtrahend.
 * @param[in] q     Prime modulus.
 *
 * @return (a - b) mod q.
 */
uint64_t ntt__submod(uint64_t a, uint64_t b, uint64_t q)
{
    return (a >= b) ? (a - b) : (a + q - b);
}

/**
 * @brief Computes modular multiplication.
 *
 * Computes (a * b) mod q.
 *
 * A portable shift-and-add accumulation avoids any dependency on a
 * compiler-provided 128-bit integer type, so this reference implementation
 * works for moduli up to 2^63 (products would otherwise exceed 64 bits). The
 * intermediate operations stay below 2^64 because every accumulated value is
 * reduced modulo q after each addition.
 *
 * @param[in] a     First operand.
 * @param[in] b     Second operand.
 * @param[in] q     Prime modulus.
 *
 * @return (a * b) mod q.
 */
uint64_t ntt__mulmod(uint64_t a, uint64_t b, uint64_t q)
{
    a %= q;
    b %= q;

    uint64_t r = 0;
    while (b != 0) {
        if (b & 1u) {
            r = (r + a) % q;
        }
        a = (a << 1) % q;
        b >>= 1;
    }

    return r;
}

/**
 * @brief Computes modular exponentiation using the binary exponentiation
 * algorithm.
 *
 * Computes (base^exp mod q) using the square-and-multiply algorithm.
 *
 * The exponent is processed one bit at a time, starting with the least
 * significant bit. For each set bit, the current result is multiplied by the
 * current power of the base. After each iteration, the base is squared and
 * the exponent is shifted right by one bit.
 *
 * The algorithm requires O(log(exp)) modular multiplications.
 *
 * @param[in] base  Base of the exponentiation.
 * @param[in] exp   Non-negative exponent.
 * @param[in] q     Prime modulus.
 *
 * @return (base^exp mod q).
 */
uint64_t ntt__modpow(uint64_t base, uint64_t exp, uint64_t q)
{
    uint64_t result = 1;

    base = ntt__reduce(base, q);

    while (exp > 0) {
        if (exp & 1u) {
            result = ntt__mulmod(result, base, q);
        }

        base = ntt__mulmod(base, base, q);
        exp >>= 1;
    }

    return result;
}

/**
 * @brief Computes the modular multiplicative inverse using Fermat's little
 * theorem.
 *
 * The modulus passed as argument must be prime, and a must be non-zero modulo
 * q.
 *
 * @param[in] a     Integer whose modular multiplicative inverse is to be
 *                  computed.
 * @param[in] q     Prime modulus.
 *
 * @return The multiplicative inverse of a modulo q.
 *
 * @warning The result is undefined if the passed modulus is not prime or if
 *          a is congruent to zero modulo q.
 */
uint64_t ntt__modinv(uint64_t a, uint64_t q)
{
    return ntt__modpow(a, q - 2, q);
}