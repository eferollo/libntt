/*
 * ntt_example_stepwise.c
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

/*****************************************************************************
 * Demonstrates how to assemble a negacyclic multiplication from the two
 * transform primitives ntt_forward() and ntt_inverse():
 *
 *     c(x) = a(x)b(x) mod (x^n + 1, q)
 *
 * by hand, exactly as ntt_negacyclic_mul() does internally:
 *
 *   1. twist        a(x) -> a(psi x),  b(x) -> b(psi x)
 *   2. forward NTT  of both twisted operands
 *   3. pointwise    multiply in the transform domain
 *   4. inverse NTT  back to the coefficient domain
 *   5. untwist      by psi^-i to undo step 1
 *
 * from two freshly randomized input polynomials. Cyclic convolution
 * (mod x^n - 1) is not supported yet. Everything the library needs (omega,
 * reduction precomputation) is derived internally from q and n; psi, the
 * primitive 2n-th root of unity required by the twist, is derived here and
 * passed to the context explicitly.
 *
 * The adapter that does the work is chosen by the user on the command line:
 *
 *     ntt_example_stepwise [adapter] [seed] [module_dir]
 *
 * passing the name of a built-in ("scalar", ...) or of a dynamically loaded
 * module, such as the "naive" third-party adapter developed as an example.
 * When omitted, the library default adapter is used, which honors
 * NTT_ADAPTER_MODULE_DIR, the configuration file, and any
 * ntt_adapter_set_default() override, falling back to the built-in "scalar"
 * adapter.
 *
 * As a sanity check on the chosen context, the example also applies a
 * forward/inverse transform round trip to one operand and verifies the input
 * is recovered exactly.
 ****************************************************************************/

#include <ntt/ntt.h>
#include <ntt/ntt_adapter.h>
#include <ntt/ntt_config.h>
#include <ntt/ntt_utils.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef NTT_MODULE_DIR
#define NTT_MODULE_DIR NULL
#endif

#define TEST_N 256
#define TEST_Q 12289u /* 2^12 * 3 + 1, supports n <= 4096 */

/**
 * @brief Internal state of the example's xorshift64 PRNG.
 *
 * Seeded from the wall clock in main(), so every run multiplies a different
 * pair of polynomials. Only fit for a demonstration; nothing here is
 * cryptographic.
 */
static uint64_t rng_state;

/**
 * @brief Computes a * b mod q with plain 64-bit arithmetic.
 *
 * Safe because the example's TEST_Q is small: a, b < q < 2^31, so the
 * product fits comfortably in uint64_t. Real adapters need a general 64-bit
 * reduction such as Barrett or Montgomery. See the optimized scalar backend.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] q Modulus.
 *
 * @return (a * b) mod q.
 */
static uint64_t example_mulmod(uint64_t a, uint64_t b, uint64_t q)
{
    return (a * b) % q;
}

/**
 * @brief Raises a value to a power modulo q (exponentiation by squaring).
 *
 * @param[in] base Base value.
 * @param[in] exp  Non-negative exponent.
 * @param[in] q    Modulus.
 *
 * @return base^exp mod q.
 */
static uint64_t example_modpow(uint64_t base, uint64_t exp, uint64_t q)
{
    uint64_t result = 1 % q;

    base %= q;
    while (exp) {
        if (exp & 1) {
            result = example_mulmod(result, base, q);
        }
        base = example_mulmod(base, base, q);
        exp >>= 1;
    }
    return result;
}

/**
 * @brief Computes the modular inverse of a non-zero field element.
 *
 * Uses Fermat's little theorem, @f$a^{-1} = a^{q-2} \bmod q@f$, which is
 * valid because q is prime.
 *
 * @param[in] a Non-zero field element.
 * @param[in] q Prime modulus.
 *
 * @return Modular inverse of @p a.
 */
static uint64_t example_modinv(uint64_t a, uint64_t q)
{
    return example_modpow(a, q - 2, q);
}

/**
 * @brief Collects the distinct prime factors of a positive integer.
 *
 * Trial division. Only used to find a primitive root of q, so the modest
 * cost is irrelevant in a demonstration.
 *
 * @param[in] x       Integer to factor.
 * @param[out] factors Distinct prime factors (order is not significant).
 * @param[out] count   Number of factors stored.
 *
 * @return true on success, with at least one factor stored.
 */
