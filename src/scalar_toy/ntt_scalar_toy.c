#include "ntt_internal.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Performs an in-place iterative radix-2 Number Theoretic Transform
 * (NTT).
 *
 * Computes the transform using the iterative Cooley-Tukey decimation-in-time
 * algorithm. The input array is first permuted into bit-reversed order, after
 * which log2(n) stages of butterfly operations are applied. At each stage,
 * subproblems of size m are combined using powers of the supplied primitive
 * root of unity.
 *
 * The transform is performed in-place and overwrites the input array.
 *
 * The root parameter must be an n-th primitive root of unity modulo
 * ctx->q for the forward transform, or its modular inverse for the inverse
 * transform. If scale is not equal to 1, each output coefficient is
 * multiplied by scale after the transform. For the inverse transform,
 * this is typically n^{-1} mod q.
 *
 * @param[in] ctx   NTT context containing the transform size (n) and
 *                  modulus (q).
 * @param[in,out] a Array of n coefficients to transform. On success, it
 *                  contains the transformed coefficients.
 * @param[in] root  Primitive n-th root of unity modulo q (or its inverse
 *                  for the inverse transform).
 * @param[in] scale Scaling factor applied to each output coefficient after the
 *                  transform. Use 1 to disable scaling.
 *
 * @return NTT_OK  Transform completed successfully.
 * @return NTT_ERROR on errors.
 */
static int
iterative_fft(const ntt_ctx *ctx, uint32_t *a, uint32_t root, uint32_t scale)
{
    int rc;
    uint32_t n = ctx->n;
    uint32_t q = ctx->q;

    rc = ntt__bitrev_permute(a, n);
    if (rc == -1) {
        NTT_LOG(NTT_LOG_ERROR, "Bit reversal failed");
        return NTT_ERROR;
    }

    for (uint32_t m = 2; m <= n; m <<= 1) {
        uint32_t w_m = ntt__modpow(root, n / m, q);
        for (uint32_t k = 0; k < n; k += m) {
            uint32_t w = 1 % q;
            for (uint32_t j = 0; j < m / 2; j++) {
                uint32_t t = ntt__mulmod(a[k + j + m / 2], w, q);
                uint32_t u = a[k + j];
                a[k + j] = ntt__addmod(u, t, q);
                a[k + j + m / 2] = ntt__submod(u, t, q);
                w = ntt__mulmod(w, w_m, q);
            }
        }
    }

    if (scale != 1) {
        for (uint32_t i = 0; i < n; i++) {
            a[i] = ntt__mulmod(a[i], scale, q);
        }
    }

    return NTT_OK;
}

/**
 * @brief Computes the forward Number Theoretic Transform (NTT).
 *
 * Applies the in-place forward radix-2 NTT to the input polynomial using the
 * primitive n-th root of unity stored in the context.
 *
 * @param[in] ctx   Initialized NTT context.
 * @param[in,out] a Array of ctx->n coefficients. On success, it contains the
 *                  forward NTT of the input.
 *
 * @return NTT_OK  Transform completed successfully.
 * @return NTT_ERROR Invalid input.
 *
 * @see iterative_fft()
 */
int ntt_forward(const ntt_ctx *ctx, uint32_t *a)
{
    if (ctx == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }
    return iterative_fft(ctx, a, ctx->omega, 1);
}

/**
 * @brief Computes the inverse Number Theoretic Transform (INTT).
 *
 * Applies the in-place inverse radix-2 NTT to the input polynomial using the
 * inverse primitive n-th root of unity stored in the context. The output is
 * scaled by n^{-1} mod q to recover the original polynomial.
 *
 * @param[in] ctx   Initialized NTT context.
 * @param[in,out] a Array of ctx->n coefficients. On success, it contains the
 *                  inverse NTT of the input.
 *
 * @return 0  Transform completed successfully.
 * @return NTT_ERROR Invalid input.
 *
 * @see iterative_fft()
 */
static inline int ntt_inverse(const ntt_ctx *ctx, uint32_t *a)
{
    if (ctx == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }
    return iterative_fft(ctx, a, ctx->omega_inv, ctx->n_inv);
}

