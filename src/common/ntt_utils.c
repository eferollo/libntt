/*
 * ntt_utils.c
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

#include "ntt/ntt_log.h"
#include "ntt_internal.h"
#include <stddef.h>

/**
 * @brief Precomputed distinct prime factorizations of q-1 for moduli this
 *        library may actually be used with in practice (e.g., Kyber,
 *        Dilithium).
 *
 * Trial division on q-1 is O(sqrt(q)), which is fine if not executed in a hot
 * path, but pointless to redo on every ntt_create() call for a modulus whose
 * factorization is public and fixed. Anything not listed here falls back to
 * ntt__distinct_prime_factors() so arbitrary user-supplied moduli still work.
 */
typedef struct {
    uint32_t q;
    uint32_t factors[32];
    size_t count;
} ntt_qm1_factors;

static const ntt_qm1_factors ntt_known_moduli[] = {
    /* q = 3329,    ML-KEM, q-1 = 3328 = 2^8 * 13 */
    {3329, {2, 13}, 2},
    /* q = 8380417, ML-DSA, q-1 = 8380416 = 2^13 * 3 * 11 * 31 */
    {8380417, {2, 3, 11, 31}, 4},
};

/*
 * Deliberately self-contained: this module runs once per ntt_create(), on
 * plain uint32_t canonical values, regardless of which adapter/arithmetic
 * representation is eventually selected. Reusing an adapter's mulmod here
 * would create a dependency in the wrong direction (common code depending
 * on adapter code) for no benefit, since none of this is a hot path.
 */
static uint32_t mulmod(uint32_t a, uint32_t b, uint32_t q)
{
    return (uint32_t)(((uint64_t)a * (uint64_t)b) % q);
}

static uint32_t modpow(uint32_t base, uint32_t exp, uint32_t q)
{
    uint32_t result = 1;
    base %= q;
    while (exp > 0) {
        if (exp & 1u) {
            result = mulmod(result, base, q);
        }

        base = mulmod(base, base, q);
        exp >>= 1;
    }

    return result;
}

/**
 * @brief Finds the distinct prime factors of x via trial division.
 *
 * Trial division up to sqrt(x) is sufficient here: x is always q-1 for a
 * 32-bit prime q, so x fits comfortably in 32 bits and this runs once per
 * ntt_create(), not in any hot path.
 *
 * @param[in]  x           The positive integer to factorize.
 * @param[out] factors     Array where the distinct prime factors are stored.
 * @param[in]  max_factors Maximum number of factors that can be stored in
 *                         factors array.
 * @param[out] count       Stores the number of distinct prime factors found.
 *
 * @return true if all distinct prime factors fit in the provided array.
 * @return false if max_factors is too small.
 */
static bool ntt__distinct_prime_factors(uint32_t x,
                                        uint32_t *factors,
                                        size_t max_factors,
                                        size_t *count)
{
    size_t cnt = 0;

    /* Try every possible divisor p up to sqrt(x). */
    for (uint32_t p = 2; (uint64_t)p * p <= x; p++) {
        /* If p does not divide x, it cannot be a prime factor. */
        if (x % p != 0) {
            continue;
        }

        if (cnt >= max_factors) {
            return false;
        }

        /* Store p only once, since only distinct prime factors are needed. */
        factors[cnt++] = p;

        /*
         * Remove every occurrence of p from x.
         * This ensures that p is not stored again and reduces x for
         * subsequent factorization steps.
         */
        while (x % p == 0) {
            x /= p;
        }
    }

    /*
     * If x is still greater than 1, the remaining value must be a prime
     * factor. This happens when the remaining prime factor is greater than
     * sqrt(x) at the point where the loop terminates.
     */
    if (x > 1) {
        if (cnt >= max_factors) {
            return false;
        }
        factors[cnt++] = x;
    }

    *count = cnt;
    return true;
}

static bool ntt__known_modulus_factors_lookup(uint32_t q,
                                              uint32_t *factors,
                                              size_t max_factors,
                                              size_t *count)
{
    size_t table_size = sizeof(ntt_known_moduli) / sizeof(ntt_known_moduli[0]);
    for (size_t i = 0; i < table_size; i++) {
        if (ntt_known_moduli[i].q != q) {
            continue;
        }
        if (ntt_known_moduli[i].count > max_factors) {
            return false;
        }
        for (size_t j = 0; j < ntt_known_moduli[j].count; j++) {
            factors[j] = ntt_known_moduli[i].factors[j];
        }
        *count = ntt_known_moduli[i].count;
        return true;
    }
    return false;
}

/**
 * @brief Finds a primitive root (generator) of the multiplicative group
 *        modulo a prime q.
 *
 * @param[in]  q     Prime modulus.
 * @param[out] g_ptr Set to a primitive root of q on success.
 *
 * @return true on success.
 * @return false if q is not valid (e.g., q < 3) or no root was found.
 */
