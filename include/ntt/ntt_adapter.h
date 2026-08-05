/*
 * ntt_adapter.h
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

#ifndef NTT_ADAPTER_H
#define NTT_ADAPTER_H

#include "ntt_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Current NTT Adapter ABI version.
 *
 * The ABI version is monotonically increasing. A library may accept Adapters
 * using an older ABI version, provided that their structure contains all
 * fields required by the current interface.
 *
 * An Adapter using a newer ABI version is rejected because the current
 * library may not understand its ABI contract.
 */
#define NTT_ADAPTER_ABI_VERSION 0x01

/**
 * @brief Validates whether a modulus is supported by an NTT Adapter.
 *
 * @param[in] config NTT configuration.
 *
 * @return true if config's q is supported by the Adapter.
 * @return false otherwise.
 */
typedef bool (*ntt_validate_modulus_fn)(const ntt_config *config);

/**
 * @brief Initializes Adapter-specific state for an NTT context.
 *
 * The returned state is owned by the NTT context and is passed back to all
 * Adapter operations. The common NTT library never inspects the contents of
 * the state. Its representation is entirely private to the Adapter.
 *
 * @param[in] config NTT configuration.
 *
 * @return Newly allocated Adapter-specific state.
 * @return NULL on failure.
 */
typedef void *(*ntt_adapter_setup_fn)(const ntt_config *config);

/**
 * @brief Releases Adapter-specific state owned by an NTT context.
 *
 * @param[in] state Adapter-specific state returned by the setup callback.
 */
typedef void (*ntt_adapter_teardown_fn)(void *state);

/**
 * @brief Computes a forward NTT using Adapter-specific state.
 *
 * The transform is performed in-place.
 *
 * @param[in] state Adapter-specific state.
 * @param[in,out] a Array of NTT coefficients.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on failure.
 */
typedef int (*ntt_adapter_forward_fn)(void *state, uint64_t *a);

/**
 * @brief Computes an inverse NTT using Adapter-specific state.
 *
 * The inverse transform is performed in-place.
 *
 * @param[in] state Adapter-specific state.
 * @param[in,out] a Array of NTT coefficients.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on failure.
 */
typedef int (*ntt_adapter_inverse_fn)(void *state, uint64_t *a);

/**
 * @brief Multiplies two polynomials using the Adapter-specific negacyclic NTT.
 *
 * Computes the product
 * @f[
 * c(x) = a(x)b(x) \bmod (x^n + 1, q).
 * @f]
 *
 * The Adapter is responsible for performing the complete multiplication using
 * its internal representation and arithmetic implementation.
 *
 * @param[in] state Adapter-specific state.
 * @param[in] a     First input polynomial.
 * @param[in] b     Second input polynomial.
 * @param[out] c    Output polynomial.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on failure.
 */
typedef int (*ntt_adapter_negacyclic_mul_fn)(void *state,
                                             uint64_t *a,
                                             uint64_t *b,
                                             uint64_t *c);

/**
 * @brief Describes an NTT Adapter implementation.
 *
 * The Adapter provides the implementation-specific operations used by the
 * common NTT library. Its internal state and arithmetic representation are
 * completely opaque to the common library.
 *
 * The structure is part of the NTT Adapter ABI. New fields must only be
 * appended to the structure to preserve binary compatibility with older
 * Adapter implementations.
 *
 * The "abi_version" field identifies the ABI contract implemented by the
 * Adapter, while "struct_size" specifies how many bytes of the structure
 * are available. This allows newer versions of the NTT library to remain
 * compatible with older Adapter implementations.
 */
struct ntt_adapter_s {
    uint32_t abi_version;
    uint32_t struct_size;

    const char *name;

    uint32_t capabilities;
    uint32_t supported_flags;

    ntt_validate_modulus_fn validate_modulus;
    ntt_adapter_setup_fn setup;
    ntt_adapter_teardown_fn teardown;
    ntt_adapter_forward_fn forward;
    ntt_adapter_inverse_fn inverse;
    ntt_adapter_negacyclic_mul_fn negacyclic_mul;
};

/**
 * @brief Describe the capabilities provided by an NTT adapter.
 */
typedef enum {
    /* Adapter supports runtime-selected moduli. */
    NTT_CAP_RUNTIME_MODULUS = 1u << 0,
    /* Adapter requires a fixed or restricted modulus set. */
    NTT_CAP_FIXED_MODULUS = 1u << 1,
    /* Adapter provides SIMD-optimized operations. */
    NTT_CAP_SIMD = 1u << 2,
    /* Adapter uses Montgomery-domain arithmetic internally. */
    NTT_CAP_MONTGOMERY = 1u << 3,
    /* Adapter uses Barrett modular reduction internally. */
    NTT_CAP_BARRETT = 1u << 4,
    /* Adapter uses Shoup precomputation internally. */
    NTT_CAP_SHOUP = 1u << 5,
} ntt_adapter_capabilities;

/**
 * @brief Opaque handle identifying one NTT adapter.
 *
 * An adapter provides a complete implementation of the NTT operations. Its
 * internal arithmetic, coefficient representation, precomputation strategy,
 * and hardware-specific optimizations are hidden from the common library.
 */
typedef struct ntt_adapter_s ntt_adapter;

/**
 * @brief Returns the ABI version required by an NTT adapter descriptor.
 *
 * @param[in] adapter NTT adapter.
 *
 * @return Adapter's ABI version,
 * @return 0 i adapter is NULL.
 */
uint32_t ntt_adapter_get_abi_version(const ntt_adapter *adapter);

/**
 * @brief Returns the size of an NTT adapter descriptor.
 *
 * @param[in] adapter NTT adapter.
 *
 * @return Descriptor size expressed in bytes.
 * @return 0 if adapter is NULL.
 */
uint32_t ntt_adapter_get_struct_size(const ntt_adapter *adapter);

/**
 * @brief Returns the name of an NTT Adapter.
 *
 * @param[in] adapter NTT Adapter.
 *
 * @return Null-terminated Adapter name.
 */
const char *ntt_adapter_get_name(const ntt_adapter *adapter);

/**
 * @brief Returns the capabilities supported by an NTT Adapter.
 *
 * @param[in] adapter NTT Adapter.
 *
 * @return Bitmask of supported Adapter capabilities.
 */
uint32_t ntt_adapter_get_capabilities(const ntt_adapter *adapter);

/**
 * @brief Returns the configuration flags supported by an NTT Adapter.
 *
 * @param[in] adapter NTT Adapter.
 *
 * @return Bitmask of supported configuration flags.
 */
uint32_t ntt_adapter_get_supported_flags(const ntt_adapter *adapter);

/**
 * @brief Checks whether an NTT Adapter supports the specified flags.
 *
 * @param[in] adapter NTT Adapter.
 * @param[in] flags   Configuration flags to check.
 *
 * @return true if all specified flags are supported.
 * @return false otherwise.
 */
bool ntt_adapter_supports_flags(const ntt_adapter *adapter, uint32_t flags);

/* ADAPTERS LIST */

/**
 * @brief Generic, runtime-modulus scalar adapter. Uses plain `%` for
 *        reduction, prioritizes clarity/correctness over speed, and
 *        is the reference every other adapter is checked against.
 */
const ntt_adapter *ntt_adapter_scalar_toy(void);

#ifdef __cplusplus
}
#endif

#endif /* NTT_ADAPTER_H */
