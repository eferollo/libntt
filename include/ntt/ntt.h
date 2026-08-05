/*
 * ntt.h
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

#ifndef NTT_H
#define NTT_H

#include "ntt/ntt_adapter.h"
#include "ntt/ntt_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ntt_ctx_s ntt_ctx;

typedef enum {
    NTT_OK = 0,
    NTT_ERROR = -1,
} ntt_status;

/**
 * @brief Initializes an NTT context using a selected NTT adapter.
 *
 * Different ntt_ctx instances may use different adapters within the same
 * process. The adapter owns all implementation-specific arithmetic state,
 * transform tables, and precomputation behind an opaque state pointer.
 *
 * @param[in] adapter NTT adapter to use.
 * @param[in] config  NTT transform parameters and optional configuration flags.
 *
 * @return Newly allocated context
 * @return NULL on invalid parameters, an adapter that rejects the requested
 *         configuration, or allocation failure.
 */
ntt_ctx *ntt_create(const ntt_adapter *adapter, const ntt_config *config);

/**
 * @brief Releases all resources owned by an NTT context.
 *
 * Frees the dynamically allocated lookup tables stored in the context,
 * securely clears the context structure, and leaves no sensitive data
 * behind.
 *
 * @param[in] ctx   Pointer to the NTT context to be released.
 */
void ntt_destroy(ntt_ctx *ctx);

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
 * @param[in] a    First input polynomial of length ctx->n.
 * @param[in] b    Second input polynomial of length ctx->n.
 * @param[out] c   Output polynomial of length ctx->n. May alias neither a nor
 *                 b.
 * @param[in] ctx  Initialized NTT context containing the transform parameters
 *                 and the precomputed powers of psi psi^{-1}.
 *
 * @return NTT_OK Multiplication completed successfully.
 * @return NTT_ERROR on errors.
 */
int ntt_negacyclic_mul(uint64_t *a,
                       uint64_t *b,
                       uint64_t *c,
                       const ntt_ctx *ctx);

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
 */
int ntt_forward(const ntt_ctx *ctx, uint64_t *a);

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
 * @return NTT_OK Transform completed successfully.
 * @return NTT_ERROR Invalid input.
 */
int ntt_inverse(const ntt_ctx *ctx, uint64_t *a);

#ifdef __cplusplus
}
#endif

#endif /* NTT_H */
