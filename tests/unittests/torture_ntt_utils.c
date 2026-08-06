#include "ntt_utils.c"
#include "ref_arith.h"
#include "test_common.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

/* Deterministic PRNG (shared splitmix64 from test_common.h) */
static uint64_t prng_state = UINT64_C(0x9E3779B97F4A7C15);

/* Known values */
static const uint64_t SMALL_PRIMES[] = {
    2ull,
    3ull,
    5ull,
    7ull,
    11ull,
    13ull,
    17ull,
    19ull,
    23ull,
    29ull,
    31ull,
    37ull,
    41ull,
    43ull,
    47ull,
};
#define SMALL_PRIMES_COUNT (sizeof(SMALL_PRIMES) / sizeof(SMALL_PRIMES[0]))

#define M61        UINT64_C(2305843009213693951)  /* 2^61 - 1        */
#define GOLDILOCKS UINT64_C(18446744069414584321) /* 2^64-2^32+1     */
#define BIG_POW2   UINT64_C(4179340454199820289)  /* 29*2^57 + 1     */
#define Q_MINUS59  UINT64_C(18446744073709551557) /* 2^64 - 59       */
#define Q_PLUS15   UINT64_C(4294967311)           /* 2^32 + 15       */
#define Q_MINUS5   UINT64_C(4294967291)           /* 2^32 - 5        */
#define Q_31MINUS1 UINT64_C(2147483647)           /* 2^31 - 1        */

/* Primes spanning the uint64 domain. */
static const uint64_t PRIME_MODULI[] = {
    3ull,
    7ull,
    113ull,
    12289ull,
    998244353ull,
    Q_31MINUS1,
    Q_MINUS5,
    Q_PLUS15,
    M61,
    GOLDILOCKS,
    BIG_POW2,
    Q_MINUS59,
};
#define PRIME_MODULI_COUNT (sizeof(PRIME_MODULI) / sizeof(PRIME_MODULI[0]))

/* Composite values covering edge shapes of the uint64 domain. */
static const uint64_t COMPOSITES[] = {
    4ull,
    9ull,
    25ull,
    49ull,
    341ull,                        /* 11 * 31                      */
    2047ull,                       /* 23 * 89                      */
    UINT64_C(4294967295),          /* 2^32-1 = 3*5*17*257*65537    */
    UINT64_C(1) << 63,             /* pure power of two            */
    UINT64_C(4611686014132420609), /* (2^31-1)^2                   */
    UINT64_C(9223372021822390277), /* (2^32-5)*(2^31-1)            */
    UINT64_C(6917529027641081853), /* 3 * M61                      */
    UINT64_C(4611686018427387902), /* 2 * M61                      */
};
#define COMPOSITES_COUNT (sizeof(COMPOSITES) / sizeof(COMPOSITES[0]))

/* Explicit-root handling: q = 12 * 2^10 + 1, omega = psi^2 (mod q). */
#define RESOLVE_Q     UINT64_C(12289)
#define RESOLVE_N     256u
#define RESOLVE_OMEGA UINT64_C(8340)
#define RESOLVE_PSI   UINT64_C(3400)

static int cmp_u64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a;
    const uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/*
 * Asserts that x factors exactly into the given (unsorted) distinct prime
 * set, and that every reported factor is prime, distinct and divides x.
 */
static void assert_factors(uint64_t x, const uint64_t *expected, size_t n)
{
    uint64_t factors[32];
    uint64_t want[32];
    size_t count = 0;

    assert_true(n <= 32);
    assert_true(ntt__distinct_prime_factors(x, factors, 32, &count));
    assert_int_equal(count, n);

    for (size_t i = 0; i < count; i++) {
        assert_true(ntt_is_prime(factors[i]));
        assert_true(x % factors[i] == 0);
        if (i > 0) {
            assert_true(factors[i] != factors[i - 1]);
        }
    }

    memcpy(want, expected, n * sizeof(want[0]));
    qsort(factors, count, sizeof(factors[0]), cmp_u64);
    qsort(want, n, sizeof(want[0]), cmp_u64);
    for (size_t i = 0; i < n; i++) {
        assert_int_equal(factors[i], want[i]);
    }
}

/* Public API unittests */
static void torture_ntt_is_power_of_two(void **state)
{
    (void)state;
    assert_false(ntt_is_power_of_two(0));
    assert_true(ntt_is_power_of_two(1));
    assert_true(ntt_is_power_of_two(2));
    assert_false(ntt_is_power_of_two(3));
    assert_true(ntt_is_power_of_two(1024));
    assert_true(ntt_is_power_of_two(UINT32_C(1) << 31));
    assert_false(ntt_is_power_of_two((UINT32_C(1) << 31) - 1));
}

