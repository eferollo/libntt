#include "ntt/ntt_log.h"
#include "ntt_internal.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize an NTT context structure.
 *
 * @param[in] q         Prime modulus.
 * @param[in] n         Transform size (power of two).
 * @param[in] omega     A primitive n-th root of unity mod q.
 * @param[in] psi       A primitive 2n-th root of unity mod q, with
 *                      psi^2 = omega.
 *
 * @return 0 on success.
 * @return -1 on allocation failure or invalid parameters.
 */
ntt_ctx *ntt_create(uint32_t q, uint32_t n, uint32_t omega, uint32_t psi)
{
    if (!ntt__is_power_of_two(n)) {
        NTT_LOG(NTT_LOG_ERROR, "n=%u is not a power of two", n);
        return NULL;
    }

    ntt_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "ctx allocation failed", n);
        return NULL;
    }

    ctx->q = q;
    ctx->n = n;
    ctx->omega = omega % q;
    ctx->psi = psi % q;

    ctx->stages = 0;
    for (uint32_t t = n; t > 1; t >>= 1) {
        ctx->stages++;
    }

    ctx->omega_inv = ntt__modinv(ctx->omega, q);
    ctx->n_inv = ntt__modinv(n % q, q);
    ctx->psi_inv = ntt__modinv(ctx->psi, q);

    ctx->psi_pow = calloc(n, sizeof(uint32_t));
    ctx->psi_inv_pow = calloc(n, sizeof(uint32_t));
    if (ctx->psi_pow == NULL || ctx->psi_inv_pow == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "psi table allocation failed (n=%u)", n);
        goto cleanup;
    }

    ctx->psi_pow[0] = 1 % q;
    ctx->psi_inv_pow[0] = 1 % q;
    for (uint32_t i = 1; i < n; i++) {
        ctx->psi_pow[i] = ntt__mulmod(ctx->psi_pow[i - 1], ctx->psi, q);
        ctx->psi_inv_pow[i] =
            ntt__mulmod(ctx->psi_inv_pow[i - 1], ctx->psi_inv, q);
    }

    NTT_LOG(NTT_LOG_INFO,
            "New NTT context: q=%u n=%u omega=%u psi=%u",
            q,
            n,
            ctx->omega,
            ctx->psi);
    return ctx;

cleanup:
    SAFE_FREE(ctx->psi_pow);
    SAFE_FREE(ctx->psi_inv_pow);
    ZERO_STRUCTP(ctx);
    return NULL;
}

/**
 * @brief Releases all resources owned by an NTT context.
 *
 * Frees the dynamically allocated lookup tables stored in the context,
 * securely clears the context structure, and leaves no sensitive data
 * behind.
 *
 * @param[in] ctx   Pointer to the NTT context to be released.
 */
void ntt_destroy(ntt_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }

    NTT_LOG(NTT_LOG_DEBUG, "NTT context q=%u n=%u freed", ctx->q, ctx->n);

    SAFE_FREE(ctx->psi_pow);
    SAFE_FREE(ctx->psi_inv_pow);
    ZERO_STRUCTP(ctx);
    SAFE_FREE(ctx);
}
