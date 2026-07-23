#include "ntt_internal.h"

int ntt_forward(const ntt_ctx *ctx, uint32_t *a)
{
    if (ctx == NULL || a == NULL || ctx->adapter == NULL ||
        ctx->adapter->forward == NULL || ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or adapter");
        return NTT_ERROR;
    }

    return ctx->adapter->forward(ctx->state, a);
}

int ntt_inverse(const ntt_ctx *ctx, uint32_t *a)
{
    if (ctx == NULL || a == NULL || ctx->adapter == NULL ||
        ctx->adapter->inverse == NULL || ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or adapter");
        return NTT_ERROR;
    }

    return ctx->adapter->inverse(ctx->state, a);
}

int ntt_negacyclic_mul(uint32_t *a,
                       uint32_t *b,
                       uint32_t *c,
                       const ntt_ctx *ctx)
{
    if (ctx == NULL || a == NULL || b == NULL || c == NULL ||
        ctx->adapter == NULL || ctx->adapter->negacyclic_mul == NULL ||
        ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or backend");
        return NTT_ERROR;
    }
    return ctx->adapter->negacyclic_mul(ctx->state, a, b, c);
}
