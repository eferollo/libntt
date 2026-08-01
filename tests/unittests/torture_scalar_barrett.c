/*
 * Unit tests for the scalar Barrett reduction backend
 * (src/adapters/scalar/redc/barrett.c).
 *
 * Each test function exercises one public entry point. Fixed regression
 * vectors come from vectors/barrett_vectors.h (regenerate with
 * vectors/gen_barrett_vectors.py). Additional coverage is differential
 * against a reference that uses plain % arithmetic, driven by a seeded
 * deterministic PRNG. The "negative" tests feed the functions inputs
 * outside their documented domain (e.g. x >= q^2 for reduce_u64,
 * non-canonical operands for mul) and verify the documented degradation
 * behaviour.
 */
#include "barrett.c"
#include "vectors/barrett_vectors.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmocka.h>
#include <openssl/rand.h>

/*
 * Deterministic PRNG (splitmix64).
 *
 * Seeded with a fixed constant so runs are reproducible. When the
 * NTT_BARRETT_STRESS environment variable is set, the seed is taken from
 * OpenSSL's CSPRNG and the iteration counts are scaled up.
 */
static uint64_t prng_state = UINT64_C(0x9E3779B97F4A7C15);

/** @brief Returns the next 64-bit PRNG output. */
static uint64_t prng_next_u64(void)
{
    uint64_t z = (prng_state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/** @brief Returns the next 32-bit PRNG output. */
static uint32_t prng_next_u32(void)
{
    return (uint32_t)prng_next_u64();
}

/** @brief Returns true when the stress mode environment variable is set. */
static bool stress_mode_enabled(void)
{
    const char *stress = getenv("NTT_BARRETT_STRESS");
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

/*
 * Reference implementations using plain % arithmetic.
 */

/** @brief Reference square-and-multiply for base^exp mod q. */
static uint32_t ref_modpow(uint32_t base, uint32_t exp, uint32_t q)
{
    uint64_t result = 1;
    uint64_t b = base % q;

    while (exp != 0) {
        if (exp & 1u) {
            result = (result * b) % q;
        }
        b = (b * b) % q;
        exp >>= 1;
    }
    return (uint32_t)result;
}

/** @brief q^2 as a 64-bit value (avoids 32-bit wraparound of q * q). */
static uint64_t q_squared(uint32_t q)
{
    return (uint64_t)q * q;
}

/*
 * Reference for the high 64 bits of the 128-bit product a * b. The primary
 * implementation uses the compiler-provided unsigned __int128 when available
 * (every GCC/Clang 64-bit toolchain). The fallback is a plain shift-and-add
 * multiplication over the full 128-bit (hi, lo) accumulator.
 */

/** @brief Shift-and-add high-word reference (pure C11, no 128-bit type). */
static uint64_t ref_mulhi_shiftadd(uint64_t a, uint64_t b)
{
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

#if defined(__SIZEOF_INT128__)
/** @brief High word of a*b via a 128-bit intermediate. */
static uint64_t ref_mulhi_u64(uint64_t a, uint64_t b)
{
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}
#else
static uint64_t ref_mulhi_u64(uint64_t a, uint64_t b)
{
    return ref_mulhi_shiftadd(a, b);
}
#endif

/*
 * Moduli swept by the differential tests: canonical NTT primes, Mersenne
 * primes and boundary/composite values that exercise edge cases.
 */
static const uint32_t MODULUS_SWEEP[] = {
    2,          3,          4,          5,          6,         113,
    257,        65537,      7681,       12289,      3329,      1048573,
    4194304,    7340033,    8380417,    167772161,  469762049, 998244353,
    2147483629, 2147483647, 4294967291, 4294967295,
};
#define MODULUS_SWEEP_COUNT (sizeof(MODULUS_SWEEP) / sizeof(MODULUS_SWEEP[0]))

/*
 * Odd-prime subset of MODULUS_SWEEP used by the Fermat / modular-inverse
 * tests, which require a prime modulus. Kept as a dedicated list so
 * ntt_is_prime() (which logs on composites) is never called on the
 * composite sweep values.
 */
static const uint32_t PRIME_MODULI[] = {
    3,
    5,
    113,
    257,
    65537,
    7681,
    12289,
    3329,
    1048573,
    7340033,
    8380417,
    167772161,
    469762049,
    998244353,
    2147483629,
    2147483647,
    4294967291,
};
#define PRIME_MODULI_COUNT (sizeof(PRIME_MODULI) / sizeof(PRIME_MODULI[0]))

/** @brief Checks mu against fixed vectors generated by the Python reference. */
static void torture_ntt_scalar_barrett_mu(void **state)
{
    for (uint64_t i = 0; i < BARRETT_MU_VECTORS_COUNT; i++) {
        const barrett_mu_vector *v = &BARRETT_MU_VECTORS[i];
        uint64_t rc = ntt_scalar_barrett_mu(v->q);
        assert_int_equal(rc, v->mu);
    }
}

/** @brief Checks mu bounds floor(2^64/q) <= mu <= floor(2^64/q)+1. */
static void torture_ntt_scalar_barrett_mu_bounds(void **state)
{
    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);
        assert_true(mu >= UINT64_MAX / q);
        assert_true(mu <= UINT64_MAX / q + 1);
    }
}

/** @brief Negative: degenerate modulus q=1 and maximal q=UINT32_MAX. */
static void torture_ntt_scalar_barrett_mu_negative(void **state)
{
    /*
     * q=1: remainder == q-1u holds, so the +1 wraps 2^64 back to 0. mu is
     * 0 (not 2^64, which is unrepresentable). The only other degenerate input
     * is q=0 (division by zero, UB). For all q >= 2 the +1 never overflows,
     * so those are covered by the bounds sweep instead.
     */
    uint64_t rc = ntt_scalar_barrett_mu(1);
    assert_int_equal(rc, 0);

    /*
     * q=UINT32_MAX: maximal representable uint32_t, type boundary. mu is
     * floor(2^64/q); the +1 branch does not fire.
     */
    rc = ntt_scalar_barrett_mu(UINT32_MAX);
    assert_int_equal(rc, UINT64_MAX / UINT32_MAX);
}

/** @brief Boundary operands cross-checked against the high-word reference. */
static void torture_ntt_scalar_mulhi(void **state)
{
    const uint64_t edge[] = {
        0,
        1,
        2,
        0xFFFFFFFFu,
        0x100000000ull,
        0x0000000100000001ull,
        0x7FFFFFFFFFFFFFFFull,
        0x8000000000000000ull,
        0xFFFFFFFF00000000ull,
        UINT64_MAX,
    };
    const uint64_t n = sizeof(edge) / sizeof(edge[0]);

    for (uint64_t i = 0; i < n; i++) {
        for (uint64_t j = 0; j < n; j++) {
            uint64_t a = edge[i];
            uint64_t b = edge[j];
            assert_int_equal(scalar_mulhi_u64(a, b), ref_mulhi_u64(a, b));
        }
    }
}

/** @brief Differential: random operands compared against the reference. */
static void torture_ntt_scalar_mulhi_random(void **state)
{
    const uint32_t iterations = test_iterations(2000);

    for (uint32_t i = 0; i < iterations; i++) {
        uint64_t a = prng_next_u64();
        uint64_t b = prng_next_u64();
        assert_int_equal(scalar_mulhi_u64(a, b), ref_mulhi_u64(a, b));
    }
}

/** @brief Checks in-domain (x < q^2) boundary and random fixed vectors. */
static void torture_ntt_scalar_barrett_reduce_u64(void **state)
{
    for (uint64_t i = 0; i < BARRETT_REDUCE_VECTORS_COUNT; i++) {
        const barrett_reduce_vector *v = &BARRETT_REDUCE_VECTORS[i];
        uint64_t rc = ntt_scalar_barrett_reduce_u64(v->x, v->q, v->mu);
        assert_int_equal(rc, v->expected);
    }
}

/** @brief Differential: random in-domain x compared against x % q. */
static void torture_ntt_scalar_barrett_reduce_u64_random(void **state)
{
    const uint32_t iterations = test_iterations(2000);

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);
        uint64_t q2 = q_squared(q);

        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t x = prng_next_u64() % q2;
            uint64_t rc = ntt_scalar_barrett_reduce_u64(x, q, mu);
            assert_int_equal(rc, x % q);
        }
    }
}