/**
 * @brief Multiplies two polynomials using the negacyclic Number Theoretic
 * Transform (NTT).
 *
 * Computes the product
 * @f[
 * c(x) = a(x)b(x) mod (x^n + 1, q),
 * @f]
 * where q is the modulus and n is the transform size stored in the NTT
 * context.
 *
 * The multiplication is performed using the standard "twisting" technique to
 * convert a negacyclic convolution into a cyclic convolution:
 *   1. Multiply each coefficient by psi^i.
 *   2. Compute the forward NTT of both operands.
 *   3. Perform pointwise multiplication in the transform domain.
 *   4. Compute the inverse NTT.
 *   5. Multiply each coefficient by psi^{-i} to recover the negacyclic
 *      product.
 *
 * Temporary buffers are allocated internally and freed before the function
 * returns.
 *
 * @param[in] a    First input polynomial of length ctx->n.
 * @param[in] b    Second input polynomial of length ctx->n.
 * @param[out] c   Output polynomial of length ctx->n. May alias neither a nor
 *                 b.
 * @param[in] ctx  Initialized NTT context containing the transform parameters
 *                 and the precomputed powers of psi psi^{-1}.
 *
 * @return 0  Multiplication completed successfully.
 * @return NTT_ERROR on errors.
 */
int ntt_negacyclic_mul(uint32_t *a,
                       uint32_t *b,
                       uint32_t *c,
                       const ntt_ctx *ctx)
{
    if (ctx == NULL || a == NULL || b == NULL || c == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    uint32_t n = ctx->n;
    uint32_t q = ctx->q;
    int rc;

    /* Twisted copies of a and b */
    uint32_t *ta = calloc(n, sizeof(uint32_t));
    uint32_t *tb = calloc(n, sizeof(uint32_t));
    if (ta == NULL || tb == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Error while allocating twisted a and b");
        rc = NTT_ERROR;
        goto cleanup;
    }

    /* Step 1: twist by psi^i */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__mulmod(a[i], ctx->psi_pow[i], q);
        tb[i] = ntt__mulmod(b[i], ctx->psi_pow[i], q);
    }

    /* Step 2: foward NTT (cyclic) on each */
    rc = ntt_forward(ctx, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }
    rc = ntt_forward(ctx, tb);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 3: pointwise multiply, reuse ta as the output buffer */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__mulmod(ta[i], tb[i], q);
    }

    /* Step 4: inverse NTT */
    rc = ntt_inverse(ctx, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 5: untwist by psi^{-i} to undo step 1 */
    for (uint32_t i = 0; i < n; i++) {
        c[i] = ntt__mulmod(ta[i], ctx->psi_inv_pow[i], q);
    }

cleanup:
    SAFE_FREE(ta);
    SAFE_FREE(tb);
    return rc;
}

/**
 * @brief Multiplies two polynomials using the cyclic Number Theoretic
 * Transform (NTT).
 *
 * Computes the product
 * @f[
 * c(x) = a(x)b(x) mod (x^n - 1, q),
 * @f]
 * where q is the modulus and n is the transform size stored in the NTT
 * context.
 *
 * Unlike negacyclic_mul_ntt(), no twisting by powers of psi is required:
 * the plain NTT (using omega directly) already implements the convolution
 * theorem for cyclic convolution, so this is the direct
 * NTT -> pointwise-multiply -> INTT pipeline with no pre- or post-processing.
 *
 *   1. Compute the forward NTT of both operands.
 *   2. Perform pointwise multiplication in the transform domain.
 *   3. Compute the inverse NTT.
 *
 * Temporary buffers are allocated internally and freed before the function
 * returns.
 *
 * @param[in] a    First input polynomial of length ctx->n.
 * @param[in] b    Second input polynomial of length ctx->n.
 * @param[out] c   Output polynomial of length ctx->n. May alias neither a nor
 *                 b.
 * @param[in] ctx  Initialized NTT context containing the transform
 *                 parameters.
 *
 * @return NTT_OK  Multiplication completed successfully.
 * @return NTT_ERROR on errors.
 */
int cyclic_mul_ntt(uint32_t *a, uint32_t *b, uint32_t *c, const ntt_ctx *ctx)
{
    if (ctx == NULL || a == NULL || b == NULL || c == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    uint32_t n = ctx->n;
    int rc;

    uint32_t *ta = calloc(n, sizeof(uint32_t));
    uint32_t *tb = calloc(n, sizeof(uint32_t));
    if (ta == NULL || tb == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Error while allocating twisted a and b");
        rc = NTT_ERROR;
        goto cleanup;
    }

    memcpy(ta, a, n * sizeof(uint32_t));
    memcpy(tb, b, n * sizeof(uint32_t));

    /* Step 1: forward NTT on each operand, no twisting */
    rc = ntt_forward(ctx, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }
    rc = ntt_forward(ctx, tb);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 2: pointwise multiply, reuse ta as the output buffer */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__mulmod(ta[i], tb[i], ctx->q);
    }

    /* Step 3: inverse NTT recovers the cyclic product directly */
    rc = ntt_inverse(ctx, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    memcpy(c, ta, n * sizeof(uint32_t));

cleanup:
    SAFE_FREE(ta);
    SAFE_FREE(tb);
    return rc;
}
