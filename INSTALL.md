# Installing libntt

This document explains how to build, test, install, and uninstall the library from source. For the complete reference of every build-time option and the feature flags mentioned here, see `docs/CONFIGURATION.md`.

## Prerequisites

- A C11 compiler (GCC, Clang, or MSVC).
- CMake 3.20 or later.
- For the test suite: cmocka and OpenSSL >= 3.0.

## Building the library (on Linux)

To build the library with everything enabled (tests, examples, and logging):

```sh
cmake -S . -B build -DNTT_BUILD_TESTS=ON -DNTT_BUILD_EXAMPLES=ON -DNTT_ENABLE_LOGGING=ON
cmake --build build
```

Choose the build type at configuration time with `-DCMAKE_BUILD_TYPE`, e.g.:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

The recognized values are `Debug`, `Release`, `RelWithDebInfo` (the default), and `MinSizeRel`.

## Selecting features

Feature flags are also passed at configuration time with `-D<OPTION>=<VALUE>`. For example, to build only the library, without tests, examples, or logging:

```
cmake -S . -B build -DNTT_BUILD_TESTS=OFF -DNTT_BUILD_EXAMPLES=OFF -DNTT_ENABLE_LOGGING=OFF
```

To disable logging but keep everything else:

```
cmake -S . -B build -DNTT_ENABLE_LOGGING=OFF
```

Flags are re-read on every `cmake -S . -B build` invocation, so switching a feature is just a matter of re-running configure and then rebuilding:

```
cmake -S . -B build -DNTT_BUILD_TESTS=OFF
cmake --build build
```

## Running the tests

The test suite is enabled by default (`NTT_BUILD_TESTS=ON`) and requires cmocka and OpenSSL >= 3.0. Build it and run it with:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Installing the library

```
cmake --install build
```

This installs the static library, the public headers under `include/ntt/`, and the license file into the standard prefix (`/usr/local` on Unix). To install elsewhere, set the prefix at install time:

```
cmake --install build --prefix /usr/local
```

## Cleaning the build

Remove the compiled objects without touching the configuration:

```
cmake --build build --target clean
```

To start from scratch, delete the build directory entirely:

```
rm -rf build
```

## Uninstalling the library

To remove every file that `cmake --install` placed on disk:

```
cmake --build build --target uninstall
```

The target reads the install manifest written by `cmake --install` and removes the listed files (the library, the public headers, and the license).

## Building the documentation

The library ships three human-readable guides alongside the code:

- `INSTALL.md` — this document: build, test, install, clean, and uninstall.
- `docs/CONFIGURATION.md` — the complete reference of the build-time options, runtime environment variables, the configuration file, and the default-adapter resolution rules.
- `docs/ARCHITECTURE.md` — a detailed description of the library components and how they interact.

An HTML API reference can be generated from the public headers and the guides with Doxygen. The target is on demand — it is not part of the default build and is simply skipped when Doxygen is not installed:

```
cmake -S . -B build
cmake --build build --target docs
```

Then open `docs/html/index.html` with your browser. The generated `docs/html` directory is not tracked by git.
