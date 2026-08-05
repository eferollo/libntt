/*
 * Tests both supported radices:
 *   - q32 = true:  R = 2^32, single-word -q^-1 mod 2^32 (moduli < 2^32).
 *   - q32 = false: R = 2^64, -q^-1 mod 2^64 (moduli in [2^32, 2^63]).
 */
#include "montgomery.c"
#include "ref_arith.h"
#include "test_common.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>
#include <openssl/rand.h>

/** @brief Deterministic PRNG state (shared splitmix64 in test_common.h). */
static uint64_t prng_state = UINT64_C(0x9E3779B97F4A7C15);

/** @brief Returns true when the stress mode environment variable is set. */
static bool stress_mode_enabled(void)
{
    const char *stress = getenv("NTT_MONTGOMERY_STRESS");
    return stress != NULL && stress[0] != '\0';
}

/** @brief Scales @p base by the stress factor when stress mode is active. */
static uint32_t test_iterations(uint32_t base)
{
    return stress_mode_enabled() ? base * 100u : base;
}

/** @brief Seeds the PRNG, from the CSPRNG in stress mode, fixed otherwise. */
static void prng_seed(void)
{
    if (stress_mode_enabled()) {
        RAND_bytes((unsigned char *)&prng_state, sizeof(prng_state));
    }
}

/* Odd-prime moduli for the R = 2^32 path (q < 2^32). */
static const uint64_t Q32_MODULI[] = {
    3ull,
    5ull,
    113ull,
    257ull,
    12289ull,
    998244353ull,
    2147483647ull,
    4294967291ull,
};
#define Q32_MODULI_COUNT (sizeof(Q32_MODULI) / sizeof(Q32_MODULI[0]))

/*
 * Odd-prime moduli for the R = 2^64 general path (q >= 2^32, q <= 2^63).
 * Each entry is verified prime by ntt_is_prime().
 */
static const uint64_t Q64_MODULI[] = {
    4294967311ull,          /* 2^32 + 15, smallest 33-bit prime */
    2305843009213693951ull, /* 2^61 - 1, Mersenne prime */
    4611686018427387847ull, /* 63-bit prime */
    9223372036854775783ull, /* largest prime below 2^63 */
};
#define Q64_MODULI_COUNT (sizeof(Q64_MODULI) / sizeof(Q64_MODULI[0]))

/** @brief Checks the modular inverse invariant q * qinv == -1 mod R. */
static void torture_ntt_scalar_mont_qinv(void **state)
{
    (void)state;
    for (uint64_t i = 0; i < Q32_MODULI_COUNT; i++) {
        uint64_t q = Q32_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, true);
        assert_true((uint32_t)(q * qinv) == UINT32_MAX);
    }

    for (uint64_t i = 0; i < Q64_MODULI_COUNT; i++) {
        uint64_t q = Q64_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, false);
        assert_true((uint64_t)(q * qinv) == UINT64_MAX);
    }
}

/** @brief Checks that mont_r2 equals R^2 mod q for both radices. */
static void torture_ntt_scalar_mont_r2(void **state)
{
    (void)state;
    for (uint64_t i = 0; i < Q32_MODULI_COUNT; i++) {
        uint64_t q = Q32_MODULI[i];
        uint64_t r2 = ntt_scalar_mont_r2(q, true);
        /* (2^64 mod q)^2 == 2^128 mod q == r2, i.e. r2^2 mod q == power */
        uint64_t expect = 1;
        for (unsigned k = 0; k < 64; k++) {
            expect = (expect << 1);
            if (expect >= q) {
                expect -= q;
            }
        }
        assert_int_equal(r2, expect);
    }

    for (uint64_t i = 0; i < Q64_MODULI_COUNT; i++) {
        uint64_t q = Q64_MODULI[i];
        uint64_t r2 = ntt_scalar_mont_r2(q, false);
        uint64_t expect = 1;
        for (unsigned k = 0; k < 128; k++) {
            expect <<= 1;
            if (expect >= q) {
                expect -= q;
            }
        }
        assert_int_equal(r2, expect);
    }
}