static void torture_ntt_reverse_bits(void **state)
{
    (void)state;
    assert_int_equal(ntt_reverse_bits(1, 4), 8);
    assert_int_equal(ntt_reverse_bits(0b1010u, 4), 0b0101u);
    assert_int_equal(ntt_reverse_bits(2, 8), 64);
    assert_int_equal(ntt_reverse_bits(0, 31), 0);
    assert_int_equal(ntt_reverse_bits(1, 31), UINT32_C(1) << 30);

    /* Round-trip: reversing twice restores the value. */
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t x = ntt_test_prng_range(&prng_state, UINT32_C(1) << 20);
        assert_int_equal(ntt_reverse_bits(ntt_reverse_bits(x, 20), 20), x);
    }
}

/* Modular arithmetic helpers (static) */
static void torture_addmod_u64(void **state)
{
    (void)state;

    /* Wrap-around behaviour near 2^64: q is close to UINT64_MAX. */
    assert_int_equal(addmod_u64(GOLDILOCKS - 1, 1, GOLDILOCKS), 0);
    assert_int_equal(addmod_u64(GOLDILOCKS - 1, GOLDILOCKS - 1, GOLDILOCKS),
                     GOLDILOCKS - 2);
    assert_int_equal(addmod_u64(0, 0, GOLDILOCKS), 0);

    for (size_t m = 0; m < PRIME_MODULI_COUNT; m++) {
        uint64_t q = PRIME_MODULI[m];
        for (uint32_t i = 0; i < 2000; i++) {
            uint64_t a = ntt_test_prng_range(&prng_state, q);
            uint64_t b = ntt_test_prng_range(&prng_state, q);
            assert_int_equal(addmod_u64(a, b, q),
                             ntt_test_ref_add_u64(a, b, q));
        }
    }
}

static void torture_mulmod(void **state)
{
    (void)state;
    for (size_t m = 0; m < PRIME_MODULI_COUNT; m++) {
        uint64_t q = PRIME_MODULI[m];
        for (uint32_t i = 0; i < 2000; i++) {
            uint64_t a = ntt_test_prng_range(&prng_state, q);
            uint64_t b = ntt_test_prng_range(&prng_state, q);
            assert_int_equal(mulmod(a, b, q), ntt_test_ref_mul_u64(a, b, q));
        }
    }

    /* Degenerate cases: operands outside [0, q) are reduced first. */
    assert_int_equal(mulmod(GOLDILOCKS, GOLDILOCKS, GOLDILOCKS), 0);
    assert_int_equal(mulmod(GOLDILOCKS - 1, GOLDILOCKS - 1, GOLDILOCKS), 1);
}

static void torture_modpow(void **state)
{
    (void)state;
    for (size_t m = 0; m < PRIME_MODULI_COUNT; m++) {
        uint64_t q = PRIME_MODULI[m];
        for (uint32_t i = 0; i < 1000; i++) {
            uint64_t a = ntt_test_prng_range(&prng_state, q);
            uint64_t e = ntt_test_prng_range(&prng_state, q);
            assert_int_equal(modpow(a, e, q), ntt_test_ref_modpow_u64(a, e, q));
        }
    }

    /* Fermat's little theorem: a^(q-1) == 1 for a != 0 mod prime q. */
    for (size_t m = 0; m < PRIME_MODULI_COUNT; m++) {
        uint64_t q = PRIME_MODULI[m];
        uint64_t a = 1 + ntt_test_prng_range(&prng_state, q - 1);
        assert_int_equal(modpow(a, q - 1, q), 1);
    }
}

static void torture_ntt__gcd_u64(void **state)
{
    (void)state;
    assert_int_equal(ntt__gcd_u64(12, 8), 4);
    assert_int_equal(ntt__gcd_u64(17, 5), 1);
    assert_int_equal(ntt__gcd_u64(0, 5), 5);
    assert_int_equal(ntt__gcd_u64(5, 0), 5);
    assert_int_equal(ntt__gcd_u64(1, 100), 1);
    assert_int_equal(ntt__gcd_u64(GOLDILOCKS - 1, UINT64_C(1) << 32),
                     UINT64_C(1) << 32);
}

