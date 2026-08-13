/*
 * test_common.c
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

#define _GNU_SOURCE

#include "test_common.h"
#include <stdlib.h>

void test_set_env(const char *name, const char *value)
{
#ifdef _WIN32
    if (value == NULL) {
        /*
         * Removing a variable this way usually works, but some CRTs reject a
         * NULL value (C11 Annex K). An empty value is an equivalent "unset"
         * for the code under test.
         */
        if (_putenv_s(name, NULL) != 0) {
            _putenv_s(name, "");
        }
        return;
    }
    _putenv_s(name, value);
#else
    if (value == NULL) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

void test_unset_env(const char *name)
{
    test_set_env(name, NULL);
}