static bool ntt__find_primitive_root(uint32_t q, uint32_t *g_ptr)
{
    if (q < 3 || g_ptr == NULL) {
        return false;
    }

    uint32_t phi = q - 1;
    uint32_t factors[32];
    size_t n_factors = 0;

    if (!ntt__known_modulus_factors_lookup(q, factors, 32, &n_factors) &&
        !ntt__distinct_prime_factors(q, factors, 32, &n_factors)) {
        NTT_LOG(NTT_LOG_ERROR,
                "Too many distinct prime factors of q-1=%u",
                phi);
        return false;
    }

    for (uint32_t g = 2; g < q; g++) {
        bool ok = true;
        for (size_t i = 0; i < n_factors; i++) {
            if (modpow(g, phi / factors[i], q) == 1) {
                ok = false;
                break;
            }
        }
        if (ok) {
            *g_ptr = g;
            return true;
        }
    }

    return false;
}

/**
 * @brief Computes a modular square root of a modulo q.
 *
 * Finds a value x such that
 *
 *     x^2 == a (mod q)
 *
 * using Euler's criterion to test whether a is a quadratic residue and
 * either a direct exponentiation or the Tonelli-Shanks algorithm (RESSOL) to
 * compute the square root.
 *
 * The modulus q must be an odd prime. If a is a quadratic residue modulo q,
 * there are generally two square roots modulo q, x and q - x. This function
 * returns one of them.
 *
 * For primes satisfying q == 3 (mod 4), the square root can be computed
 * directly as
 *
 *     x = a^((q + 1) / 4) (mod q).
 *
 * For primes satisfying q == 1 (mod 4), the general Tonelli-Shanks
 * algorithm is used.
 *
 * @param[in]  a   The value for which the modular square root is required.
 * @param[in]  q   The odd prime modulus.
 * @param[out] out Pointer to the variable that receives the computed square
 *                 root.
 *
 * @return true if a modular square root exists and is stored in out.
 * @return false if a is not a quadratic residue modulo q.
 */
static bool ntt__sqrt_mod(uint32_t a, uint32_t q, uint32_t *out)
{
    /* Reduce a to its canonical representative modulo q. */
    a %= q;

    /* Zero is always a quadratic residue, with zero as its square root. */
    if (a == 0) {
        *out = 0;
        return true;
    }

    /*
     * Apply Euler's criterion to determine whether a is a quadratic
     * residue modulo the prime q.
     *
     * For a non-zero value a:
     *
     *     a^((q - 1) / 2) ==  1 (mod q)  -> quadratic residue
     *     a^((q - 1) / 2) == -1 (mod q)  -> quadratic non-residue
     *
     * Since q - 1 is congruent to -1 modulo q, q - 1 is used to
     * represent the result -1.
     */
    if (modpow(a, (q - 1) / 2, q) != 1) {
        return false;
    }

    /*
     * When q == 3 (mod 4), a modular square root can be computed directly
     * using the exponentiation formula:
     *
     *     sqrt(a) = a^((q + 1) / 4) (mod q).
     */
    if (q % 4 == 3) {
        *out = modpow(a, (q + 1) / 4, q);
        return true;
    }

    /*
     * For q == 1 (mod 4), use the general Tonelli-Shanks algorithm.
     *
     * First factor q - 1 as:
     *
     *     q - 1 = Q * 2^S
     *
     * where Q is odd.
     */
    uint32_t Q = q - 1;
    uint32_t S = 0;

    /* Remove all factors of two from q - 1. */
    while ((Q & 1u) == 0) {
        Q >>= 1;
        S++;
    }

    /*
     * Find a quadratic non-residue z, i.e., a value satisfying:
     *
     *     z^((q - 1) / 2) == -1 (mod q).
     *
     * Euler's criterion is used to identify such a value.
     */
    uint32_t z = 2;
    while (modpow(z, (q - 1) / 2, q) != q - 1) {
        z++;
    }

    /*
     * Initialize the Tonelli-Shanks state.
     *
     * M tracks the current power of two in the exponent.
     *
     * c = z^Q
     *
     * t = a^Q
     *
     * R = a^((Q + 1) / 2)
     *
     * The algorithm maintains the invariant:
     *
     *     R^2 == a * t (mod q)
     *
     * Therefore, once t becomes 1, R is a square root of a.
     */
    uint32_t M = S;
    uint32_t c = modpow(z, Q, q);
    uint32_t t = modpow(a, Q, q);
    uint32_t R = modpow(a, (Q + 1) / 2, q);

    /*
     * Repeatedly correct R until t becomes 1.
     *
     * While t != 1, the invariant gives:
     *
     *     R^2 == a * t (mod q)
     *
     * Once t == 1:
     *
     *     R^2 == a (mod q)
     */
    while (t != 1) {
        uint32_t i = 0;
        uint32_t tt = t;

        /*
         * Find the smallest i such that:
         *
         *     t^(2^i) == 1 (mod q).
         *
         * Repeated squaring computes:
         *
         *     t, t^2, t^4, t^8, ...
         */
        while (tt != 1) {
            tt = mulmod(tt, tt, q);
            i++;

            /*
             * This condition should never be reached when a is a valid
             * quadratic residue and the Tonelli-Shanks state is correct.
             */
            if (i == M) {
                return false;
            }
        }

        /*
         * Compute the correction factor:
         *
         *     b = c^(2^(M - i - 1)).
         *
         * Each iteration squares b, producing successive powers:
         *
         *     c, c^2, c^4, c^8, ...
         */
        uint32_t b = c;
        for (uint32_t e = 0; e < M - i - 1; e++) {
            b = mulmod(b, b, q);
        }

        /*
         * Update the Tonelli-Shanks state.
         *
         * The correction factor b reduces the power-of-two order of t
         * while preserving the invariant:
         *
         *     R^2 == a * t (mod q).
         */
        M = i;
        c = mulmod(b, b, q);
        t = mulmod(t, c, q);
        R = mulmod(R, b, q);
    }

    /*
     * At this point t == 1, so the invariant becomes:
     *
     *     R^2 == a (mod q).
     *
     * Therefore R is a modular square root of a.
     */
    *out = R;
    return true;
}