static bool
example_distinct_factors(uint64_t x, uint64_t *factors, size_t *count)
{
    size_t n = 0;
    uint64_t d = 2;
    uint64_t rem = x;

    if (x < 2 || factors == NULL || count == NULL) {
        return false;
    }

    while (d <= rem / d) {
        if (rem % d == 0) {
            factors[n++] = d;
            while (rem % d == 0) {
                rem /= d;
            }
        }
        d += (d == 2) ? 1 : 2; /* 2, 3, 5, 7, ... (evens after 2) */
    }
    if (rem > 1) {
        factors[n++] = rem;
    }

    *count = n;
    return n > 0;
}

/**
 * @brief Finds a primitive root of the prime field Z_q.
 *
 * A candidate g is primitive exactly when g^((q-1)/p) != 1 (mod q) for every
 * distinct prime factor p of q-1.
 *
 * @param[in] q       Prime modulus.
 * @param[in] factors Distinct prime factors of q-1.
 * @param[in] n_factors Number of factors.
 * @param[out] g_out  Primitive root found.
 *
 * @return true on success.
 */
static bool example_find_primitive_root(uint64_t q,
                                        const uint64_t *factors,
                                        size_t n_factors,
                                        uint64_t *g_out)
{
    uint64_t g;
    bool primitive;

    if (q < 3 || factors == NULL || n_factors == 0 || g_out == NULL) {
        return false;
    }

    for (g = 2; g < q; g++) {
        primitive = true;
        for (size_t i = 0; i < n_factors; i++) {
            if (example_modpow(g, (q - 1) / factors[i], q) == 1) {
                primitive = false;
                break;
            }
        }
        if (primitive) {
            *g_out = g;
            return true;
        }
    }
    return false;
}

/**
 * @brief Multiplies every element of an array by a table of powers mod q.
 *
 * Used both for the forward twist (powers of psi^i) and, with the inverse
 * table, for the untwist.
 *
 * @param[in,out] arr    Array to twist.
 * @param[in] powers     Per-index multiplier table.
 * @param[in] n          Array length.
 * @param[in] q          Modulus.
 */
static void
twist_in_place(uint64_t *arr, const uint64_t *powers, uint32_t n, uint64_t q)
{
    for (uint32_t i = 0; i < n; i++) {
        arr[i] = example_mulmod(arr[i], powers[i], q);
    }
}

/**
 * @brief Draws the next pseudo-random 64-bit value.
 *
 * xorshift64: zero state stays zero, so the seed must be non-zero (main()
 * guards that).
 *
 * @return Next pseudo-random value.
 */
