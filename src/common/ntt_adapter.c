/*
 * ntt_adapter.c
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

#include "cfg_file_internal.h"
#include "module_internal.h"
#include "ntt_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define NTT_DEFAULT_ADAPTER_NAME_CAP 128
#define NTT_DEFAULT_MODULE_DIR_CAP   4096
#define NTT_DEFAULT_CONFIG_PATH_CAP  4096

/* Default adapter name requested by the caller. */
static char ntt__default_name[NTT_DEFAULT_ADAPTER_NAME_CAP];
/* Default module directory requested by the caller. */
static char ntt__default_module_dir[NTT_DEFAULT_MODULE_DIR_CAP];
/* True once a default adapter has been explicitly requested. */
static bool ntt__default_explicit = false;
/* Cache of the resolved default adapter. */
static const ntt_adapter *ntt__default_cache = NULL;

uint32_t ntt_adapter_get_abi_version(const ntt_adapter *adapter)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return 0;
    }
    return adapter->abi_version;
}

uint32_t ntt_adapter_get_struct_size(const ntt_adapter *adapter)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return 0;
    }
    return adapter->struct_size;
}

const char *ntt_adapter_get_name(const ntt_adapter *adapter)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return NULL;
    }
    return adapter->name;
}

uint32_t ntt_adapter_get_capabilities(const ntt_adapter *adapter)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return 0;
    }
    return adapter->capabilities;
}

uint32_t ntt_adapter_get_supported_flags(const ntt_adapter *adapter)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return 0;
    }
    return adapter->supported_flags;
}

bool ntt_adapter_supports_flags(const ntt_adapter *adapter, uint32_t flags)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return false;
    }
    return (flags & ~adapter->supported_flags) == 0;
}

/**
 * @brief Returns whether a field is present in an adapter descriptor.
 *
 * This helper is used when extending the adapter ABI with fields appended to
 * the descriptor.
 *
 * @param[in] adapter Adapter descriptor.
 * @param[in] offset  Offset of the field in the descriptor.
 * @param[in] size    Size of the field.
 *
 * @return true if the field is available in the descriptor.
 * @return false otherwise.
 */
bool ntt__adapter_has_field(const ntt_adapter *adapter,
                            size_t offset,
                            size_t size)
{
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return false;
    }
    return adapter->struct_size >= offset + size;
}

/**
 * @brief Checks whether an NTT adapter is compatible with the current ABI.
 *
 * An adapter is considered compatible when its ABI version is not newer than
 * the ABI version supported by the library and its structure contains all
 * fields required by the current NTT adapter interface.
 *
 * Older adapter ABI versions are accepted to preserve backward compatibility,
 * provided that the adapter structure is large enough to contain all required
 * fields.
 *
 * @param[in] adapter NTT adapter to validate.
 *
 * @return true if the adapter is compatible with the current ABI.
 * @return false otherwise.
 */
bool ntt__adapter_is_compatible(const ntt_adapter *adapter)
{
    const size_t required_size =
        offsetof(ntt_adapter, negacyclic_mul) + sizeof(adapter->negacyclic_mul);

    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "NTT adapter is NULL");
        return false;
    }

    if (adapter->abi_version > NTT_ADAPTER_ABI_VERSION) {
        NTT_LOG(NTT_LOG_ERROR,
                "Unsupported NTT adapter ABI version: "
                "adapter=%u, maximum_supported=%u",
                adapter->abi_version,
                NTT_ADAPTER_ABI_VERSION);
        return false;
    }

    if (adapter->struct_size < required_size) {
        NTT_LOG(NTT_LOG_ERROR,
                "NTT adapter structure is too small: "
                "adapter_size=%zu, required_size=%zu",
                adapter->struct_size,
                required_size);
        return false;
    }

    return true;
}

/**
 * @brief Clears the process-local default adapter override and its cache.
 *
 * Called by ntt_adapter_unload_all() so the next ntt_adapter_get_default()
 * re-resolves from the configuration file.
 */
void ntt__adapter_reset_default(void)
{
    ntt__default_cache = NULL;
    ntt__default_explicit = false;
    ntt__default_name[0] = '\0';
    ntt__default_module_dir[0] = '\0';
}