/* Static Miller-Rabin core and public ntt_is_prime() */
static void torture_ntt__miller_rabin(void **state)
{
    (void)state;
    for (size_t i = 0; i < PRIME_MODULI_COUNT; i++) {
        uint64_t q = PRIME_MODULI[i];
        uint64_t witness = 0;
        assert_true(ntt__miller_rabin(q, &witness));
        assert_true(ntt_is_prime(q));
    }

    for (size_t i = 0; i < SMALL_PRIMES_COUNT; i++) {
        assert_true(ntt_is_prime(SMALL_PRIMES[i]));
    }

    for (size_t i = 0; i < COMPOSITES_COUNT; i++) {
        uint64_t c = COMPOSITES[i];
        uint64_t witness = 0;
        assert_false(ntt__miller_rabin(c, &witness));
        assert_false(ntt_is_prime(c));
    }

    assert_false(ntt_is_prime(0));
    assert_false(ntt_is_prime(1));
    assert_false(ntt_is_prime(GOLDILOCKS - 1)); /* even */
}

/* Pollard's rho (static) */
static void torture_ntt__pollard_rho(void **state)
{
    (void)state;
    for (size_t i = 0; i < COMPOSITES_COUNT; i++) {
        uint64_t c = COMPOSITES[i];
        if (ntt__miller_rabin(c, NULL)) {
            continue;
        }
        uint64_t d = ntt__pollard_rho(c);
        assert_true(d > 1);
        assert_true(d < c);
        assert_true(c % d == 0);
    }
}

/* Modular square root (static) */
static void torture_ntt__sqrt_mod(void **state)
{
    (void)state;

    for (size_t m = 0; m < PRIME_MODULI_COUNT; m++) {
        uint64_t q = PRIME_MODULI[m];

        /* Every square of a random residue must have a verified root. */
        for (uint32_t i = 0; i < 200; i++) {
            uint64_t s0 = 1 + ntt_test_prng_range(&prng_state, q - 1);
            uint64_t a = mulmod(s0, s0, q);
            uint64_t s = 0;
            assert_true(ntt__sqrt_mod(a, q, &s));
            assert_int_equal(mulmod(s, s, q), a);
        }

        /* A quadratic non-residue must be rejected. */
        uint64_t z = 2;
        while (z < q && modpow(z, (q - 1) / 2, q) != q - 1) {
            z++;
        }
        if (z < q) {
            uint64_t s = 0;
            assert_false(ntt__sqrt_mod(z, q, &s));
        }
    }

    /* Zero is always its own square root. */
    uint64_t s = 0;
    assert_true(ntt__sqrt_mod(0, 7, &s));
    assert_int_equal(s, 0);
}

/* Primitive-root search (static) */
static void assert_full_generator(uint64_t q, uint64_t g)
{
    assert_true(g >= 2);
    assert_true(g < q);
    assert_int_equal(modpow(g, q - 1, q), 1);

    uint64_t factors[32];
    size_t nf = 0;
    assert_true(ntt__distinct_prime_factors(q - 1, factors, 32, &nf));
    for (size_t i = 0; i < nf; i++) {
        assert_true(modpow(g, (q - 1) / factors[i], q) != 1);
    }
}

static void torture_ntt__find_primitive_root(void **state)
{
    (void)state;
    for (size_t i = 0; i < PRIME_MODULI_COUNT; i++) {
        uint64_t g = 0;
        assert_true(ntt__find_primitive_root(PRIME_MODULI[i], &g));
        assert_full_generator(PRIME_MODULI[i], g);
    }

    /* Invalid inputs are rejected. */
    uint64_t g = 0;
    assert_false(ntt__find_primitive_root(0, &g));
    assert_false(ntt__find_primitive_root(2, &g));
    assert_false(ntt__find_primitive_root(9, NULL));
}

/* Known-moduli table */
static void torture_ntt__known_modulus_factors_lookup(void **state)
{
    (void)state;
    static const uint64_t mlkem[] = {2, 13};
    static const uint64_t mldsa[] = {2, 3, 11, 31};
    static const uint64_t gold[] = {2, 3, 5, 17, 257, 65537};

    assert_factors(UINT64_C(3329) - 1, mlkem, 2);
    assert_factors(UINT64_C(8380417) - 1, mldsa, 4);
    assert_factors(GOLDILOCKS - 1, gold, 6);

    uint64_t factors[32];
    size_t count = 0;
    assert_false(ntt__known_modulus_factors_lookup(9999, factors, 32, &count));
}

