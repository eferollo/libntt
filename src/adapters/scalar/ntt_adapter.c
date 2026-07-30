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
#include "ntt_scalar_internal.h"

/**
 * @brief Returns the optimized scalar NTT adapter.
 *
 * The adapter implements radix-2 Cooley-Tukey forward NTT and radix-2
 * Gentleman-Sande inverse NTT. Barrett reduction is the default arithmetic
 * mode. Montgomery reduction can be requested with
 * NTT_CONFIG_REDUCTION_MONTGOMERY.
 *
 * @return Pointer to the scalar adapter descriptor.
 */
const ntt_adapter *ntt_adapter_scalar(void)
{
    static const ntt_adapter adapter = {
        .abi_version = NTT_ADAPTER_ABI_VERSION,
        .struct_size = sizeof(ntt_adapter),
        .name = "scalar",
        .capabilities =
            NTT_CAP_RUNTIME_MODULUS | NTT_CAP_BARRETT | NTT_CAP_MONTGOMERY,
        .supported_flags = NTT_CONFIG_REDUCTION_BARRETT |
                           NTT_CONFIG_REDUCTION_MONTGOMERY,
        .validate_modulus = ntt__scalar_validate_modulus,
        .setup = ntt__scalar_adapter_setup,
        .teardown = ntt__scalar_adapter_teardown,
        .forward = ntt__scalar_forward,
        .inverse = ntt__scalar_inverse,
        .negacyclic_mul = ntt__scalar_negacyclic_mul,
    };

    return &adapter;
}