/**
 * @brief Resolves a default adapter name/directory pair.
 *
 * Tries the built-in registry, then an external module in @p module_dir, and
 * finally falls back to the built-in "scalar" adapter so the default is never
 * NULL.
 *
 * @param[in] name       Adapter name, or NULL/empty for the fallback.
 * @param[in] module_dir Directory for an external adapter, or NULL.
 *
 * @return Adapter descriptor (never NULL).
 */
static const ntt_adapter *ntt__resolve_default(const char *name,
                                               const char *module_dir)
{
    const ntt_adapter *adapter = NULL;

    if (name == NULL || name[0] == '\0') {
        return ntt_adapter_scalar();
    }

    adapter = ntt__registry_lookup(name);
    if (adapter != NULL) {
        return adapter;
    }

    if (module_dir != NULL && module_dir[0] != '\0') {
        adapter = ntt__module_load_from_dir(module_dir, name);
        if (adapter != NULL) {
            return adapter;
        }
    }

    return ntt_adapter_scalar();
}

const ntt_adapter *ntt_adapter_get(ntt_adapter_selector selector)
{
    switch (selector) {
    case NTT_ADAPTER_SCALAR:
        return ntt_adapter_scalar();
    case NTT_ADAPTER_SCALAR_TOY:
        return ntt_adapter_scalar_toy();
    case NTT_ADAPTER_DEFAULT:
    default:
        return ntt_adapter_get_default();
    }
}

int ntt_adapter_set_default(const char *name, const char *module_dir)
{
    size_t len;
    if (name == NULL || name[0] == '\0') {
        NTT_LOG(NTT_LOG_ERROR, "Invalid default adapter name");
        return NTT_ERROR;
    }

    len = snprintf(ntt__default_name, sizeof(ntt__default_name), "%s", name);
    if (len >= (int)sizeof(ntt__default_name)) {
        NTT_LOG(NTT_LOG_ERROR, "Default adapter name too long");
        return NTT_ERROR;
    }

    if (module_dir != NULL) {
        len = snprintf(ntt__default_module_dir,
                       sizeof(ntt__default_module_dir),
                       "%s",
                       module_dir);
        if (len >= (int)sizeof(ntt__default_module_dir)) {
            NTT_LOG(NTT_LOG_ERROR, "Default adapter module dir too long");
            return NTT_ERROR;
        }
    } else {
        ntt__default_module_dir[0] = '\0';
    }

    ntt__default_explicit = true;
    ntt__default_cache = NULL;
    return NTT_OK;
}

const ntt_adapter *ntt_adapter_get_default(void)
{
    char path[NTT_DEFAULT_CONFIG_PATH_CAP] = {0};
    char name[NTT_DEFAULT_ADAPTER_NAME_CAP] = {0};
    char dir[NTT_DEFAULT_MODULE_DIR_CAP] = {0};
    const char *envdir = NULL;
    const ntt_adapter *adapter = NULL;

    if (ntt__default_cache != NULL) {
        return ntt__default_cache;
    }

    if (ntt__default_explicit) {
        adapter = ntt__resolve_default(ntt__default_name,
                                       ntt__default_module_dir[0] != '\0'
                                           ? ntt__default_module_dir
                                           : NULL);
        ntt__default_cache = adapter;
        return adapter;
    }

    name[0] = '\0';
    dir[0] = '\0';
    if (ntt__config_default_path(path, sizeof(path))) {
        ntt__config_file_load(path, name, sizeof(name), dir, sizeof(dir));
    }

    envdir = getenv("NTT_ADAPTER_MODULE_DIR");
    if (envdir != NULL && envdir[0] != '\0') {
        (void)snprintf(dir, sizeof(dir), "%s", envdir);
    }

    adapter = ntt__resolve_default(name, dir);
    ntt__default_cache = adapter;
    return adapter;
}

const ntt_adapter *ntt_adapter_load(const char *name, const char *module_dir)
{
    const ntt_adapter *adapter = NULL;

    if (name == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid adapter name");
        return NULL;
    }

    if (module_dir == NULL) {
        adapter = ntt__registry_lookup(name);
        if (adapter == NULL) {
            NTT_LOG(NTT_LOG_ERROR, "Unknown built-in adapter: %s", name);
        }
        return adapter;
    }

    adapter = ntt__module_load_from_dir(module_dir, name);
    if (adapter == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "Failed to load adapter module: name=%s dir=%s",
                name,
                module_dir);
    }
    return adapter;
}

void ntt_adapter_unload_all(void)
{
    ntt__module_unload_all();
    ntt__adapter_reset_default();
}
