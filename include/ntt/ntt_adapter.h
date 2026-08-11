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
#include "ntt_core.h"

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
 * @param[in] api    Core API injected by the library, used to read the
 *                   (opaque) configuration.
 *
 * @return true if config's q is supported by the Adapter.
 * @return false otherwise.
 */
typedef bool (*ntt_validate_modulus_fn)(const ntt_config *config,
                                        const ntt_core_api *api);

/**
 * @brief Initializes Adapter-specific state for an NTT context.
 *
 * The returned state is owned by the NTT context and is passed back to all
 * Adapter operations. The common NTT library never inspects the contents of
 * the state. Its representation is entirely private to the Adapter.
 *
 * @param[in] config NTT configuration.
 * @param[in] api    Core API injected by the library, used to read the
 *                   (opaque) configuration.
 *
 * @return Newly allocated Adapter-specific state.
 * @return NULL on failure.
 */
typedef void *(*ntt_adapter_setup_fn)(const ntt_config *config,
                                      const ntt_core_api *api);

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
 * @brief Selector for the built-in adapter registry.
 *
 * Used to switch among adapters compiled into the library explicitly. When
 * selecting through the configuration file, the plain @em name string of the
 * built-in is used instead (e.g., "scalar", "scalar_toy").
 */
typedef enum {
    NTT_ADAPTER_DEFAULT = 0, /* resolve to the library default */
    NTT_ADAPTER_SCALAR,      /* built-in optimized adapter */
    NTT_ADAPTER_SCALAR_TOY,  /* built-in reference adapter */
} ntt_adapter_selector;

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

/**
 * @brief Returns the NTT adapter named @p name.
 *
 * An adapter is delivered either built into the library or, for external
 * implementations, as a module: a standalone shared library loaded at run
 * time that exports ntt_adapter_module_init. See ntt_core_api.h for the
 * terminology.
 *
 * Built-in adapters are selected by name and resolved through the library's
 * internal registry; their getters are not part
 * of the public API. Select one explicitly with ntt_adapter_get() or via the
 * configuration file instead.
 *
 * If @p module_dir is NULL the built-in registry is queried. Otherwise the
 * shared library <module_dir>/libntt_adapter_<name>.so is loaded at run time
 * and its exported ntt_adapter_module_init entry is invoked to obtain the
 * adapter descriptor.
 *
 * @param[in] name       Adapter name (e.g., "scalar_toy", "scalar").
 * @param[in] module_dir Directory containing the adapter module, or NULL to
 *                       select a built-in adapter.
 *
 * @return Adapter descriptor.
 * @return NULL if the adapter cannot be found or loaded.
 */
const ntt_adapter *ntt_adapter_load(const char *name, const char *module_dir);

/**
 * @brief Selects an adapter by built-in selector.
 *
 * @param[in] selector Built-in adapter selector.
 *
 * @return Adapter descriptor. An out-of-range @p selector falls back to the
 *         library default (see ntt_adapter_get_default()), so this never
 *         returns NULL.
 */
const ntt_adapter *ntt_adapter_get(ntt_adapter_selector selector);

/**
 * @brief Sets the process-local default adapter override.
 *
 * When set, this takes precedence over the configuration file. Pass a non-NULL
 * @p name for a built-in or external adapter; @p module_dir is required only
 * for a non-built-in (external) name. The values are copied.
 *
 * @param[in] name       Adapter name.
 * @param[in] module_dir Directory containing an external adapter module, or
 *                       NULL for a built-in adapter.
 *
 * @return NTT_OK on success.
 * @return NTT_ERROR on invalid input.
 */
int ntt_adapter_set_default(const char *name, const char *module_dir);

/**
 * @brief Returns the library's default adapter.
 *
 * Resolution order, from highest to lowest precedence:
 *   1. the adapter set via ntt_adapter_set_default();
 *   2. the adapter named by the global configuration file;
 *   3. the built-in "scalar" adapter.
 *
 * The configuration file path is given by the NTT_CONFIG_FILE environment
 * variable, defaulting to $HOME/.config/libntt/ntt.conf. Its "adapter" and
 * "module_dir" keys select the adapter. The NTT_ADAPTER_MODULE_DIR environment
 * variable overrides "module_dir". The resolved adapter is cached until
 * ntt_adapter_unload_all() is called. If nothing resolves explicitly, the
 * built-in "scalar" adapter is returned.
 *
 * @return The default adapter descriptor (never NULL).
 */
const ntt_adapter *ntt_adapter_get_default(void);

/**
 * @brief Releases all dynamically loaded adapter modules.
 *
 * Existing ntt_ctx objects created from a dynamically loaded adapter must be
 * destroyed before calling this function. It also resets the default adapter
 * resolution state: both the cached default and any override installed by
 * ntt_adapter_set_default() are cleared, so the next ntt_adapter_get_default()
 * call re-resolves from the configuration file.
 */
void ntt_adapter_unload_all(void);

#ifdef __cplusplus
}
#endif

#endif /* NTT_ADAPTER_H */