/**
 * @brief Checks whether x is a primitive root of the specified order modulo q.
 *
 * A value x is a primitive root of a given order modulo q if its
 * multiplicative order modulo q is exactly order. This requires
 * x^order = 1 (mod q), while x^d != 1 (mod q) for every proper divisor
 * d of order.
 *
 * @param[in] x     The candidate root.
 * @param[in] order The required multiplicative order of x.
 * @param[in] q     The modulus.
 *
 * @return true if x has exactly the specified multiplicative order
 *         modulo q.
 * @return false otherwise.
 */
bool ntt__is_primitive_root_of_order(uint32_t x, uint32_t order, uint32_t q)
{
    if (order == 0 || x == 0) {
        return false;
    }

    /* x must satisfy x^order = 1 (mod q). */
    if (modpow(x, order, q) != 1) {
        return false;
    }

    if (order == 1) {
        return true;
    }

    /*
     * Since order is a power of two, every proper divisor of order divides
     * order / 2. Therefore, if x^(order / 2) != 1 (mod q), x cannot have
     * any proper divisor of order as its multiplicative order.
     */
    return modpow(x, order / 2, q) != 1;
}

bool ntt__resolve_roots(uint32_t q,
                        uint32_t n,
                        ntt_transform_type type,
                        uint32_t *omega,
                        uint32_t *psi)
{
    if (omega == NULL || psi == NULL || n == 0) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return false;
    }

    bool have_omega = *omega != 0;
    bool have_psi = *psi != 0;

    if (type == NTT_TRANSFORM_CYCLIC) {
        if (have_psi) {
            NTT_LOG(NTT_LOG_ERROR,
                    "psi is not applicable to NTT_TRANSFORM_CYCLIC");
            return false;
        }
        if (have_omega) {
            if (!ntt__is_primitive_root_of_order(*omega, n, q)) {
                NTT_LOG(
                    NTT_LOG_ERROR,
                    "omega=%u is not a primitive %u-th root of unity mod %u",
                    *omega,
                    n,
                    q);
                return false;
            }
            return true;
        }

        uint32_t g;
        if (!ntt__find_primitive_root(q, &g)) {
            NTT_LOG(NTT_LOG_ERROR, "no primitive root found for q=%u", q);
            return false;
        }
        *omega = modpow(g, (q - 1) / n, q);
        return true;
    }

    /* NTT_TRANSFORM_NEGACYCLIC */
    if (have_omega && have_psi) {
        if (mulmod(*psi, *psi, q) != *omega) {
            NTT_LOG(NTT_LOG_ERROR, "psi^2 != omega (mod q)");
            return false;
        }
        if (!ntt__is_primitive_root_of_order(*psi, 2 * n, q)) {
            NTT_LOG(NTT_LOG_ERROR,
                    "psi=%u is not a primitive %u-th root of unity mod %u",
                    *psi,
                    2 * n,
                    q);
            return false;
        }
        return true;
    }

    if (have_psi) {
        if (!ntt__is_primitive_root_of_order(*psi, 2 * n, q)) {
            NTT_LOG(NTT_LOG_ERROR,
                    "psi=%u is not a primitive %u-th root of unity mod %u",
                    *psi,
                    2 * n,
                    q);
            return false;
        }
        *omega = mulmod(*psi, *psi, q);
        return true;
    }

    if (have_omega) {
        if (!ntt__is_primitive_root_of_order(*omega, n, q)) {
            NTT_LOG(NTT_LOG_ERROR,
                    "omega=%u is not a primitive %u-th root of unity mod %u",
                    *omega,
                    n,
                    q);
            return false;
        }
        uint32_t s;
        if (!ntt__sqrt_mod(*omega, q, &s)) {
            NTT_LOG(NTT_LOG_ERROR,
                    "omega has no square root mod %u: modulus does not "
                    "support a negacyclic transform of size %u",
                    q,
                    n);
            return false;
        }
        *psi = s;
        return true;
    }

    /* Neither given: derive both from scratch. */
    uint32_t g;
    if (!ntt__find_primitive_root(q, &g)) {
        NTT_LOG(NTT_LOG_ERROR, "no primitive root found for q=%u", q);
        return false;
    }
    *psi = modpow(g, (q - 1) / (2 * n), q);
    *omega = mulmod(*psi, *psi, q);
    return true;
}

