/*
 * ntt_api.c
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

#include "ntt_internal.h"

int ntt_forward(const ntt_ctx *ctx, uint64_t *a)
{
    if (ctx == NULL || a == NULL || ctx->adapter == NULL ||
        ctx->adapter->forward == NULL || ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or adapter");
        return NTT_ERROR;
    }

    return ctx->adapter->forward(ctx->state, a);
}

int ntt_inverse(const ntt_ctx *ctx, uint64_t *a)
{
    if (ctx == NULL || a == NULL || ctx->adapter == NULL ||
        ctx->adapter->inverse == NULL || ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or adapter");
        return NTT_ERROR;
    }

    return ctx->adapter->inverse(ctx->state, a);
}

int ntt_negacyclic_mul(uint64_t *a,
                       uint64_t *b,
                       uint64_t *c,
                       const ntt_ctx *ctx)
{
    if (ctx == NULL || a == NULL || b == NULL || c == NULL ||
        ctx->adapter == NULL || ctx->adapter->negacyclic_mul == NULL ||
        ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments or adapter");
        return NTT_ERROR;
    }
    return ctx->adapter->negacyclic_mul(ctx->state, a, b, c);
}