static uint64_t next_random(void)
{
    uint64_t x = rng_state;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

/**
 * @brief Resolves the desired adapter.
 *
 * A NULL/empty @p name resolves the process default
 * (ntt_adapter_get_default()), which honors the configuration file and the
 * NTT_ADAPTER_MODULE_DIR environment variable and falls back to the built-in
 * "scalar" adapter. An explicit @p name is first tried against the built-in
 * registry, then -- if @p module_dir is given -- against the external modules
 * in that directory (e.g. "naive").
 *
 * @param[in] name       Adapter name, or NULL for the default.
 * @param[in] module_dir Directory holding libntt_adapter_<name>.so for
 *                       external adapters, or NULL.
 *
 * @return Adapter descriptor.
 * @return NULL if an explicit name matched neither a built-in nor a module.
 */
static const ntt_adapter *select_adapter(const char *name,
                                         const char *module_dir)
{
    const ntt_adapter *adapter = NULL;

    if (name == NULL || name[0] == '\0') {
        return ntt_adapter_get_default();
    }

    adapter = ntt_adapter_load(name, NULL);
    if (adapter == NULL && module_dir != NULL && module_dir[0] != '\0') {
        adapter = ntt_adapter_load(name, module_dir);
    }
    return adapter;
}

/**
 * @brief Builds an NTT context for the given adapter.
 *
 * The configuration carries the mathematical parameters only: modulus,
 * transform size, and the primitive 2n-th root of unity psi. omega is derived
 * from psi (omega = psi^2) by the common layer, which also validates psi.
 *
 * @param[in] adapter Adapter the context will dispatch through.
 * @param[in] q       Prime modulus.
 * @param[in] n       Transform size.
 * @param[in] psi     Primitive 2n-th root of unity.
 *
 * @return Newly created NTT context.
 * @return NULL on failure.
 */
static ntt_ctx *
ctx_init(const ntt_adapter *adapter, uint64_t q, uint32_t n, uint64_t psi)
{
    ntt_config *cfg = NULL;
    ntt_ctx *ctx = NULL;
    int rc;

    cfg = ntt_config_new();
    if (cfg == NULL) {
        return NULL;
    }

    rc = ntt_config_set_modulus(cfg, q);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    rc = ntt_config_set_size(cfg, n);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    rc = ntt_config_set_psi(cfg, psi);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    ctx = ntt_create(adapter, cfg);

cleanup:
    ntt_config_free(cfg);
    return ctx;
}

/**
 * @brief Computes a negacyclic product step by step from ntt_forward() and
 *        ntt_inverse().
 *
 * Recreates manually what ntt_negacyclic_mul() does internally, so a user
 * limited to the transform primitives can still assemble the convolution:
 *
 *   1. twist:          a(x) -> a(psi x), b(x) -> b(psi x);
 *   2. forward NTT:    of both twisted operands;
 *   3. pointwise:      multiply in the transform domain;
 *   4. inverse NTT:    back to coefficient domain;
 *   5. untwist:        the result by psi^-1 to undo step 1.
 *
 * @param[in] ctx          Context dispatching the transforms.
 * @param[in] psi_pow      psi_pow[i]     = psi^i  mod q.
 * @param[in] psi_inv_pow  psi_inv_pow[i] = psi^-i mod q.
 * @param[in] q            Prime modulus.
 * @param[in] n            Transform size.
 * @param[in] a            First input polynomial.
 * @param[in] b            Second input polynomial.
 * @param[out] out         c(x) = a(x)b(x) mod (x^n + 1, q).
 *
 * @return NTT_OK on success.
 */
static int do_negacyclic_stepwise(const ntt_ctx *ctx,
                                  const uint64_t *psi_pow,
                                  const uint64_t *psi_inv_pow,
                                  uint64_t q,
                                  uint32_t n,
                                  const uint64_t *a,
                                  const uint64_t *b,
                                  uint64_t *out)
{
    uint64_t *ta = NULL;
    uint64_t *tb = NULL;
    int rc = NTT_ERROR;

    if (ctx == NULL || psi_pow == NULL || psi_inv_pow == NULL || a == NULL ||
        b == NULL || out == NULL) {
        return NTT_ERROR;
    }

    ta = calloc(n, sizeof(uint64_t));
    tb = calloc(n, sizeof(uint64_t));
    if (ta == NULL || tb == NULL) {
        goto cleanup;
    }
    memcpy(ta, a, n * sizeof(uint64_t));
    memcpy(tb, b, n * sizeof(uint64_t));

    /* Step 1: twist a(x) and b(x) by psi^i. */
    twist_in_place(ta, psi_pow, n, q);
    twist_in_place(tb, psi_pow, n, q);

    /* Step 2: forward NTT of both twisted operands. */
    if (ntt_forward(ctx, ta) != NTT_OK || ntt_forward(ctx, tb) != NTT_OK) {
        goto cleanup;
    }

    /* Step 3: pointwise multiply in the transform domain. */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = example_mulmod(ta[i], tb[i], q);
    }

    /* Step 4: inverse NTT. */
    if (ntt_inverse(ctx, ta) != NTT_OK) {
        goto cleanup;
    }

    /* Step 5: untwist by psi^-i to undo step 1. */
    for (uint32_t i = 0; i < n; i++) {
        out[i] = example_mulmod(ta[i], psi_inv_pow[i], q);
    }
    rc = NTT_OK;

cleanup:
    free(ta);
    free(tb);
    return rc;
}

/**
 * @brief Applies a forward-then-inverse transform round trip.
 *
 * Copies @p in and runs ntt_forward() followed by ntt_inverse() on the copy,
 * then checks the input is recovered exactly. A context that fails this
 * round trip is not usable, so it validates the adapter/config combination
 * chosen on the command line.
 *
 * @param[in] ctx  Context dispatching the transforms.
 * @param[in] n    Transform size.
 * @param[in] in   Input polynomial.
 *
 * @return true if the round trip recovers @p in exactly.
 */
