/*
 * adapter_internal.h
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

#ifndef NTT_ADAPTER_INTERNAL_H
#define NTT_ADAPTER_INTERNAL_H

#include <ntt/ntt_adapter.h>
#include <stddef.h>

bool ntt__adapter_has_field(const ntt_adapter *adapter,
                            size_t offset,
                            size_t size);
bool ntt__adapter_is_compatible(const ntt_adapter *adapter);
void ntt__adapter_reset_default(void);

/*
 * Getters for the adapters compiled into the library. Both built-in adapters
 * are internal: ntt_adapter_scalar_toy (the plain-`%` reference) and
 * ntt_adapter_scalar (the optimized one). They are surfaced here so the
 * built-in adapter registry can refer to both uniformly.
 *
 * Users select a built-in through the public ntt_adapter_get() selector or
 * ntt_adapter_load()/config file by name.
 */
const ntt_adapter *ntt_adapter_scalar_toy(void);
const ntt_adapter *ntt_adapter_scalar(void);

#endif /* NTT_ADAPTER_INTERNAL_H */
