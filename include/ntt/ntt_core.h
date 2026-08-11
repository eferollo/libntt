/*
 * ntt_core.h
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

#ifndef NTT_CORE_H
#define NTT_CORE_H

#include "ntt_config.h"
#include "ntt_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Core dispatch API.
 *
 * Terminology:
 *   - adapter: an ntt_adapter descriptor, a vtable of NTT operations
 *     (setup, forward, inverse, ...). Every backend, built-in or
 *     third-party, is an adapter.
 *   - module: a standalone shared library loaded at run time that delivers
 *     an external adapter via ntt_adapter_module_init. Built-in adapters
 *     have no module.
 *
 * The NTT library injects a bundle of core services (function pointers) into
 * every adapter. Built-in and dynamically loaded adapters alike query the
 * opaque "ntt_config" through this bundle, and may also call the utility
 * functions it exposes, the same way.
 *
 * This is what lets an external adapter be shipped as a module that links
 * nothing against libntt: the dispatch table is handed to the module's entry
 * point, and the module builds its adapter on the core's accessors instead
 * of calling into the library directly.
 ****************************************************************************/

/**
 * @brief Version of the injected adapter core API.
 *
 * The core is extended by appending ids only, so the version grows
 * monotonically and a newer core is always a strict superset of an older one.
 *
 * Compatibility rule: a module built against core version @p Vm is compatible
 * with a library whose `core->version >= Vm`. A newer library core is a
 * superset and serves an older module; an older library core is missing ids
 * the module references, so loading is rejected. See ntt_core_is_compatible().
 */
#define NTT_CORE_API_VERSION 0x01

/**
 * @brief Function identifiers in the adapter core API dispatch stream.
 *
 * Every id has a fixed, well-known signature, summarised here:
 *
 *  - NTT_FUNC_CONFIG_GET_MODULUS         uint64_t (*)(const ntt_config *)
 *  - NTT_FUNC_CONFIG_GET_SIZE            uint32_t (*)(const ntt_config *)
 *  - NTT_FUNC_CONFIG_GET_TRANSFORM_TYPE  ntt_transform_type (*)(const
 *                                                               ntt_config *)
 *  - NTT_FUNC_CONFIG_GET_FLAGS           uint32_t (*)(const ntt_config *)
 *  - NTT_FUNC_CONFIG_GET_OMEGA           uint64_t (*)(const ntt_config *)
 *  - NTT_FUNC_CONFIG_GET_PSI             uint64_t (*)(const ntt_config *)
 *  - NTT_FUNC_UTIL_IS_PRIME              bool (*)(uint64_t)
 */
typedef enum {
    /* stream terminator */
    NTT_FUNC_NONE = 0,

    /* config accessors */
    NTT_FUNC_CONFIG_GET_MODULUS = 1,
    NTT_FUNC_CONFIG_GET_SIZE = 2,
    NTT_FUNC_CONFIG_GET_TRANSFORM_TYPE = 3,
    NTT_FUNC_CONFIG_GET_FLAGS = 4,
    NTT_FUNC_CONFIG_GET_OMEGA = 5,
    NTT_FUNC_CONFIG_GET_PSI = 6,

    /* utility helpers */
    NTT_FUNC_UTIL_IS_PRIME = 7,
} ntt_dispatch_id;

/**
 * @brief A single entry of the injected adapter core API dispatch stream.
 */
typedef struct {
    ntt_dispatch_id id;
    void (*fn)(void);
} ntt_dispatch;

/**
 * @brief The injected adapter core API handed to every adapter.
 *
 * @p ops is a NULL-terminated stream of {@ref ntt_dispatch} entries.
 */
typedef struct {
    uint32_t version;        /* NTT_CORE_API_VERSION */
    uint32_t struct_size;    /* Number of bytes available in this table */
    const ntt_dispatch *ops; /* NULL-terminated stream of dispatch entries */
} ntt_core_api;

/**
 * @brief Platform symbol-visibility macro used to export a module entry point.
 *
 * Use to prefix a matching implementation of ntt_adapter_module_init.
 */
#if defined(_WIN32)
#define NTT_MODULE_EXPORT __declspec(dllexport)
#else
#define NTT_MODULE_EXPORT __attribute__((visibility("default")))
#endif /* _WIN32 */

/**
 * @brief Name of the module entry point a shared adapter library must export.
 *
 * A module shared library must export a function named
 * ntt_adapter_module_init with this signature:
 *
 * @code
 * NTT_MODULE_EXPORT int ntt_adapter_module_init(const ntt_core_api *core,
 *                                               const ntt_adapter **out);
 * @endcode
 *
 * It returns 0 (or a negative error code) and, on success, sets @p out to a
 * populated ntt_adapter descriptor.
 *
 * A module must first reject an injected core that does not meet its
 * requirement, e.g.,
 *
 * @code
 * if (!ntt_core_is_compatible(core, NTT_CORE_API_VERSION)) {
 *     return -1;    // library core too old for this module
 * }
 * @endcode
 *
 * @see ntt_core_is_compatible().
 * @see ntt_adapter.h.
 */
