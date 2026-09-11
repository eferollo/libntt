/*
 * ref_arith.h
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

#ifndef NTT_TEST_REF_ARITH_H
#define NTT_TEST_REF_ARITH_H

#include <stdint.h>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

/*
 * Shared reference arithmetic for differential tests.
 *
 * The library deliberately avoids 128-bit types in its portable paths (e.g.
 * the shift-and-add mulmod in ntt_utils.c), so the test oracle must be
 * independent of the code under test. It is provided in three mutually
 * exclusive variants, selected at compile time:
 *
 * - native: compilers that define __SIZEOF_INT128__ (GCC, Clang) reduce
 *   the 128-bit product directly.
 * - intrinsic: on x64 MSVC the _umul128 / __umulh intrinsics reach the
 *   same single 64x64 -> 128-bit multiply.
 * - decomposed: everywhere else (e.g., 32-bit MSVC) falls back to fully
 *   portable 64-bit implementations.
 *
 * The fallbacks are slow but obviously correct, and they keep differential
 * coverage running on every toolchain instead of silently skipping it.
 */

/*
 * Shift-and-add reduction, shared by the intrinsic and decomposed variants:
 * both have to reduce an arbitrary 128-bit value modulo q word by word.
 */
static inline uint64_t ntt_test_ref_addmod(uint64_t a, uint64_t b, uint64_t q)
{
    /* a, b in [0, q): the single subtraction is exact because a + b < 2q. */
    if (a >= q - b) {
        return a - (q - b);
    }
    return a + b;
}

static inline uint64_t
ntt_test_ref_reduce128(uint64_t hi, uint64_t lo, uint64_t q)
{
    uint64_t r = 0;
    for (int i = 127; i >= 0; i--) {
        r = ntt_test_ref_addmod(r, r, q);
        if (((i >= 64) ? (hi >> (i - 64)) : (lo >> i)) & 1u) {
            r = ntt_test_ref_addmod(r, 1, q);
        }
    }
    return r;
}

#if defined(__SIZEOF_INT128__) /* native variant */

static inline uint64_t ntt_test_ref_add_u64(uint64_t a, uint64_t b, uint64_t q)
{
    return (uint64_t)(((unsigned __int128)a + b) % q);
}

static inline uint64_t ntt_test_ref_mul_u64(uint64_t a, uint64_t b, uint64_t q)
{
    return (uint64_t)(((unsigned __int128)a * b) % q);
}

static inline uint64_t ntt_test_ref_mulhi_u64(uint64_t a, uint64_t b)
{
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}

#elif defined(_MSC_VER) && defined(_M_X64) /* intrinsic variant */

static inline uint64_t ntt_test_ref_add_u64(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t s = a + b;
    return ntt_test_ref_reduce128((uint64_t)(s < a), s, q);
}

static inline uint64_t ntt_test_ref_mul_u64(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t hi;
    uint64_t lo = _umul128(a, b, &hi);
    return ntt_test_ref_reduce128(hi, lo, q);
}

static inline uint64_t ntt_test_ref_mulhi_u64(uint64_t a, uint64_t b)
{
    return __umulh(a, b);
}

#else /* decomposed variant */

static inline uint64_t ntt_test_ref_add_u64(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t s = a + b;
    return ntt_test_ref_reduce128((uint64_t)(s < a), s, q);
}

static inline uint64_t ntt_test_ref_mul_u64(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t a0 = a & 0xFFFFFFFFu;
    uint64_t a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu;
    uint64_t b1 = b >> 32;

    uint64_t low = a0 * b0;
    uint64_t mid1 = a0 * b1;
    uint64_t mid2 = a1 * b0;
    uint64_t high = a1 * b1;

    uint64_t mid = mid1 + mid2;
    uint64_t mid_carry = (uint64_t)(mid < mid1);

    uint64_t lo = low + (mid << 32);
    uint64_t hi = high + (mid >> 32) + (uint64_t)(lo < low) + (mid_carry << 32);

    return ntt_test_ref_reduce128(hi, lo, q);
}

static inline uint64_t ntt_test_ref_mulhi_u64(uint64_t a, uint64_t b)
{
    /*
     * Shift-and-add high word: accumulate the a*2^i contributions that spill
     * past bit 63 of the product into a (hi, lo) accumulator.
     */
    uint64_t hi = 0;
    uint64_t lo = 0;

    for (uint64_t i = 0; i < 64; i++) {
        if ((b >> i) & 1u) {
            uint64_t add_hi = (i == 0) ? 0 : (a >> (64 - i));
            uint64_t add_lo = a << i;
            uint64_t sum = lo + add_lo;
            uint64_t carry = (sum < add_lo) ? 1u : 0u;

            lo = sum;
            hi = hi + add_hi + carry;
        }
    }

    return hi;
}

#endif /* ntt_test_ref_* variant selection */

static inline uint64_t
ntt_test_ref_modpow_u64(uint64_t base, uint64_t exp, uint64_t q)
{
    uint64_t r = 1u % q;
    base %= q;
    while (exp != 0) {
        if (exp & 1u) {
            r = ntt_test_ref_mul_u64(r, base, q);
        }
        base = ntt_test_ref_mul_u64(base, base, q);
        exp >>= 1;
    }
    return r;
}

#endif /* NTT_TEST_REF_ARITH_H */
