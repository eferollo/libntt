/*
 * A deliberately simple, self-contained NTT adapter, written the way a
 * third-party adapter would be.
 *
 * It includes ONLY the public NTT headers, links against nothing, is compiled
 * with hidden visibility, and exports a single ntt_adapter_module_init entry
 * point that the loader looks up with dlsym.
 *
 * Because a dynamically loaded module cannot link against the library, this
 * adapter reads the configuration through the ntt_core_api dispatch table the
 * loader injects instead of calling ntt_config getters directly. It uses
 * plain '%' modular arithmetic and owns its own logging, since an external
 * adapter owns its logging.
 */

#include <ntt/ntt.h>
#include <ntt/ntt_core.h>
#include <ntt/ntt_utils.h>

#include <stdlib.h>
#include <string.h>

#ifdef NTT_NAIVE_DEBUG
#define NAIVE_LOG(...) fprintf(stderr, "[naive] " __VA_ARGS__)
#else
#define NAIVE_LOG(...) ((void)0)
#endif /* NTT_NAIVE_DEBUG */

/**
 * @brief Multiplies two values modulo m without a 128-bit integer type.
 *
 * Implements the multiplication as repeated doubling and conditional addition
 * (binary left-to-right method). Each intermediate value is reduced before it
 * can grow: a and r stay below m and their sum below 2m, so plain 64-bit
 * arithmetic is always sufficient.
 *
 * The result is correct for any modulus m <= 2^63, which is the general range
 * the public NTT API accepts.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] m Modulus.
 *
 * @return (a * b) mod m.
 */
static uint64_t naive_mul(uint64_t a, uint64_t b, uint64_t m)
{
    uint64_t r = 0;

    a %= m;
    while (b) {
        if (b & 1) {
            r = (r + a) % m;
        }
        a = (a + a) % m;
        b >>= 1;
    }
    return r;
}

/**
 * @brief Adds two values modulo q.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] q Modulus.
 *
 * @return (a + b) mod q.
 */
static uint64_t naive_addmod(uint64_t a, uint64_t b, uint64_t q)
{
    return (a + b) % q;
}

/**
 * @brief Subtracts two values modulo q.
 *
 * The result is always reduced by forcing it through % q, so no requirement
 * is placed on the relative sizes of @p a and @p b.
 *
 * @param[in] a Minuend.
 * @param[in] b Subtrahend.
 * @param[in] q Modulus.
 *
 * @return (a - b) mod q.
 */
static uint64_t naive_submod(uint64_t a, uint64_t b, uint64_t q)
{
    return (a + q - b) % q;
}

/**
 * @brief Raises a value to a power modulo q (exponentiation by squaring).
 *
 * @param[in] base  Base value.
 * @param[in] exp   Non-negative exponent.
 * @param[in] q     Modulus.
 *
 * @return base^exp mod q. For base == 0 and exp == 0 the conventional result
 *         is 1 (hence the initial 1 % q, which also guards the degenerate
 *         q == 1).
 */
static uint64_t naive_modpow(uint64_t base, uint64_t exp, uint64_t q)
{
    uint64_t result = 1 % q;

    base %= q;
    while (exp) {
        if (exp & 1) {
            result = naive_mul(result, base, q);
        }
        base = naive_mul(base, base, q);
        exp >>= 1;
    }
    return result;
}

/**
 * @brief Computes the modular inverse of a non-zero field element.
 *
 * Uses Fermat's little theorem,
 * @f$a^{-1} = a^{q-2} \bmod q@f$, which is valid because the naive adapter
 * operates over a prime field Z_q.
 *
 * @param[in] a Non-zero field element.
 * @param[in] q Prime modulus.
 *
 * @return Modular inverse of @p a.
 */
static uint64_t naive_modinv(uint64_t a, uint64_t q)
{
    return naive_modpow(a, q - 2, q);
}

