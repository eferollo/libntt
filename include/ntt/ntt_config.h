/*
 * ntt_config.h
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

#ifndef NTT_CONFIG_H
#define NTT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration object used to create an NTT context.
 */
typedef struct ntt_config_s ntt_config;

/**
 * @brief Convolution type an NTT context is configured for.
 *
 * This determines both which of omega/psi are meaningful and how missing
 * roots of unity are derived when not supplied explicitly:
 *
 *   - NTT_TRANSFORM_NEGACYCLIC: c(x) = a(x)b(x) mod (x^n + 1, q). Requires
 *     psi, a primitive 2n-th root of unity (omega = psi^2 is derived from
 *     it automatically). This is the convolution PQC schemes need and is
 *     the default (value 0), so existing callers that never set this are
 *     unaffected.
 *   - NTT_TRANSFORM_CYCLIC: c(x) = a(x)b(x) mod (x^n - 1, q). Requires only
 *     omega, a primitive n-th root of unity. Setting psi for this type is
 *     an error. There is no twist step in the cyclic procedure, so a psi
 *     value would silently be ignored, which is worse than refusing it.
 *
 * For either type, omega and/or psi may be left unset (0) and will be
 * derived automatically from q and n if the modulus supports the
 * requested transform size.
 */
typedef enum {
    NTT_TRANSFORM_NEGACYCLIC = 0,
    NTT_TRANSFORM_CYCLIC = 1,
} ntt_transform_type;

/**
 * @brief Optional configuration flags for an NTT context.
 */
typedef enum {
    /* Use Barrett reduction for scalar modular multiplication. */
    NTT_CONFIG_REDUCTION_BARRETT = 1u << 0,
    /* Use Montgomery reduction for scalar modular multiplication. */
    NTT_CONFIG_REDUCTION_MONTGOMERY = 1u << 1,
} ntt_config_flags;

/**
 * @brief Creates an empty NTT configuration object.
 *
 * @return Newly allocated ntt_config object.
 * @return NULL on allocation failure.
 */
ntt_config *ntt_config_new(void);

/**
 * @brief Release an NTT configuration object.
 *
 * @param[in] config Configuraton object to release.
 */
void ntt_config_free(ntt_config *config);

/* ntt_config set helper functions */

/**
 * @brief Sets the convolution type (negacyclic or cyclic) for this config.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     type   Convolution type.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_transform_type(ntt_config *config, ntt_transform_type type);

/**
 * @brief Sets the modulus q in the NTT configuration.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     q      Prime modulus.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_modulus(ntt_config *config, uint32_t q);

/**
 * @brief Sets the NTT transform size.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     n      Transform size.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_size(ntt_config *config, uint32_t n);

/**
 * @brief Sets the primitive n-th root of unity.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     omega  Primitive n-th root of unity modulo q.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_omega(ntt_config *config, uint32_t omega);

/**
 * @brief Sets the primitive 2n-th root of unity.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     psi  Primitive 2n-th root of unity modulo q.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_psi(ntt_config *config, uint32_t psi);

/**
 * @brief Sets configuration flags.
 *
 * @param[in,out] config NTT configuration.
 * @param[in]     flags Configuration flags.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_config_set_flags(ntt_config *config, uint32_t flags);

/* ntt_config getter helper functions */

/**
 * @brief Returns the convolution type configured.
 *
 * @param[in] config NTT configuration.
 *
 * @return Convolution type. NTT_TRANSFORM_NEGACYCLIC if config is NULL.
 */
ntt_transform_type ntt_config_get_transform_type(const ntt_config *config);

/**
 * @brief Returns the modulus q.
 *
 * @param[in] config NTT configuration.
 *
 * @return Prime modulus q.
 */
uint32_t ntt_config_get_modulus(const ntt_config *config);

/**
 * @brief Returns the NTT transform size.
 *
 * @param[in] config NTT configuration.
 *
 * @return Transform size n.
 */
uint32_t ntt_config_get_size(const ntt_config *config);

/**
 * @brief Returns the primitive n-th root of unity.
 *
 * @param[in] config NTT configuration.
 *
 * @return Primitive n-th root of unity omega.
 */
uint32_t ntt_config_get_omega(const ntt_config *config);

/**
 * @brief Returns the primitive 2n-th root of unity.
 *
 * @param[in] config NTT configuration.
 *
 * @return Primitive 2n-th root of unity psi.
 */
uint32_t ntt_config_get_psi(const ntt_config *config);

/**
 * @brief Returns the configuration flags.
 *
 * @param[in] config NTT configuration.
 *
 * @return Configuration flags.
 */
uint32_t ntt_config_get_flags(const ntt_config *config);

#ifdef __cplusplus
}
#endif

#endif /* NTT_CONFIG_H */
