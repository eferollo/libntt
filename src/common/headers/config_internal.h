/*
 * config_internal.h
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

#ifndef NTT_CONFIG_INTERNAL_H
#define NTT_CONFIG_INTERNAL_H

#include "ntt/ntt_adapter.h"

/**
 * @brief Internal representation of the opaque public NTT configuration object.
 */
struct ntt_config_s {
    uint64_t q;
    uint32_t n;
    uint64_t omega;
    uint64_t psi;
    uint32_t flags;
    /* defaults to NEGACYCLIC (O) via calloc */
    ntt_transform_type transform_type;
};

#endif /* NTT_CONFIG_INTERNAL_H */
