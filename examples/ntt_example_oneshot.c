/*
 * ntt_example_oneshot.c
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
 * Demonstrates the usage of negacyclic multiplication API ntt_negacyclic_mul:
 *
 *     c(x) = a(x)b(x) mod (x^n + 1, q)
 *
 * from two freshly randomized input polynomials. Cyclic convolution
 * (mod x^n - 1) is not supported yet. Everything the library needs (psi,
 * omega, reduction precomputation) is derived internally from q and n and
 * according to the selected adapter.
 *
 * The adapter that does the work is chosen by the user on the command line:
 *
 *     ntt_example_oneshot [adapter] [seed] [module_dir]
 *
 * passing the name of a built-in ("scalar", ...) or of a dynamically
 * loaded module, such as the "naive" third-party adapter developed as an
 * example. When omitted, the library default adapter is used, which honors
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
 * pair of polynomials. Only fit for a demonstration. Nothing here is
 * cryptographic.
 */
static uint64_t rng_state;

/**
 * @brief Draws the next pseudo-random 64-bit value.
 *
 * @note xorshift64: zero state stays zero, so the seed must be non-zero.
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
 * registry, then, if @p module_dir is given, against the external modules
 * in that directory (e.g., "naive").
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
 * @brief Initializes an NTT context for the given adapter.
 *
 * Only the mathematical parameters are configured: modulus, transform size.
 * psi is left unset (0): for the one-shot API the common layer derives the
 * primitive 2n-th root of unity automatically from q and n, and ntt_create()
 * validates the result.
 *
 * @param[in] adapter Adapter the context will dispatch through.
 * @param[in] q       Prime modulus.
 * @param[in] n       Transform size.
 *
 * @return Newly created NTT context.
 * @return NULL on failure.
 */
static ntt_ctx *ctx_init(const ntt_adapter *adapter, uint64_t q, uint32_t n)
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

    ctx = ntt_create(adapter, cfg);

cleanup:
    ntt_config_free(cfg);
    return ctx;
}

/**
 * @brief Computes a negacyclic product in one shot.
 *
 * Copies both operands so the caller's arrays are untouched, then delegates
 * the whole convolution to ntt_negacyclic_mul().
 *
 * @param[in] ctx  Context dispatching the multiplication.
 * @param[in] n    Transform size.
 * @param[in] a    First input polynomial.
 * @param[in] b    Second input polynomial.
 * @param[out] out c(x) = a(x)b(x) mod (x^n + 1, q).
 *
 * @return NTT_OK on success.
 */
static int do_negacyclic_one_shot(const ntt_ctx *ctx,
                                  uint32_t n,
                                  const uint64_t *a,
                                  const uint64_t *b,
                                  uint64_t *out)
{
    uint64_t *ta = NULL;
    uint64_t *tb = NULL;
    int rc = NTT_ERROR;

    if (ctx == NULL || a == NULL || b == NULL || out == NULL) {
        return NTT_ERROR;
    }

    ta = calloc(n, sizeof(uint64_t));
    tb = calloc(n, sizeof(uint64_t));
    if (ta == NULL || tb == NULL) {
        goto cleanup;
    }
    memcpy(ta, a, n * sizeof(uint64_t));
    memcpy(tb, b, n * sizeof(uint64_t));

    rc = ntt_negacyclic_mul(ta, tb, out, ctx);

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
    printf("  Runs a negacyclic multiplication a(x)b(x) mod (x^n + 1, q)\n");
    printf("  in a single ntt_negacyclic_mul() call, using the adapter of\n");
    printf("  the user's choice.\n");
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
    printf("              external adapters. Defaults to the build's \n");
    printf("              modules directory (NTT_MODULE_DIR), else none.\n");
    printf("\n");
    printf("  -h, --help  Show this help and exit.\n");
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    const char *module_dir = NTT_MODULE_DIR;
    const ntt_adapter *adapter = NULL;
    ntt_ctx *ctx = NULL;
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

    ctx = ctx_init(adapter, TEST_Q, TEST_N);
    if (ctx == NULL) {
        fprintf(stderr, "failed to create the NTT context\n");
        goto out;
    }

    a = calloc(TEST_N, sizeof(uint64_t));
    b = calloc(TEST_N, sizeof(uint64_t));
    c = calloc(TEST_N, sizeof(uint64_t));
    if (a == NULL || b == NULL || c == NULL) {
        fprintf(stderr, "out of memory\n");
        goto out;
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

    printf("\nq=%llu (prime: %s)  n=%u  seed=%llu\n",
           (unsigned long long)TEST_Q,
           ntt_is_prime(TEST_Q) ? "yes" : "no",
           TEST_N,
           (unsigned long long)seed);

    printf("\ninput polynomials and one-shot negacyclic product:\n");
    rc = do_negacyclic_one_shot(ctx, TEST_N, a, b, c);
    if (rc != NTT_OK) {
        fprintf(stderr, "ntt_negacyclic_mul() failed\n");
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
    if (ctx != NULL) {
        ntt_destroy(ctx);
    }
    ntt_adapter_unload_all();
    return rc;
}
