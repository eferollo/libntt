/*
 * ntt_core.c
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

#include "core_internal.h"
#include "ntt_internal.h"

#include <stddef.h>

#if defined(_WIN32)
#include <windows.h>
#define NTT_DL_EXT ".dll"
#else
#include <dlfcn.h>
#define NTT_DL_EXT ".so"
#endif

void *ntt__dlopen(const char *path)
{
#if defined(_WIN32)
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *ntt__dlsym(void *handle, const char *name)
{
#if defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}

void ntt__dlclose(void *handle)
{
#if defined(_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

/** @brief Dynamic library filename extension (".dll" or ".so"). */
const char *ntt__dl_extension(void)
{
    return NTT_DL_EXT;
}

/** @brief Dynamic library filename prefix ("lib" on Unix, "" on Windows). */
const char *ntt__dl_prefix(void)
{
#if defined(_WIN32)
    return "";
#else
    return "lib";
#endif
}

/** @brief Path separator ("/" on Unix, "\\" on Windows). */
const char *ntt__dl_separator(void)
{
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}

static const ntt_dispatch ntt__core_ops[] = {
    {
        NTT_FUNC_CONFIG_GET_MODULUS,
        (void (*)(void))ntt_config_get_modulus,
    },
    {
        NTT_FUNC_CONFIG_GET_SIZE,
        (void (*)(void))ntt_config_get_size,
    },
    {
        NTT_FUNC_CONFIG_GET_TRANSFORM_TYPE,
        (void (*)(void))ntt_config_get_transform_type,
    },
    {
        NTT_FUNC_CONFIG_GET_FLAGS,
        (void (*)(void))ntt_config_get_flags,
    },
    {
        NTT_FUNC_CONFIG_GET_OMEGA,
        (void (*)(void))ntt_config_get_omega,
    },
    {
        NTT_FUNC_CONFIG_GET_PSI,
        (void (*)(void))ntt_config_get_psi,
    },
    {
        NTT_FUNC_UTIL_IS_PRIME,
        (void (*)(void))ntt_is_prime,
    },
    {
        NTT_FUNC_NONE,
        NULL,
    },
};

_Static_assert(NTT_FUNC_UTIL_IS_PRIME + 1 ==
                   (int)(sizeof(ntt__core_ops) / sizeof(ntt__core_ops[0])),
               "adapter core dispatch must contain exactly every id once");

static const ntt_core_api ntt__core_api_instance = {
    .version = NTT_CORE_API_VERSION,
    .struct_size = sizeof(ntt_core_api),
    .ops = ntt__core_ops,
};

/**
 * @brief Returns the core API injected into every adapter.
 *
 * A single, fully populated ntt_core_api (a static dispatch table owned by
 * the library) is handed to built-in adapters and, via the module loader, to
 * dynamically loaded adapter modules.
 *
 * @return The injected core API (never NULL).
 */
const ntt_core_api *ntt__core_api(void)
{
    return &ntt__core_api_instance;
}
