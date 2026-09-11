/*
 * wide_int.h
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

#ifndef WIDE_INT_H
#define WIDE_INT_H

#include <stdint.h>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#if defined(__SIZEOF_INT128__)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#endif

/***************************************************************************
 * Extended-precision primitives for the reduction backend.
 *
 * Every reduction path needs multiply-high products and, on the two-word
 * paths, full 64x64 -> 128-bit products. The wide products are formed by one
 * of three variants depending on what the toolchain provides:
 *
 * - native variant: compilers that define __SIZEOF_INT128__ (GCC, Clang) form
 *   the whole product with a single extended-precision multiplication, which
 *   collapses to one mulq/umulh instruction on 64-bit targets.
 * - intrinsic variant: on x64 MSVC the _umul128 / __umulh intrinsics provide
 *   the same single-instruction multiply.
 * - decomposed variant: everywhere else (e.g., 32-bit MSVC) falls back to a
 *   64-bit-limb decomposition that needs no compiler extension.
 *
 * The variant is selected at compile time. The public reduction API is
 * identical on all three. See barrett.c and montgomery.c for the consumers.
 ****************************************************************************/

/**
 * @brief Computes the high 64 bits of a 64-by-64-bit multiplication.
 *
 * Returns the upper half of the mathematical 128-bit product @p a*@p b.
 *
 * @param[in] a First 64-bit operand.
 * @param[in] b Second 64-bit operand.
 *
 * @return The upper 64 bits of the mathematical product @p a*@p b.
 */
static inline uint64_t scalar_mulhi_u64(uint64_t a, uint64_t b)
{
#if defined(__SIZEOF_INT128__)
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    return __umulh(a, b);
#else
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
#endif
}

/**
 * @brief Computes the full 128-bit product of two 64-bit words.
 *
 * Recovers both halves of @f$a\cdot b@f$ in a single primitive. The native
 * and intrinsic variants form the whole product at once whereas the decomposed
 * variant does a multiply-high followed by a plain multiply.
 *
 * @param[in]  a  First 64-bit operand.
 * @param[in]  b  Second 64-bit operand.
 * @param[out] hi Upper 64 bits of the mathematical product.
 * @param[out] lo Lower 64 bits of the mathematical product.
 */
static inline void
scalar_mulwide_u64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
#if defined(__SIZEOF_INT128__)
    unsigned __int128 p = (unsigned __int128)a * b;
    *hi = (uint64_t)(p >> 64);
    *lo = (uint64_t)p;
#elif defined(_MSC_VER) && defined(_M_X64)
    *lo = _umul128(a, b, hi);
#else
    *hi = scalar_mulhi_u64(a, b);
    *lo = a * b;
#endif
}

/**
 * @brief Computes the upper 128 bits of a 128-by-128-bit product.
 *
 * Decomposes the operands into pairs of 64-bit words and accumulates the
 * middle carries so that the two most-significant words of the 256-bit
 * product are recovered. Each partial product is formed through the wide
 * multiplication primitives above.
 *
 * @param[in]  a_hi High word of the first operand.
 * @param[in]  a_lo Low word of the first operand.
 * @param[in]  b_hi High word of the second operand.
 * @param[in]  b_lo Low word of the second operand.
 * @param[out] r_hi High word of the upper half of the product.
 * @param[out] r_lo Low word of the upper half of the product.
 */
static inline void scalar_mulhi128_u64(uint64_t a_hi,
                                       uint64_t a_lo,
                                       uint64_t b_hi,
                                       uint64_t b_lo,
                                       uint64_t *r_hi,
                                       uint64_t *r_lo)
{
    uint64_t c0 = scalar_mulhi_u64(a_lo, b_lo);
    uint64_t d1, d2, d3, d4, d5, d6;

    scalar_mulwide_u64(a_lo, b_hi, &d2, &d1);
    scalar_mulwide_u64(a_hi, b_lo, &d4, &d3);
    scalar_mulwide_u64(a_hi, b_hi, &d6, &d5);

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

#if defined(__SIZEOF_INT128__)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

#endif /* WIDE_INT_H */