/* Distinct prime factorization (internal) */
static void torture_ntt__distinct_prime_factors_rejects_invalid(void **state)
{
    (void)state;
    uint64_t factors[32];
    size_t count = 0;

    assert_false(ntt__distinct_prime_factors(0, factors, 32, &count));
    assert_false(ntt__distinct_prime_factors(1, factors, 32, &count));
}

static void torture_ntt__distinct_prime_factors_pure_power_of_two(void **state)
{
    (void)state;
    static const uint64_t expected[] = {2};
    assert_factors(UINT64_C(1) << 63, expected, 1);
    assert_factors(UINT64_C(1) << 32, expected, 1);
    assert_factors(UINT64_C(2), expected, 1);
}

static void torture_ntt__distinct_prime_factors_primes(void **state)
{
    (void)state;
    uint64_t x = 1;
    for (size_t i = 0; i < SMALL_PRIMES_COUNT; i++) {
        x *= SMALL_PRIMES[i];
    }
    assert_factors(x, SMALL_PRIMES, SMALL_PRIMES_COUNT);
}

static void torture_ntt__distinct_prime_factors_square(void **state)
{
    (void)state;
    static const uint64_t expected[] = {UINT64_C(2147483647)};
    /* (2^31 - 1)^2 = 2^62 - 2^32 + 1, a 63-bit perfect square. */
    assert_factors(UINT64_C(4611686014132420609), expected, 1);
}

static void torture_ntt__distinct_prime_factors_two_large_primes(void **state)
{
    (void)state;
    /* (2^32 - 5) * (2^31 - 1), two ~31/32-bit primes, split by Pollard rho. */
    static const uint64_t expected[] = {UINT64_C(4294967291),
                                        UINT64_C(2147483647)};
    assert_factors(UINT64_C(9223372021822390277), expected, 2);
}

static void torture_ntt__distinct_prime_factors_large_prime_factor(void **state)
{
    (void)state;
    static const uint64_t expected_m61[] = {M61};
    assert_factors(M61, expected_m61, 1);

    /* A prime factor well above 2^32 must fit and be reported. */
    static const uint64_t expected_2m[] = {UINT64_C(2), M61};
    assert_factors(UINT64_C(4611686018427387902), expected_2m, 2); /* 2*M61 */

    static const uint64_t expected_3m[] = {UINT64_C(3), M61};
    assert_factors(UINT64_C(6917529027641081853), expected_3m, 2); /* 3*M61 */
}

static void torture_ntt__distinct_prime_factors_products(void **state)
{
    (void)state;
    /* 2^32 - 1 = 3 * 5 * 17 * 257 * 65537. */
    static const uint64_t p32m1[] = {3, 5, 17, 257, 65537};
    assert_factors(UINT64_C(4294967295), p32m1, 5);

    /*
     * 2^60 - 1 = 3^2 * 5^2 * 7 * 11 * 13 * 31 * 41 * 61 * 151 * 331 * 1321;
     * eleven distinct prime factors, the largest well above 32 bits.
     */
    static const uint64_t p60m1[] =
        {3, 5, 7, 11, 13, 31, 41, 61, 151, 331, 1321};
    assert_factors(UINT64_C(1152921504606846975), p60m1, 11);
}

/* Root derivation end-to-end */
static void torture_ntt__resolve_roots_generic_32bit(void **state)
{
    (void)state;
    /* q = 2^32 + 15, 33-bit prime not in the known-moduli table. */
    static const uint64_t q = Q_PLUS15;
    static const uint32_t n = 2;
    uint64_t omega = 0, psi = 0;

    assert_true(ntt__resolve_roots(q, n, NTT_TRANSFORM_CYCLIC, &omega, &psi));
    assert_true(ntt__is_primitive_root_of_order(omega, n, q));
}

static void torture_ntt__resolve_roots_generic_64bit(void **state)
{
    (void)state;
    /* 2^61 - 1: q - 1 = 2 * (2^60 - 1), supports cyclic size 2. */
    static const uint64_t q = M61;
    static const uint32_t n = 2;
    uint64_t omega = 0, psi = 0;

    assert_true(ntt__resolve_roots(q, n, NTT_TRANSFORM_CYCLIC, &omega, &psi));
    assert_true(ntt__is_primitive_root_of_order(omega, n, q));
}

