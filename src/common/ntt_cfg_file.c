/*
 * ntt_cfg_file.c
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
#include <ntt/ntt_log.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTT_CFG_KEY_ADAPTER    "adapter"
#define NTT_CFG_KEY_MODULE_DIR "module_dir"
#define NTT_CFG_LINE_MAX       512

/**
 * @brief Resolves the global NTT configuration file path.
 *
 * Returns the NTT_CONFIG_FILE environment variable when it is set and
 * non-empty. Otherwise returns $HOME/.config/libntt/ntt.conf. On Windows,
 * where HOME is not set reliably, USERPROFILE (with HOMEDRIVE + HOMEPATH as a
 * legacy fallback) is consulted first, e.g., %USERPROFILE%\.config\libntt.
 *
 * @param[out] buf Buffer receiving the NUL-terminated path.
 * @param[in]  capacity Size of @p buf in bytes.
 *
 * @return true on success, with the path stored in @p buf.
 * @return false if the path cannot be resolved (no NTT_CONFIG_FILE or HOME)
 *         or does not fit in @p buf.
 */
bool ntt__config_default_path(char *buf, size_t capacity)
{
    const char *file = NULL, *home = NULL;
    size_t len;

    file = getenv("NTT_CONFIG_FILE");
    if (file != NULL && file[0] != '\0') {
        len = strlen(file);
        if (len == 0 || len >= capacity) {
            NTT_LOG(NTT_LOG_ERROR,
                    "NTT_CONFIG_FILE path is empty or too long (len=%zu)",
                    len);
            return false;
        }
        memcpy(buf, file, len + 1);
        NTT_LOG(NTT_LOG_DEBUG, "config file from NTT_CONFIG_FILE: %s", buf);
        return true;
    }

    home = getenv("HOME");
#ifdef _WIN32
    if (home == NULL || home[0] == '\0') {
        /*
         * Windows does not set HOME consistently: USERPROFILE is the norm,
         * HOMEDRIVE + HOMEPATH the fallback for legacy profiles.
         */
        home = getenv("USERPROFILE");
        if (home == NULL || home[0] == '\0') {
            const char *drive = getenv("HOMEDRIVE");
            const char *dir = getenv("HOMEPATH");
            if (drive != NULL && dir != NULL && drive[0] != '\0' &&
                dir[0] != '\0') {
                len = (size_t)snprintf(buf,
                                       capacity,
                                       "%s%s/.config/libntt/ntt.conf",
                                       drive,
                                       dir);
                if (len >= capacity) {
                    NTT_LOG(NTT_LOG_ERROR,
                            "default config path does not fit in buffer "
                            "(len=%zu)",
                            len);
                    return false;
                }
                NTT_LOG(NTT_LOG_INFO, "using default config file: %s", buf);
                return true;
            }
            home = NULL;
        }
    }
#endif
    if (home == NULL || home[0] == '\0') {
        NTT_LOG(NTT_LOG_ERROR,
                "no home directory configured. Cannot resolve default config "
                "path");
        return false;
    }

    len = (size_t)snprintf(buf, capacity, "%s/.config/libntt/ntt.conf", home);
    if (len >= capacity) {
        NTT_LOG(NTT_LOG_ERROR,
                "default config path does not fit in buffer (len=%zu)",
                len);
        return false;
    }
    NTT_LOG(NTT_LOG_INFO, "using default config file: %s", buf);
    return true;
}

/**
 * @brief Parses a single non-comment, non-blank configuration line.
 *
 * @p line must already point past any leading whitespace (the caller skips
 * it). The trailing newline and any trailing whitespace are trimmed, then the
 * line is split on the first '=' and whitespace around both the key and the
 * value is removed. A recognized key is written NUL-terminated into the
 * caller's buffer. Unrecognized keys and lines without '=' are ignored.
 *
 * @param[in,out] line        Buffer holding the line, NUL-terminated.
 * @param[in]     line_no     1-based line number, for logging.
 * @param[out]    adapter     Buffer for the "adapter" value.
 * @param[in]     adapter_cap Size of @p adapter in bytes.
 * @param[out]    module_dir  Buffer for the "module_dir" value.
 * @param[in]     dir_cap     Size of @p module_dir in bytes.
 *
 * @return 0 if the line was handled or ignored.
 * @return -1 if a recognized value does not fit into its destination buffer.
 */