/**
 * @brief Validates the generic mathematical requirements of an NTT domain.
 *
 * This function validates constraints that depend on both the modulus and
 * the transform size. Adapter-specific modulus requirements, such as
 * primality checks or reduction-backend constraints, remain in the adapter's
 * validate_modulus callback.
 *
 * For a cyclic transform, a primitive n-th root of unity exists in the
 * multiplicative group of a prime field only when n divides q - 1. For a
 * negacyclic transform modulo x^n + 1, a primitive 2n-th root is required,
 * so 2n must divide q - 1.
 *
 * @param[in] q    Prime modulus already validated by the selected adapter.
 * @param[in] n    Transform size.
 * @param[in] type Transform type.
 *
 * @return true if the generic NTT domain requirements are satisfied.
 * @return false otherwise.
 */
bool ntt__validate_transform_params(uint32_t q,
                                    uint32_t n,
                                    ntt_transform_type type)
{

    if (q <= 2 || n == 0 || !ntt_is_power_of_two(n)) {
        NTT_LOG(NTT_LOG_ERROR,
                "Invalid NTT transform parameters: q=%u n=%u",
                q,
                n);
        return false;
    }

    uint64_t order;

    switch (type) {
    case NTT_TRANSFORM_CYCLIC:
        order = n;
        break;
    case NTT_TRANSFORM_NEGACYCLIC:
        order = 2ull * n;
        break;
    default:
        NTT_LOG(NTT_LOG_ERROR, "Unsupported NTT transform type=%d", (int)type);
        return false;
    }

    if (((uint64_t)(q - 1)) % order != 0) {
        NTT_LOG(NTT_LOG_ERROR,
                "required root order=%llu does not divide q-1=%u",
                (unsigned long long)order,
                q - 1);
        return false;
    }

    return true;
}

/* Public utils API */

bool ntt_is_power_of_two(uint32_t x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

uint32_t ntt_reverse_bits(uint32_t x, uint32_t bits)
{
    uint32_t r = 0;
    for (uint32_t i = 0; i < bits; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

bool ntt_is_prime(uint32_t q)
{
    static const uint32_t witnesses[] = {2u, 7u, 61u};

    if (q < 2u) {
        NTT_LOG(NTT_LOG_ERROR, "Primality test failed: q=%u is less than 2", q);
        return false;
    }

    if (q == 2u || q == 3u) {
        return true;
    }

    if ((q & 1u) == 0u) {
        NTT_LOG(NTT_LOG_ERROR, "Primality test failed: q=%u is even", q);
        return false;
    }

    /*
     * Decompose q - 1 as d * 2^s, where d is odd.
     */
    uint32_t d = q - 1u;
    uint32_t s = 0u;

    while ((d & 1u) == 0u) {
        d >>= 1;
        s++;
    }

    for (size_t i = 0; i < sizeof(witnesses) / sizeof(witnesses[0]); i++) {
        uint32_t a = witnesses[i];

        /*
         * A witness greater than or equal to q is not meaningful for this
         * candidate. This also handles small prime candidates.
         */
        if (a >= q) {
            continue;
        }

        uint32_t x = modpow(a, d, q);

        if (x == 1u || x == q - 1u) {
            continue;
        }

        bool witness_passed = false;

        for (uint32_t r = 1u; r < s; r++) {
            x = mulmod(x, x, q);

            if (x == q - 1u) {
                witness_passed = true;
                break;
            }
        }

        if (!witness_passed) {
            NTT_LOG(NTT_LOG_ERROR,
                    "Primality test failed: q=%u is composite "
                    "(Miller-Rabin witness=%u)",
                    q,
                    a);
            return false;
        }
    }

    return true;
}