static void torture_ntt__resolve_roots_generic_power_of_two(void **state)
{
    (void)state;
    /*
     * 29 * 2^57 + 1, a 64-bit prime with a high power-of-two factor in q - 1.
     * Not in the known-moduli table, so this exercises the generic path for
     * a real large NTT modulus.
     */
    static const uint64_t q = BIG_POW2;
    static const uint32_t n = UINT32_C(1) << 30;

    uint64_t omega = 0, psi = 0;
    assert_true(ntt__resolve_roots(q, n, NTT_TRANSFORM_CYCLIC, &omega, &psi));
    assert_true(ntt__is_primitive_root_of_order(omega, n, q));

    omega = psi = 0;
    assert_true(
        ntt__resolve_roots(q, n >> 1, NTT_TRANSFORM_NEGACYCLIC, &omega, &psi));
    assert_true(ntt__is_primitive_root_of_order(omega, n >> 1, q));
    assert_true(ntt__is_primitive_root_of_order(psi, n, q));
}

static void torture_ntt__resolve_roots_known_table_unaffected(void **state)
{
    (void)state;
    /* ML-KEM q = 3329; the table fast path must keep working. */
    static const uint64_t q = UINT64_C(3329);
    static const uint32_t n = 16;
    uint64_t omega = 0, psi = 0;

    assert_true(ntt__resolve_roots(q, n, NTT_TRANSFORM_CYCLIC, &omega, &psi));
    assert_true(ntt__is_primitive_root_of_order(omega, n, q));
}

static void torture_ntt__resolve_roots_cyclic_rejects_psi(void **state)
{
    (void)state;
    uint64_t omega = 0, psi = RESOLVE_PSI;

    assert_false(ntt__resolve_roots(RESOLVE_Q,
                                    RESOLVE_N,
                                    NTT_TRANSFORM_CYCLIC,
                                    &omega,
                                    &psi));
}

static void torture_ntt__resolve_roots_cyclic_accepts_omega(void **state)
{
    (void)state;
    uint64_t omega = RESOLVE_OMEGA, psi = 0;

    assert_true(ntt__resolve_roots(RESOLVE_Q,
                                   RESOLVE_N,
                                   NTT_TRANSFORM_CYCLIC,
                                   &omega,
                                   &psi));
    assert_int_equal(omega, RESOLVE_OMEGA);
    assert_int_equal(psi, 0);
}

static void
torture_ntt__resolve_roots_cyclic_rejects_invalid_omega(void **state)
{
    (void)state;
    uint64_t omega = 1, psi = 0;

    assert_false(ntt__resolve_roots(RESOLVE_Q,
                                    RESOLVE_N,
                                    NTT_TRANSFORM_CYCLIC,
                                    &omega,
                                    &psi));
}

static void torture_ntt__resolve_roots_negacyclic_accepts_psi(void **state)
{
    (void)state;
    uint64_t omega = 0, psi = RESOLVE_PSI;

    assert_true(ntt__resolve_roots(RESOLVE_Q,
                                   RESOLVE_N,
                                   NTT_TRANSFORM_NEGACYCLIC,
                                   &omega,
                                   &psi));
    assert_int_equal(omega, RESOLVE_OMEGA);
    assert_int_equal(psi, RESOLVE_PSI);
}

static void
torture_ntt__resolve_roots_negacyclic_accepts_omega_psi(void **state)
{
    (void)state;
    uint64_t omega = RESOLVE_OMEGA, psi = RESOLVE_PSI;

    assert_true(ntt__resolve_roots(RESOLVE_Q,
                                   RESOLVE_N,
                                   NTT_TRANSFORM_NEGACYCLIC,
                                   &omega,
                                   &psi));
}

static void
torture_ntt__resolve_roots_negacyclic_rejects_mismatched_roots(void **state)
{
    (void)state;
    uint64_t omega = RESOLVE_OMEGA, psi = 1;

    assert_false(ntt__resolve_roots(RESOLVE_Q,
                                    RESOLVE_N,
                                    NTT_TRANSFORM_NEGACYCLIC,
                                    &omega,
                                    &psi));
}

static void
torture_ntt__resolve_roots_negacyclic_rejects_invalid_psi(void **state)
{
    (void)state;
    /* psi = 1 is not a primitive 2n-th root of unity. */
    uint64_t omega = 0, psi = 1;

    assert_false(ntt__resolve_roots(RESOLVE_Q,
                                    RESOLVE_N,
                                    NTT_TRANSFORM_NEGACYCLIC,
                                    &omega,
                                    &psi));
}