static int ntt__config_parse_line(char *line,
                                  unsigned int line_no,
                                  char *adapter,
                                  size_t adapter_cap,
                                  char *module_dir,
                                  size_t dir_cap)
{
    char *key = NULL, *eq = NULL, *value = NULL;
    size_t len;

    /* drop trailing newline and spaces */
    len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }

    eq = strchr(line, '=');
    if (eq == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "config line %u: no '=', ignoring", line_no);
        return 0;
    }

    /* trim key */
    *eq = '\0';
    key = line;
    len = strlen(key);
    while (len > 0 && isspace((unsigned char)key[len - 1])) {
        key[--len] = '\0';
    }

    /* trim value */
    value = eq + 1;
    while (isspace((unsigned char)*value)) {
        value++;
    }
    len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1])) {
        value[--len] = '\0';
    }

    if (strcmp(key, NTT_CFG_KEY_ADAPTER) == 0) {
        if (strlen(value) >= adapter_cap) {
            NTT_LOG(NTT_LOG_ERROR,
                    "config line %u: adapter value does not fit (need %zu "
                    "bytes)",
                    line_no,
                    strlen(value) + 1);
            return -1;
        }
        if (adapter_cap > 0) {
            (void)snprintf(adapter, adapter_cap, "%s", value);
        }
        NTT_LOG(NTT_LOG_INFO, "config line %u: adapter = %s", line_no, value);
    } else if (strcmp(key, NTT_CFG_KEY_MODULE_DIR) == 0) {
        if (strlen(value) >= dir_cap) {
            NTT_LOG(NTT_LOG_ERROR,
                    "config line %u: module_dir value does not fit (need %zu "
                    "bytes)",
                    line_no,
                    strlen(value) + 1);
            return -1;
        }
        if (dir_cap > 0) {
            (void)snprintf(module_dir, dir_cap, "%s", value);
        }
        NTT_LOG(NTT_LOG_INFO,
                "config line %u: module_dir = %s",
                line_no,
                value);
    } else {
        NTT_LOG(NTT_LOG_DEBUG,
                "config line %u: unknown key '%s' ignored",
                line_no,
                key);
    }
    return 0;
}

/**
 * @brief Parses a minimal key = value configuration file.
 *
 * Only the "adapter" and "module_dir" keys are recognized. Every other line
 * (comments starting with '#' and blank lines included) is ignored. Leading
 * and trailing whitespace around keys and values is trimmed. The parsed
 * values are written NUL-terminated into the caller's buffers and are left
 * empty when the key is absent.
 *
 * @param[in]  path        File to read.
 * @param[out] adapter     Buffer for the "adapter" value.
 * @param[in]  adapter_cap Size of @p adapter in bytes.
 * @param[out] module_dir  Buffer for the "module_dir" value.
 * @param[in]  dir_cap     Size of @p module_dir in bytes.
 *
 * @return true if the file was opened and read, even when it held no known
 *         keys.
 * @return false if the file could not be opened, could not be read, or held a
 *         value that does not fit in its destination buffer.
 */
bool ntt__config_file_load(const char *path,
                           char *adapter,
                           size_t adapter_cap,
                           char *module_dir,
                           size_t dir_cap)
{
    char line[NTT_CFG_LINE_MAX] = {0};
    unsigned int line_no = 0;
    FILE *fp = NULL;
    char *cp = NULL;

    if (path == NULL || (adapter_cap > 0 && adapter == NULL) ||
        (dir_cap > 0 && module_dir == NULL)) {
        NTT_LOG(NTT_LOG_ERROR, "bad arguments to ntt__config_file_load");
        return false;
    }

    if (adapter_cap > 0) {
        adapter[0] = '\0';
    }
    if (dir_cap > 0) {
        module_dir[0] = '\0';
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "error opening config file %s: %s",
                path,
                strerror(errno));
        return false;
    }

    NTT_LOG(NTT_LOG_INFO, "reading config file from %s", path);

    while (fgets(line, sizeof(line), fp) != NULL) {
        line_no++;

        for (cp = line; *cp != '\0'; cp++) {
            if (!isspace((unsigned char)*cp)) {
                break;
            }
        }

        switch (*cp) {
        case '#':
        case '\0':
            continue;
        }

        if (ntt__config_parse_line(cp,
                                   line_no,
                                   adapter,
                                   adapter_cap,
                                   module_dir,
                                   dir_cap) < 0) {
            fclose(fp);
            return false;
        }
    }

    if (ferror(fp)) {
        NTT_LOG(NTT_LOG_ERROR,
                "error reading config file %s at line %u",
                path,
                line_no);
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}
