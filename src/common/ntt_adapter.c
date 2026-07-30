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

#include "ntt_internal.h"

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