static bool
round_trip_recovers(const ntt_ctx *ctx, uint32_t n, const uint64_t *in)
{
    uint64_t *buf = NULL;
    bool ok = false;

    if (ctx == NULL || in == NULL) {
        return false;
    }

    buf = calloc(n, sizeof(uint64_t));
    if (buf == NULL) {
        return false;
    }
    memcpy(buf, in, n * sizeof(uint64_t));

    if (ntt_forward(ctx, buf) == NTT_OK && ntt_inverse(ctx, buf) == NTT_OK) {
        ok = true;
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != in[i]) {
                ok = false;
                break;
            }
        }
    }

    free(buf);
    return ok;
}

/**
 * @brief Prints the raw adapter descriptor fields.
 *
 * @param[in] adapter Adapter to describe.
 */
static void describe_adapter(const ntt_adapter *adapter)
{
    printf("    name:            %s\n", ntt_adapter_get_name(adapter));
    printf("    abi version:     %u\n", ntt_adapter_get_abi_version(adapter));
    printf("    struct size:     %u bytes\n",
           ntt_adapter_get_struct_size(adapter));
    printf("    capabilities:    0x%08x\n",
           ntt_adapter_get_capabilities(adapter));
    printf("    supported flags: 0x%08x\n",
           ntt_adapter_get_supported_flags(adapter));
}

/**
 * @brief Prints every coefficient of a polynomial, eight per line.
 *
 * @param[in] name Polynomial identifier used in the header (e.g., "a").
 * @param[in] p    Coefficients, reduced modulo q.
 * @param[in] n    Number of coefficients.
 */
static void print_poly(const char *name, const uint64_t *p, uint32_t n)
{
    printf("%s(x):\n", name);
    for (uint32_t i = 0; i < n; i++) {
        if (i % 8 == 0) {
            uint32_t last = (i + 7 < n) ? i + 7 : n - 1;
            printf("  [%3u..%3u]", i, last);
        }
        printf(" %5llu", (unsigned long long)p[i]);
        if (i % 8 == 7 || i == n - 1) {
            printf("\n");
        }
    }
}

/**
 * @brief Prints the expected command-line usage.
 *
 * @param[in] prog Program name as invoked (argv[0]).
 */
