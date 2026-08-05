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

/*
 * Shared reference arithmetic for differential tests.
 *
 * The library deliberately avoids 128-bit types in its portable paths (e.g.
 * the shift-and-add mulmod in ntt_utils.c), so the test oracle must be
 * independent of the code under test. Compilers that provide __int128 are the
 * primary reference. Toolchains without it (e.g., MSVC) fall back to fully
 * portable 64-bit implementations. The fallbacks are slow but obviously
 * correct, and they keep differential coverage running on every toolchain
 * instead of silently skipping it.
 */

#if defined(__SIZEOF_INT128__)

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

#else /* !defined(__SIZEOF_INT128__) */

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

#endif /* __SIZEOF_INT128__ */

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
