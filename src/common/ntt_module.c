/*
 * ntt_module.c
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

#include "adapter_internal.h"
#include "core_internal.h"
#include "module_internal.h"
#include "ntt_internal.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTT_MAX_LOADED_MODULES 16

/**
 * @brief A built-in adapter registry entry.
 *
 * Associates the name under which a built-in adapter is selected with the
 * factory returning its immutable descriptor.
 */
typedef struct {
    const char *name;
    const ntt_adapter *(*get)(void);
} ntt_builtin_entry;

/**
 * @brief A handle to a dynamically loaded adapter module.
 *
 * A module is a shared library exposing ntt_adapter_module_init. Handles are
 * kept alive in a small registry (see ntt__loaded) so a module is not
 * unloaded while an adapter returned by it is still in use.
 */
typedef struct {
    void *handle;
} ntt_loaded_module;

/* Registry of currently loaded adapter module handles. */
static ntt_loaded_module ntt__loaded[NTT_MAX_LOADED_MODULES];

static const ntt_builtin_entry ntt__builtin_registry[] = {
    {
        "scalar_toy",
        ntt_adapter_scalar_toy,
    },
    {
        "scalar",
        ntt_adapter_scalar,
    },
    {
        NULL,
        NULL,
    },
};

/**
 * @brief Records a loaded module handle so the module stays resident.
 *
 * @param[in] handle The handle of a successfully loaded module.
 *
 * @return true if the handle was stored in the registry.
 * @return false if the registry is full; the caller must then unload the
 *         module itself.
 */
static bool ntt__module_cache_handle(void *handle)
{
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        if (ntt__loaded[i].handle == NULL) {
            ntt__loaded[i].handle = handle;
            return true;
        }
    }
    return false;
}

/**
 * @brief Validates the integrity of the core API injected into a module.
 *
 * The handshake loosely guards the adapter side of the contract: the injected
 * core must carry the current version (so ntt_core_is_compatible() in a
 * module sees an up-to-date superset), a well-formed dispatch stream, and a
 * sane struct size. A module's own ntt_core_is_compatible() check remains the
 * actual version gate; this rejects a broken core defensively.
 */
static bool ntt__module_core_is_valid(void)
{
    const ntt_core_api *core = ntt__core_api();
    const size_t ops_field_end =
        offsetof(ntt_core_api, ops) + sizeof(ntt_dispatch *);

    /* struct_size must at least cover the ops pointer field. */
    if (core == NULL || core->version != NTT_CORE_API_VERSION ||
        core->struct_size < ops_field_end || core->ops == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "Injected core API is malformed (version=%u, ops=%p).",
                core ? core->version : 0u,
                (void *)(core ? (void *)core->ops : NULL));
        return false;
    }
    return true;
}

/**
 * @brief Looks up a built-in adapter by name in the static registry.
 *
 * @param[in] name Adapter name ("scalar_toy", "scalar", ...).
 *
 * @return Built-in adapter descriptor, or NULL if unknown.
 */

const ntt_adapter *ntt__registry_lookup(const char *name)
{
    const ntt_builtin_entry *e = NULL;
    if (name == NULL) {
        return NULL;
    }
    for (e = ntt__builtin_registry; e->name != NULL; ++e) {
        if (strcmp(name, e->name) == 0) {
            return e->get();
        }
    }
    return NULL;
}

/**
 * @brief Loads an external adapter module from a directory.
 *
 * @param[in] module_dir Directory to search.
 * @param[in] name       Module (adapter) name.
 *
 * @return Adapter descriptor, or NULL on failure.
 */
const ntt_adapter *ntt__module_load_from_dir(const char *module_dir,
                                             const char *name)
{
    size_t len;
    char *path = NULL;
    void *handle = NULL;
    union {
        void *obj;
        int (*fn)(const ntt_core_api *, const ntt_adapter **);
    } entry;
    const ntt_adapter *adapter = NULL;

    len = strlen(module_dir) + strlen(ntt__dl_separator()) +
          strlen(ntt__dl_prefix()) + strlen("ntt_adapter_") + strlen(name) +
          strlen(ntt__dl_extension()) + 1;
    path = calloc(len, 1);
    if (path == NULL) {
        return NULL;
    }
    snprintf(path,
             len,
             "%s%s%sntt_adapter_%s%s",
             module_dir,
             ntt__dl_separator(),
             ntt__dl_prefix(),
             name,
             ntt__dl_extension());

    handle = ntt__dlopen(path);
    if (handle == NULL) {
        SAFE_FREE(path);
        return NULL;
    }

    entry.obj = ntt__dlsym(handle, NTT_ADAPTER_MODULE_ENTRY);
    if (entry.fn == NULL) {
        ntt__dlclose(handle);
        SAFE_FREE(path);
        return NULL;
    }

    if (ntt__module_core_is_valid() == false) {
        ntt__dlclose(handle);
        SAFE_FREE(path);
        return NULL;
    }

    if (entry.fn(ntt__core_api(), &adapter) != 0 || adapter == NULL ||
        ntt__adapter_is_compatible(adapter) == false) {
        ntt__dlclose(handle);
        SAFE_FREE(path);
        return NULL;
    }

    if (ntt__module_cache_handle(handle) == false) {
        ntt__dlclose(handle);
    }
    SAFE_FREE(path);
    return adapter;
}

/**
 * @brief Releases every handle tracked by the loaded-module registry.
 */
void ntt__module_unload_all(void)
{
    for (size_t i = 0; i < NTT_MAX_LOADED_MODULES; ++i) {
        if (ntt__loaded[i].handle != NULL) {
            NTT_LOG(NTT_LOG_INFO,
                    "Unloading adapter module %zu/%d",
                    i,
                    NTT_MAX_LOADED_MODULES);
            ntt__dlclose(ntt__loaded[i].handle);
            ntt__loaded[i].handle = NULL;
        }
    }
}