#define NTT_ADAPTER_MODULE_ENTRY "ntt_adapter_module_init"

/*****************************************************************************
 * Typed accessors. Adapters use these. They never hand-cast the opaque
 * dispatch function pointers themselves. The casts live here, once per id.
 *
 * NOTE: for a successfully loaded module, ntt_core_find() on any id the
 * module references is present and has a non-NULL fn. This holds because:
 * - (a) The library's core table is complete (a compile-time assert in the
 *       library guarantees it).
 * - (b) The load-time version handshake rejects a module whose required core
 *       version exceeds the injected core's.
 * Hence the accessors below dereference the found entry directly, with no NULL
 * fallback.
 ****************************************************************************/

/**
 * @brief Looks up the dispatch entry with the given id, or NULL.
 *
 * @param[in] api The injected core API.
 * @param[in] id  Function identifier to look up.
 *
 * @return Matching dispatch entry, or NULL if absent.
 */
static inline const ntt_dispatch *ntt_core_find(const ntt_core_api *api,
                                                ntt_dispatch_id id)
{
    const ntt_dispatch *d = NULL;
    if (api == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Core API is NULL");
        return NULL;
    }
    for (d = api->ops; d != NULL && d->id != NTT_FUNC_NONE; ++d) {
        if (d->id == id) {
            return d;
        }
    }
    NTT_LOG(NTT_LOG_ERROR,
            "Core dispatch entry not found: id=%d (is the core too old?)",
            (int)id);
    return NULL;
}

/**
 * @brief Checks whether an injected core satisfies a module's requirement.
 *
 * The core API is append-only (see NTT_CORE_API_VERSION), so compatibility is
 * "library core >= module's compiled version". A module calls this first in
 * its entry point and must refuse to load when it is false; the loader relies
 * on that non-zero return to reject the module.
 *
 * @param[in] api     Injected core API.
 * @param[in] min_version Minimum core version the module requires (typically
 *                        NTT_CORE_API_VERSION as baked into the module).
 *
 * @return true if the injected core provides at least @p min_version.
 */
static inline bool ntt_core_is_compatible(const ntt_core_api *api,
                                          uint32_t min_version)
{
    if (api == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Core API is NULL");
        return false;
    }
    if (api->version < min_version) {
        NTT_LOG(NTT_LOG_ERROR,
                "Injected core is too old: required=%u, found=%u",
                min_version,
                api->version);
        return false;
    }
    return true;
}

/**
 * @brief Reads the configured modulus through the injected core API.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return Modulus q.
 */
static inline uint64_t ntt_core_get_modulus(const ntt_core_api *api,
                                            const ntt_config *config)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_MODULUS);
    return ((uint64_t (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Reads the configured transform size through the injected core API.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return Transform size n.
 */
static inline uint32_t ntt_core_get_size(const ntt_core_api *api,
                                         const ntt_config *config)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_SIZE);
    return ((uint32_t (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Reads the configured transform type through the injected core API.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return Transform type.
 */
static inline ntt_transform_type
ntt_core_get_transform_type(const ntt_core_api *api, const ntt_config *config)
{
    const ntt_dispatch *d =
        ntt_core_find(api, NTT_FUNC_CONFIG_GET_TRANSFORM_TYPE);
    return ((ntt_transform_type (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Reads the configured flags through the injected core API.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return Configuration flags.
 */
static inline uint32_t ntt_core_get_flags(const ntt_core_api *api,
                                          const ntt_config *config)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_FLAGS);
    return ((uint32_t (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Reads the configured primitive n-th root of unity.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return omega.
 */
static inline uint64_t ntt_core_get_omega(const ntt_core_api *api,
                                          const ntt_config *config)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_OMEGA);
    return ((uint64_t (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Reads the configured primitive 2n-th root of the unity.
 *
 * @param[in] api    Injected core API.
 * @param[in] config NTT configuration.
 *
 * @return psi.
 */
static inline uint64_t ntt_core_get_psi(const ntt_core_api *api,
                                        const ntt_config *config)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_PSI);
    return ((uint64_t (*)(const ntt_config *))d->fn)(config);
}

/**
 * @brief Tests primality through the injected core API.
 *
 * @param[in] api Injected core API.
 * @param[in] q   Value to test.
 *
 * @return true if q is prime.
 */
static inline bool ntt_core_is_prime(const ntt_core_api *api, uint64_t q)
{
    const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_UTIL_IS_PRIME);
    return ((bool (*)(uint64_t))d->fn)(q);
}

#ifdef __cplusplus
}
#endif

#endif /* NTT_CORE_H */