/** @brief Negative: out-of-domain fixed vectors (x >= q^2). */
static void torture_ntt_scalar_barrett_reduce_u64_negative(void **state)
{
    for (uint64_t i = 0; i < BARRETT_REDUCE_NEGATIVE_VECTORS_COUNT; i++) {
        const barrett_reduce_vector *v = &BARRETT_REDUCE_NEGATIVE_VECTORS[i];
        uint64_t rc = ntt_scalar_barrett_reduce_u64(v->x, v->q, v->mu);
        assert_int_equal(rc, v->expected);
    }
}

/** @brief Negative: differential for out-of-domain x, incl. UINT64_MAX. */
static void torture_ntt_scalar_barrett_reduce_u64_random_negative(void **state)
{
    const uint32_t iterations = test_iterations(2000);

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);
        uint64_t q2 = q_squared(q);
        uint64_t span = UINT64_MAX - q2;

        for (uint32_t k = 0; k < iterations; k++) {
            uint64_t x = q2 + (prng_next_u64() % span);
            uint64_t rc = ntt_scalar_barrett_reduce_u64(x, q, mu);
            assert_int_equal(rc, x % q);
        }
        uint64_t rc = ntt_scalar_barrett_reduce_u64(UINT64_MAX, q, mu);
        assert_int_equal(rc, UINT64_MAX % q);
    }
}