/**
 * @brief Private state owned by the naive NTT adapter.
 *
 * Stores the mathematical parameters of the transform and the precomputed
 * powers of psi needed by the negacyclic convolution. The state is opaque to
 * the common library: the adapter builds it in setup(), holds it through the
 * life of the context, and frees it in teardown().
 */
typedef struct {
    uint64_t q;            /* prime modulus */
    uint32_t n;            /* transform size */
    uint64_t omega;        /* primitive n-th root of unity mod q */
    uint64_t omega_inv;    /* inverse of omega mod q */
    uint64_t n_inv;        /* inverse of n mod q (INTT scaling factor) */
    uint64_t psi;          /* primitive 2n-th root of unity mod q */
    uint64_t psi_inv;      /* inverse of psi mod q */
    uint64_t *psi_pow;     /* psi_pow[i] = psi^i mod q, i = 0..n-1 */
    uint64_t *psi_inv_pow; /* psi_inv_pow[i] = psi^-i mod q, i = 0..n-1 */
} naive_state;

static void naive_teardown(void *state);
static int naive_forward(void *state, uint64_t *a);

/**
 * @brief Validates whether a modulus is supported.
 *
 * The generic half of the validation (primality, transform-size support,
 * root resolution) is handled by the common NTT layer before setup(). Only
 * the trivial adapter-side constraint needs to be checked here.
 *
 * @param[in] config NTT configuration.
 * @param[in] api    Core API injected by the loader.
 *
 * @return true if the configured modulus is > 1.
 */
static bool naive_validate_modulus(const ntt_config *config,
                                   const ntt_core_api *api)
{
    return ntt_core_get_modulus(api, config) > 1;
}

/**
 * @brief Initializes the naive adapter state for an NTT context.
 *
 * Reads the mathematical parameters through the injected Core API, then
 * precomputes the powers of psi and psi^-1 used to twist and un-twist the
 * inputs of the negacyclic multiplication.
 *
 * @param[in] config NTT configuration.
 * @param[in] api    Core API injected by the loader.
 *
 * @return Newly allocated naive adapter state.
 * @return NULL on invalid parameters or allocation failure.
 */
static void *naive_setup(const ntt_config *config, const ntt_core_api *api)
{
    uint64_t q;
    uint64_t psi;
    uint32_t n;
    naive_state *s = NULL;

    q = ntt_core_get_modulus(api, config);
    n = ntt_core_get_size(api, config);
    psi = ntt_core_get_psi(api, config);

    if (q <= 1 || n == 0) {
        NAIVE_LOG("bad params q=%llu n=%u\n", (unsigned long long)q, n);
        return NULL;
    }

    s = (naive_state *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }

    s->q = q;
    s->n = n;
    s->psi = psi % q;
    s->psi_inv = naive_modinv(s->psi, q);
    s->omega = naive_mul(s->psi, s->psi, q);
    s->omega_inv = naive_modinv(s->omega, q);
    /* Fermat works only because q is prime; % q guards n >= q. */
    s->n_inv = naive_modinv((uint64_t)n % q, q);

    s->psi_pow = (uint64_t *)calloc(n, sizeof(uint64_t));
    s->psi_inv_pow = (uint64_t *)calloc(n, sizeof(uint64_t));
    if (s->psi_pow == NULL || s->psi_inv_pow == NULL) {
        naive_teardown(s);
        return NULL;
    }

    s->psi_pow[0] = 1;
    s->psi_inv_pow[0] = 1;
    for (uint32_t i = 1; i < n; i++) {
        s->psi_pow[i] = naive_mul(s->psi_pow[i - 1], s->psi, q);
        s->psi_inv_pow[i] = naive_mul(s->psi_inv_pow[i - 1], s->psi_inv, q);
    }

    return s;
}

