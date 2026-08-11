/*
 * module_internal.h
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

#ifndef NTT_MODULE_INTERNAL_H
#define NTT_MODULE_INTERNAL_H

#include <ntt/ntt_adapter.h>

const ntt_adapter *ntt__registry_lookup(const char *name);
const ntt_adapter *ntt__module_load_from_dir(const char *module_dir,
                                             const char *name);
void ntt__module_unload_all(void);

#endif /* NTT_MODULE_INTERNAL_H */