/** @brief Checks canonical-operand fixed vectors against (a*b) mod q. */
static void torture_ntt_scalar_barrett_mul(void **state)
{
    for (uint64_t i = 0; i < BARRETT_MUL_VECTORS_COUNT; i++) {
        const barrett_mul_vector *v = &BARRETT_MUL_VECTORS[i];
        uint32_t rc = ntt_scalar_barrett_mul(v->a, v->b, v->q, v->mu);
        assert_int_equal(rc, v->expected);
    }
}

/** @brief Differential: random canonical operands against (a*b) mod q. */
static void torture_ntt_scalar_barrett_mul_random(void **state)
{
    const uint32_t iterations = test_iterations(2000);

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t a = prng_next_u32() % q;
            uint32_t b = prng_next_u32() % q;
            uint32_t rc = ntt_scalar_barrett_mul(a, b, q, mu);
            assert_int_equal(rc, (uint64_t)a * b % q);
        }
    }
}

/** @brief Negative: non-canonical operands (a, b >= q) still match %. */
static void torture_ntt_scalar_barrett_mul_negative(void **state)
{
    const uint32_t iterations = test_iterations(2000);
    const uint64_t mu = ntt_scalar_barrett_mu(113);

    /* Explicit out-of-domain literals for the q=113 case. */
    uint32_t rc = ntt_scalar_barrett_mul(200, 200, 113, mu);
    assert_int_equal(rc, 200u * 200 % 113);
    rc = ntt_scalar_barrett_mul(UINT32_MAX, UINT32_MAX, 113, mu);
    assert_int_equal(rc, (uint64_t)UINT32_MAX * UINT32_MAX % 113);

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t muq = ntt_scalar_barrett_mu(q);
        uint32_t span = UINT32_MAX - q + 1u;

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t a = q + (prng_next_u32() % span);
            uint32_t b = q + (prng_next_u32() % span);
            uint32_t rc = ntt_scalar_barrett_mul(a, b, q, muq);
            assert_int_equal(rc, (uint64_t)a * b % q);
        }
    }
}

/*
 * ntt_scalar_barrett_modpow
 */

/** @brief Checks fixed modpow vectors generated by the Python reference. */
static void torture_ntt_scalar_barrett_modpow(void **state)
{
    for (uint64_t i = 0; i < BARRETT_MODPOW_VECTORS_COUNT; i++) {
        const barrett_modpow_vector *v = &BARRETT_MODPOW_VECTORS[i];
        uint32_t rc = ntt_scalar_barrett_modpow(v->base, v->exp, v->q, v->mu);
        assert_int_equal(rc, v->expected);
    }
}

/** @brief Differential: random base/exponent against a %-based reference. */
static void torture_ntt_scalar_barrett_modpow_random(void **state)
{
    const uint32_t iterations = test_iterations(200);

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t base = prng_next_u32() % q;
            uint32_t exp = prng_next_u32();
            uint32_t rc = ntt_scalar_barrett_modpow(base, exp, q, mu);
            assert_int_equal(rc, ref_modpow(base, exp, q));
        }
    }
}