/**
 * @brief Applies an in-place bit-reversal permutation.
 *
 * Rearranges @p a so that coefficient @p i ends up at the bit-reversed index
 * of @p i over log2(n) bits; this is the required input order of the iterative
 * radix-2 butterfly network.
 *
 * @param[in,out] a Array to permute.
 * @param[in] n     Number of elements.
 *
 * @return NTT_OK on success.
 */
static int naive_bitrev(uint64_t *a, uint32_t n)
{
    uint32_t bit, i, j = 0;

    for (i = 1; i < n; i++) {
        bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            uint64_t tmp = a[i];
            a[i] = a[j];
            a[j] = tmp;
        }
    }
    return NTT_OK;
}

/**
 * @brief Computes a radix-2 Cooley-Tukey NTT.
 *
 * Decimation-in-time butterflies over successively wider blocks. @p root is
 * either omega (forward transform) or omega^-1 (inverse transform), and
 * @p scale the n^-1 factor applied afterwards for the inverse. This is the
 * textbook O(n log n) formulation, kept intentionally unoptimized.
 *
 * @param[in,out] s     Naive adapter state.
 * @param[in,out] a     Coefficient array, bit-reversed in place first.
 * @param[in] root      Primitive n-th root of unity (or its inverse).
 * @param[in] scale     Post-transform scaling factor (1 for the forward NTT).
 *
 * @return NTT_OK on success.
 */
static int naive_fft(naive_state *s, uint64_t *a, uint64_t root, uint64_t scale)
{
    uint32_t n = s->n;
    uint64_t q = s->q;

    if (naive_bitrev(a, n) != NTT_OK) {
        return NTT_ERROR;
    }

    for (uint32_t m = 2; m <= n; m <<= 1) {
        uint64_t w_m = naive_modpow(root, n / m, q);
        for (uint32_t k = 0; k < n; k += m) {
            uint64_t w = 1;
            for (uint32_t j = 0; j < m / 2; j++) {
                uint64_t t = naive_mul(a[k + j + m / 2], w, q);
                uint64_t u = a[k + j];
                a[k + j] = naive_addmod(u, t, q);
                a[k + j + m / 2] = naive_submod(u, t, q);
                w = naive_mul(w, w_m, q);
            }
        }
    }

    if (scale != 1) {
        for (uint32_t i = 0; i < n; i++) {
            a[i] = naive_mul(a[i], scale, q);
        }
    }
    return NTT_OK;
}

/**
 * @brief Computes the forward NTT.
 *
 * @param[in] state Adapter state.
 * @param[in,out] a Coefficient array of length n.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if an input pointer is NULL.
 */
static int naive_forward(void *state, uint64_t *a)
{
    naive_state *s = NULL;

    if (state == NULL || a == NULL) {
        return NTT_ERROR;
    }
    s = (naive_state *)state;
    return naive_fft(s, a, s->omega, 1);
}

/**
 * @brief Computes the inverse NTT.
 *
 * @param[in] state Adapter state.
 * @param[in,out] a Coefficient array of length n.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if an input pointer is NULL.
 */
static int naive_inverse(void *state, uint64_t *a)
{
    naive_state *s;

    if (state == NULL || a == NULL) {
        return NTT_ERROR;
    }
    s = (naive_state *)state;
    return naive_fft(s, a, s->omega_inv, s->n_inv);
}

/**
 * @brief Multiplies two polynomials using the negacyclic convolution.
 *
 * Computes c(x) = a(x)b(x) mod (x^n + 1, q) through the standard twist:
 *
 *   1. multiply both inputs by psi^i (negacyclic twist);
 *   2. forward NTT, pointwise multiply, inverse NTT;
 *   3. multiply the result by psi^-i (un-twist).
 *
 * The precomputed psi_pow / psi_inv_pow tables make the twist constant-time
 * per coefficient.
 *
 * @param[in] state Adapter state.
 * @param[in] a     First input polynomial.
 * @param[in] b     Second input polynomial.
 * @param[out] c    Output polynomial (may alias neither a nor b).
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if an input pointer is NULL or a transform failed.
 */
