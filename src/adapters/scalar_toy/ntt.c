/*
 * ntt.c
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

#include "ntt/ntt_config.h"
#include "ntt_scalar_toy_internal.h"
#include <string.h>

/**
 * @brief Reorders an array into bit-reversed order.
 *
 * @param[in,out] a    Pointer to the array to permute.
 * @param[in] n        Number of elements in the array. Must be a power of two.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR if the input array pointer is NULL.
 */
static int bitrev_permute(uint64_t *a, uint32_t n)
{
    if (a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid argument");
        return NTT_ERROR;
    }

    for (uint32_t i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
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
 * @brief Initializes the scalar toy adapter state.
 *
 * @param[in] config NTT parameters and optional adapter configuration flags.
 *
 * @return Newly allocated scalar toy adapter state.
 * @return NULL on failure.
 */
void *ntt__adapter_setup(const ntt_config *config)
{
    uint64_t q, omega, psi;
    uint32_t n;
    if (config == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid scalar toy adapter configuration");
        return NULL;
    }

    q = ntt_config_get_modulus(config);
    n = ntt_config_get_size(config);
    omega = ntt_config_get_omega(config);
    psi = ntt_config_get_psi(config);

    ntt_scalar_toy_state *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "scalar toy state allocation failed");
        return NULL;
    }

    state->q = q;
    state->n = n;
    state->omega = ntt__reduce(omega, q);
    state->psi = ntt__reduce(psi, q);

    for (uint32_t t = n; t > 1; t >>= 1) {
        state->stages++;
    }

    state->omega_inv = ntt__modinv(state->omega, q);
    state->n_inv = ntt__modinv(ntt__reduce(n, q), q);
    state->psi_inv = ntt__modinv(state->psi, q);

    state->psi_pow = calloc(n, sizeof(uint64_t));
    state->psi_inv_pow = calloc(n, sizeof(uint64_t));
    if (state->psi_pow == NULL || state->psi_inv_pow == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "scalar toy psi table allocation failed (n=%u)",
                n);
        ntt__adapter_teardown(state);
        return NULL;
    }

    state->psi_pow[0] = 1;
    state->psi_inv_pow[0] = 1;

    for (uint32_t i = 1; i < n; i++) {
        state->psi_pow[i] = ntt__mulmod(state->psi_pow[i - 1], state->psi, q);
        state->psi_inv_pow[i] =
            ntt__mulmod(state->psi_inv_pow[i - 1], state->psi_inv, q);
    }

    return state;
}

/**
 * @brief Releases scalar toy adapter state.
 *
 * @param[in] state_ptr Adapter-specific state to release.
 */
void ntt__adapter_teardown(void *state_ptr)
{
    ntt_scalar_toy_state *state = state_ptr;
    if (state == NULL) {
        return;
    }

    SAFE_FREE(state->psi_pow);
    SAFE_FREE(state->psi_inv_pow);
    ZERO_STRUCTP(state);
    SAFE_FREE(state);
}

/**
 * @brief Validates a modulus for the scalar toy arithmetic adapter.
 *
 * @param[in] config NTT configuration.
 *
 * @return true if q is a valid modulus for this adapter.
 * @return false otherwise.
 */
bool ntt__validate_modulus(const ntt_config *config)
{
    uint64_t q = ntt_config_get_modulus(config);
    return q > 1;
}

/**
 * @brief Performs an in-place iterative radix-2 Number Theoretic Transform
 * (NTT).
 *
 * @param[in] state Backend-specific state.containing the transform size (n)
 *                  and modulus (q).
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
static int iterative_fft(ntt_scalar_toy_state *state,
                         uint64_t *a,
                         uint64_t root,
                         uint64_t scale)
{
    int rc;
    uint32_t n = state->n;
    uint64_t q = state->q;

    rc = bitrev_permute(a, n);
    if (rc != NTT_OK) {
        NTT_LOG(NTT_LOG_ERROR, "Bit reversal failed");
        return NTT_ERROR;
    }

    for (uint32_t m = 2; m <= n; m <<= 1) {
        uint64_t w_m = ntt__modpow(root, n / m, q);
        for (uint32_t k = 0; k < n; k += m) {
            uint64_t w = 1;
            for (uint32_t j = 0; j < m / 2; j++) {
                uint64_t t = ntt__mulmod(a[k + j + m / 2], w, q);
                uint64_t u = a[k + j];
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
 * @param[in] state_ptr Backend-specific state.
 * @param[in,out] a Array of ctx->n coefficients. On success, it contains the
 *                  forward NTT of the input.
 *
 * @return NTT_OK  Transform completed successfully.
 * @return NTT_ERROR Invalid input.
 */
int ntt__forward(void *state_ptr, uint64_t *a)
{
    if (state_ptr == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }
    ntt_scalar_toy_state *state = state_ptr;
    return iterative_fft(state, a, state->omega, 1);
}

/**
 * @brief Computes the inverse Number Theoretic Transform (INTT).
 *
 * @param[in] state_ptr Backend-specific state.
 * @param[in,out] a Array of ctx->n coefficients. On success, it contains the
 *                  inverse NTT of the input.
 *
 * @return NTT_OK  Transform completed successfully.
 * @return NTT_ERROR Invalid input.
 */
int ntt__inverse(void *state_ptr, uint64_t *a)
{
    if (state_ptr == NULL || a == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }
    ntt_scalar_toy_state *state = state_ptr;
    return iterative_fft(state, a, state->omega_inv, state->n_inv);
}

/**
 * @brief Multiplies two polynomials using the negacyclic Number Theoretic
 * Transform (NTT).
 *
 * @param[in] state_ptr Backend-specific state.
 * @param[in] a  First input polynomial of length ctx->n.
 * @param[in] b  Second input polynomial of length ctx->n.
 * @param[out] c Output polynomial of length ctx->n. May alias neither a nor
 *               b.
 *
 * @return NTT_OK Multiplication completed successfully.
 * @return NTT_ERROR on errors.
 */
int ntt__negacyclic_mul(void *state_ptr, uint64_t *a, uint64_t *b, uint64_t *c)
{
    if (state_ptr == NULL || a == NULL || b == NULL || c == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid arguments");
        return NTT_ERROR;
    }

    ntt_scalar_toy_state *state = state_ptr;
    uint32_t n = state->n;
    uint64_t q = state->q;
    int rc;

    uint64_t *ta = calloc(n, sizeof(uint64_t));
    uint64_t *tb = calloc(n, sizeof(uint64_t));
    if (ta == NULL || tb == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Error while allocating twisted a and b");
        rc = NTT_ERROR;
        goto cleanup;
    }

    /* Step 1: twist by psi^i */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__mulmod(a[i], state->psi_pow[i], q);
        tb[i] = ntt__mulmod(b[i], state->psi_pow[i], q);
    }

    /* Step 2: foward NTT (cyclic) on each */
    rc = ntt__forward(state, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }
    rc = ntt__forward(state, tb);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 3: pointwise multiply, reuse ta as the output buffer */
    for (uint32_t i = 0; i < n; i++) {
        ta[i] = ntt__mulmod(ta[i], tb[i], q);
    }

    /* Step 4: inverse NTT */
    rc = ntt__inverse(state, ta);
    if (rc != NTT_OK) {
        goto cleanup;
    }

    /* Step 5: untwist by psi^{-i} to undo step 1 */
    for (uint32_t i = 0; i < n; i++) {
        c[i] = ntt__mulmod(ta[i], state->psi_inv_pow[i], q);
    }

cleanup:
    SAFE_FREE(ta);
    SAFE_FREE(tb);
    return rc;
}