/* Root-order validity checks */
static void torture_ntt__is_primitive_root_of_order(void **state)
{
    (void)state;
    /* 1 is a primitive root of order 1 (mod any q). */
    assert_true(ntt__is_primitive_root_of_order(1, 1, 7));

    /* Mod 5 the group has order 4; 2 is a primitive root. */
    assert_true(ntt__is_primitive_root_of_order(2, 4, 5));
    assert_false(ntt__is_primitive_root_of_order(4, 4, 5));
    assert_false(ntt__is_primitive_root_of_order(2, 2, 5));
    assert_false(ntt__is_primitive_root_of_order(0, 4, 5));
    assert_false(ntt__is_primitive_root_of_order(2, 0, 5));
}

static void torture_ntt__validate_transform_params(void **state)
{
    (void)state;
    /* q - 1 = 16, so 2^k divides 16 for k <= 4. */
    assert_true(ntt__validate_transform_params(17, 16, NTT_TRANSFORM_CYCLIC));
    assert_true(ntt__validate_transform_params(17, 8, NTT_TRANSFORM_CYCLIC));
    assert_true(
        ntt__validate_transform_params(17, 8, NTT_TRANSFORM_NEGACYCLIC));
    assert_true(
        ntt__validate_transform_params(17, 4, NTT_TRANSFORM_NEGACYCLIC));

    /* Required order does not divide q - 1. */
    assert_false(ntt__validate_transform_params(17, 32, NTT_TRANSFORM_CYCLIC));
    assert_false(
        ntt__validate_transform_params(17, 16, NTT_TRANSFORM_NEGACYCLIC));

    /* Size must be a nonzero power of two. */
    assert_false(ntt__validate_transform_params(17, 12, NTT_TRANSFORM_CYCLIC));
    assert_false(ntt__validate_transform_params(17, 0, NTT_TRANSFORM_CYCLIC));

    /* Modulus must be greater than two. */
    assert_false(ntt__validate_transform_params(0, 16, NTT_TRANSFORM_CYCLIC));
    assert_false(ntt__validate_transform_params(2, 4, NTT_TRANSFORM_CYCLIC));

    /* Unknown transform type. */
    assert_false(ntt__validate_transform_params(17, 4, (ntt_transform_type)99));
}

int main(void)
{
    ntt_test_set_log_level();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(torture_ntt_is_power_of_two),
        cmocka_unit_test(torture_ntt_reverse_bits),
        cmocka_unit_test(torture_addmod_u64),
        cmocka_unit_test(torture_mulmod),
        cmocka_unit_test(torture_modpow),
        cmocka_unit_test(torture_ntt__gcd_u64),
        cmocka_unit_test(torture_ntt__miller_rabin),
        cmocka_unit_test(torture_ntt__pollard_rho),
        cmocka_unit_test(torture_ntt__sqrt_mod),
        cmocka_unit_test(torture_ntt__find_primitive_root),
        cmocka_unit_test(torture_ntt__known_modulus_factors_lookup),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_rejects_invalid),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_pure_power_of_two),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_primes),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_square),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_two_large_primes),
        cmocka_unit_test(
            torture_ntt__distinct_prime_factors_large_prime_factor),
        cmocka_unit_test(torture_ntt__distinct_prime_factors_products),
        cmocka_unit_test(torture_ntt__resolve_roots_generic_32bit),
        cmocka_unit_test(torture_ntt__resolve_roots_generic_64bit),
        cmocka_unit_test(torture_ntt__resolve_roots_generic_power_of_two),
        cmocka_unit_test(torture_ntt__resolve_roots_known_table_unaffected),
        cmocka_unit_test(torture_ntt__resolve_roots_cyclic_rejects_psi),
        cmocka_unit_test(torture_ntt__resolve_roots_cyclic_accepts_omega),
        cmocka_unit_test(
            torture_ntt__resolve_roots_cyclic_rejects_invalid_omega),
        cmocka_unit_test(torture_ntt__resolve_roots_negacyclic_accepts_psi),
        cmocka_unit_test(
            torture_ntt__resolve_roots_negacyclic_accepts_omega_psi),
        cmocka_unit_test(
            torture_ntt__resolve_roots_negacyclic_rejects_mismatched_roots),
        cmocka_unit_test(
            torture_ntt__resolve_roots_negacyclic_rejects_invalid_psi),
        cmocka_unit_test(torture_ntt__is_primitive_root_of_order),
        cmocka_unit_test(torture_ntt__validate_transform_params),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
