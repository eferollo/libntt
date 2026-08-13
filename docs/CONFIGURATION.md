# Configuration Reference

This document provides a reference for the configuration surface of **libntt**. It specifies every mechanism through which the library's behavior can be adjusted, and the order in which those mechanisms take precedence when they conflict. The configuration is organized along three chapters, treated in turn below: build-time CMake options, runtime environment variables, and the on-disk configuration file. 

## Build-time configuration (CMake options)

| Option | Default | Description |
|---|---|---|
| `NTT_BUILD_TESTS` | `ON` | Build the test suite. Requires `cmocka` and OpenSSL >= 3.0. |
| `NTT_BUILD_EXAMPLES` | `ON` | Build the example programs and the `naive` adapter module. Skipped entirely on Windows. |
| `NTT_ENABLE_LOGGING` | `ON` | Compile in the `NTT_LOG()` calls. Set to `OFF` for zero-overhead benchmark builds: every `NTT_LOG()` is removed at compile time. |

For instructions on building, testing, installing, cleaning, and uninstalling the library, see `INSTALL.md`.

## Runtime environment variables

| Variable | Values | Meaning |
|---|---|---|
| `NTT_CONFIG_FILE` | path | Full path of the configuration file, overriding the default location. |
| `NTT_ADAPTER_MODULE_DIR` | directory | Directory holding external adapter modules; overrides the `module_dir` config-file key. |
| `NTT_LOG_LEVEL` | `none` \| `error` \| `info` \| `debug` | Logging threshold. Case-sensitive. Re-evaluated on every log write unless a level was set explicitly through `ntt_log_set_level()`. Unset or unrecognized values keep the default `error` threshold. |
| `HOME` | path | Base directory for the default config path `$HOME/.config/libntt/ntt.conf` on POSIX. |
| `USERPROFILE` | path | Windows primary home variable; used when `HOME` is unset. |
| `HOMEDRIVE` + `HOMEPATH` | path | Windows legacy fallback pair for the profile directory. |

Test-only variables (not part of the public API):

| Variable | Meaning |
|---|---|
| `NTT_BARRETT_STRESS` | When set, the Barrett unit test reseeds its PRNG from OpenSSL and scales the differential-iteration count. |
| `NTT_MONTGOMERY_STRESS` | Same, for the Montgomery unit test. |

## Configuration file

The library reads a minimal `key = value` file that selects the default adapter and the module directory.

### Location

The path is resolved in `ntt__config_default_path()`:

1. `NTT_CONFIG_FILE` if set and non-empty;
2. otherwise `$HOME/.config/libntt/ntt.conf` (POSIX);
3. on Windows, where `HOME` is not set reliably, `%USERPROFILE%` first and `HOMEDRIVE + HOMEPATH` as a legacy fallback, i.e., `%USERPROFILE%\.config\libntt\ntt.conf`;
4. if no home can be resolved, the default adapter simply falls back to the built-in `scalar` adapter.

### Format

- Lines are limited to 512 characters.
- `#` comments and blank lines are ignored.
- Keys are matched after trimming surrounding whitespace. Recognized keys:

| Key | Purpose |
|---|---|
| `adapter` | Default adapter name (built-in or module). |
| `module_dir` | Directory searched for an external module of that name. |

Example:

```
# libntt configuration
adapter = naive
module_dir = /usr/lib/ntt
```

Unrecognized keys are ignored. A value that does not fit its destination buffer fails the load.

## Default adapter resolution

The default adapter used when a context is created without an explicit adapter is resolved in this order, first match wins:

1. An explicit, process-local override set with `ntt_adapter_set_default(name, module_dir)`;
2. the `adapter` key of the configuration file;
3. an external module named `adapter` loaded from `module_dir`, where `module_dir` comes from the config file and is overridden by the `NTT_ADAPTER_MODULE_DIR` environment variable;
4. the built-in `scalar` adapter (never fails).

`ntt_adapter_get_default()` caches the resolved adapter until `ntt_adapter_set_default()` is called again or `ntt_adapter_unload_all()` clears the cache. `ntt_adapter_load(name, module_dir)` selects an adapter explicitly, bypassing the default resolution entirely: built-in adapters are looked up in the registry, and only if the name is unknown and `module_dir` is given is the module loader consulted.

## Module naming across platforms

The module loader builds the shared-library path as `<module_dir><sep><prefix>ntt_adapter_<name><ext>`:

| Platform | Example path |
|---|---|
| Linux / Unix | `libntt_adapter_naive.so` |
| macOS | `libntt_adapter_naive.so` |
| Windows | `ntt_adapter_naive.dll` (no `lib` prefix, backslash separator) |

See `docs/ARCHITECTURE.md` for how the module loader, injected core API, and adapters interact.

## See also

- `INSTALL.md`: how to build, test, install, clean, and uninstall the library.
- `docs/ARCHITECTURE.md`: detailed description of the library components and how they interact.
- `README.md`: overview, quick start, and general summary of the library.