static void print_usage(const char *prog)
{
    printf("usage: %s [adapter] [seed] [module_dir]\n", prog);
    printf("\n");
    printf("  Assembles a negacyclic multiplication a(x)b(x) mod\n");
    printf("  (x^n + 1, q) manually from twist + ntt_forward() + pointwise\n");
    printf("  multiply + ntt_inverse() + untwist, using the adapter of the\n");
    printf("  user's choice.\n");
    printf("\n");
    printf("  adapter     Name of the adapter to use, e.g., 'scalar',\n");
    printf("              'scalar_toy', or an external module such as\n");
    printf("              'naive'. Defaults to the resolved library default\n");
    printf("              (configuration file / NTT_ADAPTER_MODULE_DIR),\n");
    printf("              which falls back to the built-in 'scalar'.\n");
    printf("  seed        Seed for the coefficient PRNG (decimal or\n");
    printf("              0x-prefixed hex). Defaults to a seed derived from\n");
    printf("              the wall clock.\n");
    printf("  module_dir  Directory that holds libntt_adapter_<name>.so for\n");
    printf(
        "              external adapters. Defaults to the build's modules\n");
    printf("              directory (NTT_MODULE_DIR), else none.\n");
    printf("\n");
    printf("  -h, --help  Show this help and exit.\n");
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    const char *module_dir = NTT_MODULE_DIR;
    const ntt_adapter *adapter = NULL;
    ntt_ctx *ctx = NULL;
    uint64_t factors[16] = {0};
    uint64_t g, psi, psi_inv;
    size_t n_factors = 0;
    uint64_t *psi_pow = NULL, *psi_inv_pow = NULL;
    uint64_t *a = NULL, *b = NULL, *c = NULL;
    uint64_t seed;
    int rc = EXIT_FAILURE;

    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc > 4) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    name = (argc >= 2) ? argv[1] : NULL;

    if (argc >= 3) {
        char *end = NULL;
        seed = strtoull(argv[2], &end, 0);
        if (end == argv[2] || *end != '\0') {
            fprintf(stderr, "%s: invalid seed '%s'\n", argv[0], argv[2]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        seed = (uint64_t)time(NULL);
    }

    module_dir = (argc >= 4) ? argv[3] : NTT_MODULE_DIR;

    adapter = select_adapter(name, module_dir);
    if (adapter == NULL) {
        if (module_dir != NULL) {
            fprintf(stderr,
                    "unknown adapter '%s': not built-in and not found in "
                    "'%s'\n",
                    name ? name : "(default)",
                    module_dir);
        } else {
            fprintf(stderr,
                    "unknown adapter '%s': not a built-in adapter\n",
                    name ? name : "(default)");
        }
        return EXIT_FAILURE;
    }

    /* psi = g^((q-1)/(2n)) from a primitive root g of Z_q. */
    if (!example_distinct_factors(TEST_Q - 1, factors, &n_factors) ||
        !example_find_primitive_root(TEST_Q, factors, n_factors, &g)) {
        fprintf(stderr, "failed to find a primitive root of %u\n", TEST_Q);
        return EXIT_FAILURE;
    }
    psi = example_modpow(g, (TEST_Q - 1) / (2ull * TEST_N), TEST_Q);
    psi_inv = example_modinv(psi, TEST_Q);

    ctx = ctx_init(adapter, TEST_Q, TEST_N, psi);
    if (ctx == NULL) {
        fprintf(stderr, "failed to create the NTT context (psi invalid?)\n");
        goto out;
    }

    psi_pow = calloc(TEST_N, sizeof(uint64_t));
    psi_inv_pow = calloc(TEST_N, sizeof(uint64_t));
    a = calloc(TEST_N, sizeof(uint64_t));
    b = calloc(TEST_N, sizeof(uint64_t));
    c = calloc(TEST_N, sizeof(uint64_t));
    if (psi_pow == NULL || psi_inv_pow == NULL || a == NULL || b == NULL ||
        c == NULL) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }

    psi_pow[0] = 1;
    psi_inv_pow[0] = 1;
    for (uint32_t i = 1; i < TEST_N; i++) {
        psi_pow[i] = example_mulmod(psi_pow[i - 1], psi, TEST_Q);
        psi_inv_pow[i] = example_mulmod(psi_inv_pow[i - 1], psi_inv, TEST_Q);
    }

    rng_state = seed ? seed ^ 0x9E3779B97F4A7C15ull : 0x853C49E6748FEA9Bull;
    for (uint32_t i = 0; i < TEST_N; i++) {
        a[i] = next_random() % TEST_Q;
        b[i] = next_random() % TEST_Q;
    }

    printf("Adapter selected:         '%s' (via %s)\n",
           ntt_adapter_get_name(adapter),
           name == NULL ? "default resolution" : "command line");
    describe_adapter(adapter);

    printf("\nq=%llu (prime: %s)  n=%u  psi=%llu  seed=%llu\n",
           (unsigned long long)TEST_Q,
           ntt_is_prime(TEST_Q) ? "yes" : "no",
           TEST_N,
           (unsigned long long)psi,
           (unsigned long long)seed);

    printf("\ninput polynomials and step-wise negacyclic product "
           "(twist -> forward -> pointwise -> inverse -> untwist):\n");
    rc = do_negacyclic_stepwise(ctx,
                                psi_pow,
                                psi_inv_pow,
                                TEST_Q,
                                TEST_N,
                                a,
                                b,
                                c);
    if (rc != NTT_OK) {
        fprintf(stderr, "step-wise multiplication failed\n");
        goto out;
    }
    print_poly("a", a, TEST_N);
    printf("\n");
    print_poly("b", b, TEST_N);
    printf("\n");
    print_poly("c", c, TEST_N);

    printf("\nforward/inverse round trip on a(x):\n");
    if (!round_trip_recovers(ctx, TEST_N, a)) {
        fprintf(stderr, "  MISMATCH: input not recovered\n");
        goto out;
    }
    printf("  recovers input\n");

out:
    free(c);
    free(b);
    free(a);
    free(psi_inv_pow);
    free(psi_pow);
    if (ctx != NULL) {
        ntt_destroy(ctx);
    }
    ntt_adapter_unload_all();
    return rc;
}