static int
naive_negacyclic_mul(void *state, uint64_t *a, uint64_t *b, uint64_t *c)
{
    naive_state *s = NULL;
    uint32_t n;
    uint64_t q;
    uint64_t *ta = NULL, *tb = NULL;
    int rc = NTT_ERROR;

    if (state == NULL || a == NULL || b == NULL || c == NULL) {
        return NTT_ERROR;
    }
    s = (naive_state *)state;
    n = s->n;
    q = s->q;

    ta = (uint64_t *)calloc(n, sizeof(uint64_t));
    tb = (uint64_t *)calloc(n, sizeof(uint64_t));
    if (ta == NULL || tb == NULL) {
        goto cleanup;
    }

    for (uint32_t i = 0; i < n; i++) {
        ta[i] = naive_mul(a[i], s->psi_pow[i], q);
        tb[i] = naive_mul(b[i], s->psi_pow[i], q);
    }
    if (naive_forward(s, ta) != NTT_OK || naive_forward(s, tb) != NTT_OK) {
        goto cleanup;
    }
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = naive_mul(ta[i], tb[i], q);
    }
    if (naive_inverse(s, ta) != NTT_OK) {
        goto cleanup;
    }
    for (uint32_t i = 0; i < n; i++) {
        c[i] = naive_mul(ta[i], s->psi_inv_pow[i], q);
    }
    rc = NTT_OK;

cleanup:
    free(ta);
    free(tb);
    return rc;
}

/**
 * @brief Releases all resources owned by a naive adapter state.
 *
 * @param[in] state Adapter state returned by naive_setup().
 */
static void naive_teardown(void *state)
{
    naive_state *s = NULL;

    if (state == NULL) {
        return;
    }
    s = (naive_state *)state;
    free(s->psi_pow);
    free(s->psi_inv_pow);
    /* Wipe the state before returning it to the allocator. */
    memset(s, 0, sizeof(*s));
    free(s);
}

/**
 * @brief Static descriptor of the naive adapter.
 *
 * The descriptor is immutable and stored in .rodata; the module entry point
 * only hands out a pointer to it. capabilities advertises that the modulus is
 * selected at run time, while supported_flags is empty because the naive
 * backend has no optimizable reduction to configure.
 */
static const ntt_adapter ntt_naive_adapter = {
    .abi_version = NTT_ADAPTER_ABI_VERSION,
    .struct_size = sizeof(ntt_adapter),
    .name = "naive",
    .capabilities = NTT_CAP_RUNTIME_MODULUS,
    .supported_flags = 0,
    .validate_modulus = naive_validate_modulus,
    .setup = naive_setup,
    .teardown = naive_teardown,
    .forward = naive_forward,
    .inverse = naive_inverse,
    .negacyclic_mul = naive_negacyclic_mul,
};

/**
 * @brief Module entry point invoked by the loader after dlsym().
 *
 * The injected core is append-only, so a module built against a given
 * NTT_CORE_API_VERSION is satisfied by any library core with a version >= it;
 * a newer module on an older core would miss dispatch ids it references, so
 * it must refuse to load. ntt_core_is_compatible() performs that handshake:
 * returning -1 here makes the loader reject the module loudly.
 *
 * @param[in] core Injected core API.
 * @param[out] out On success, points to the static adapter descriptor.
 *
 * @return 0 on success.
 * @return -1 if the core is too old or @p out is NULL.
 */
NTT_MODULE_EXPORT int ntt_adapter_module_init(const ntt_core_api *core,
                                              const ntt_adapter **out)
{
    if (!ntt_core_is_compatible(core, NTT_CORE_API_VERSION)) {
        return -1;
    }
    if (out == NULL) {
        return -1;
    }
    *out = &ntt_naive_adapter;
    return 0;
}