/** @brief encode/decode round trip over the supported radices. */
static void torture_ntt_scalar_mont_encode_decode(void **state)
{
    (void)state;
    const uint32_t iterations = test_iterations(400);

    /* q32 path; operands stay below 2^32. */
    for (uint64_t i = 0; i < Q32_MODULI_COUNT; i++) {
        uint64_t q = Q32_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, true);
        uint64_t r2 = ntt_scalar_mont_r2(q, true);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t x = ntt_test_prng_range(&prng_state, q);
            uint64_t enc = ntt_scalar_mont_encode(x, q, qinv, r2, true);
            assert_int_equal(ntt_scalar_mont_decode(enc, q, qinv, true), x);

            uint64_t y = ntt_test_prng_range(&prng_state, q);
            uint64_t enc_y = ntt_scalar_mont_encode(y, q, qinv, r2, true);
            uint64_t prod = ntt_scalar_mont_mul(enc, enc_y, q, qinv, true);
            assert_int_equal(ntt_scalar_mont_decode(prod, q, qinv, true),
                             ntt_test_ref_mul_u64(x, y, q));
        }
    }

    /* q64 path; operands can be full 64-bit residues. */
    for (uint64_t i = 0; i < Q64_MODULI_COUNT; i++) {
        uint64_t q = Q64_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, false);
        uint64_t r2 = ntt_scalar_mont_r2(q, false);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t x = ntt_test_prng_range(&prng_state, q);
            uint64_t enc = ntt_scalar_mont_encode(x, q, qinv, r2, false);
            assert_int_equal(ntt_scalar_mont_decode(enc, q, qinv, false), x);

            uint64_t y = ntt_test_prng_range(&prng_state, q);
            uint64_t enc_y = ntt_scalar_mont_encode(y, q, qinv, r2, false);
            uint64_t prod = ntt_scalar_mont_mul(enc, enc_y, q, qinv, false);
            assert_int_equal(ntt_scalar_mont_decode(prod, q, qinv, false),
                             ntt_test_ref_mul_u64(x, y, q));
        }
    }
}

/** @brief modpow differential against a plain-% reference for both radices. */
static void torture_ntt_scalar_mont_modpow(void **state)
{
    (void)state;
    const uint32_t iterations = test_iterations(40);

    for (uint64_t i = 0; i < Q32_MODULI_COUNT; i++) {
        uint64_t q = Q32_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, true);
        uint64_t r2 = ntt_scalar_mont_r2(q, true);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t base = ntt_test_prng_range(&prng_state, q);
            uint64_t exp = ntt_test_prng_next_u64(&prng_state);
            assert_int_equal(
                ntt_scalar_mont_modpow(base, exp, q, qinv, r2, true),
                ntt_test_ref_modpow_u64(base, exp, q));
        }
    }

    for (uint64_t i = 0; i < Q64_MODULI_COUNT; i++) {
        uint64_t q = Q64_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, false);
        uint64_t r2 = ntt_scalar_mont_r2(q, false);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t base = ntt_test_prng_range(&prng_state, q);
            uint64_t exp = ntt_test_prng_next_u64(&prng_state);
            assert_int_equal(
                ntt_scalar_mont_modpow(base, exp, q, qinv, r2, false),
                ntt_test_ref_modpow_u64(base, exp, q));
        }
    }
}

/** @brief Fermat: a^(q-1) == 1 for every odd prime q and a != 0. */
static void torture_ntt_scalar_mont_modpow_fermat(void **state)
{
    (void)state;
    const uint32_t iterations = test_iterations(30);

    for (uint64_t i = 0; i < Q32_MODULI_COUNT; i++) {
        uint64_t q = Q32_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, true);
        uint64_t r2 = ntt_scalar_mont_r2(q, true);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t a = 1ull + ntt_test_prng_range(&prng_state, q - 1ull);
            assert_int_equal(
                ntt_scalar_mont_modpow(a, q - 1ull, q, qinv, r2, true),
                1u);
        }
    }

    for (uint64_t i = 0; i < Q64_MODULI_COUNT; i++) {
        uint64_t q = Q64_MODULI[i];
        uint64_t qinv = ntt_scalar_mont_qinv(q, false);
        uint64_t r2 = ntt_scalar_mont_r2(q, false);
        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t a = 1ull + ntt_test_prng_range(&prng_state, q - 1ull);
            assert_int_equal(
                ntt_scalar_mont_modpow(a, q - 1ull, q, qinv, r2, false),
                1u);
        }
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_scalar_mont_qinv),
        cmocka_unit_test(torture_ntt_scalar_mont_r2),
        cmocka_unit_test(torture_ntt_scalar_mont_encode_decode),
        cmocka_unit_test(torture_ntt_scalar_mont_modpow),
        cmocka_unit_test(torture_ntt_scalar_mont_modpow_fermat),
    };

    prng_seed();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