/** @brief Fermat: a^(q-1) = 1 for every odd prime q and a != 0. */
static void torture_ntt_scalar_barrett_modpow_fermat(void **state)
{
    const uint32_t iterations = test_iterations(50);

    for (uint64_t i = 0; i < PRIME_MODULI_COUNT; i++) {
        uint32_t q = PRIME_MODULI[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t a = 1u + (prng_next_u32() % (q - 1u));
            uint32_t rc = ntt_scalar_barrett_modpow(a, q - 1u, q, mu);
            assert_int_equal(rc, 1u);
        }
    }
}

/** @brief Modular inverse: a * a^(q-2) = 1 for every odd prime q. */
static void torture_ntt_scalar_barrett_modpow_inverse(void **state)
{
    const uint32_t iterations = test_iterations(50);

    for (uint64_t i = 0; i < PRIME_MODULI_COUNT; i++) {
        uint32_t q = PRIME_MODULI[i];
        uint64_t mu = ntt_scalar_barrett_mu(q);

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t a = 1u + (prng_next_u32() % (q - 1u));
            uint32_t inv = ntt_scalar_barrett_modpow(a, q - 2u, q, mu);
            uint32_t rc = ntt_scalar_barrett_mul(a, inv, q, mu);
            assert_int_equal(rc, 1u);
        }
    }
}

/** @brief Negative: out-of-domain base, 0^0 and maximal exponent. */
static void torture_ntt_scalar_barrett_modpow_negative(void **state)
{
    const uint32_t iterations = test_iterations(200);
    const uint64_t mu = ntt_scalar_barrett_mu(113);

    /* 0^0 follows the square-and-multiply convention: identity. */
    uint32_t rc = ntt_scalar_barrett_modpow(0, 0, 113, mu);
    assert_int_equal(rc, 1u);

    /* Out-of-domain base and maximal exponent match the %-based reference. */
    rc = ntt_scalar_barrett_modpow(0xFFFFFFFFu, 0xFFFFFFFFu, 113, mu);
    assert_int_equal(rc, ref_modpow(0xFFFFFFFFu, 0xFFFFFFFFu, 113));

    for (uint64_t i = 0; i < MODULUS_SWEEP_COUNT; i++) {
        uint32_t q = MODULUS_SWEEP[i];
        uint64_t muq = ntt_scalar_barrett_mu(q);
        uint32_t span = UINT32_MAX - q + 1u;

        for (uint32_t k = 0; k < iterations; k++) {
            uint32_t base = q + (prng_next_u32() % span);
            uint32_t exp = prng_next_u32();
            uint32_t rc = ntt_scalar_barrett_modpow(base, exp, q, muq);
            assert_int_equal(rc, ref_modpow(base, exp, q));
        }
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_scalar_barrett_mu),
        cmocka_unit_test(torture_ntt_scalar_barrett_mu_bounds),
        cmocka_unit_test(torture_ntt_scalar_barrett_mu_negative),
        cmocka_unit_test(torture_ntt_scalar_mulhi),
        cmocka_unit_test(torture_ntt_scalar_mulhi_random),
        cmocka_unit_test(torture_ntt_scalar_barrett_reduce_u64),
        cmocka_unit_test(torture_ntt_scalar_barrett_reduce_u64_random),
        cmocka_unit_test(torture_ntt_scalar_barrett_reduce_u64_negative),
        cmocka_unit_test(torture_ntt_scalar_barrett_reduce_u64_random_negative),
        cmocka_unit_test(torture_ntt_scalar_barrett_mul_random),
        cmocka_unit_test(torture_ntt_scalar_barrett_mul_negative),
        cmocka_unit_test(torture_ntt_scalar_barrett_modpow),
        cmocka_unit_test(torture_ntt_scalar_barrett_modpow_random),
        cmocka_unit_test(torture_ntt_scalar_barrett_modpow_fermat),
        cmocka_unit_test(torture_ntt_scalar_barrett_modpow_inverse),
        cmocka_unit_test(torture_ntt_scalar_barrett_modpow_negative),
    };

    prng_seed();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
