# libntt - WIP not ready

A generic and extensible Number Theoretic Transform (NTT) library written in C.

The library is designed to separate the **mathematical NTT interface** from the **implementation of the underlying arithmetic and transform algorithms**. This allows different implementations, such as reference scalar code, optimized scalar arithmetic, SIMD/vectorized implementations, or hardware-specific implementations, to coexist behind a common interface.

Backends are called *Adapters*. They are delivered either compiled into the library ("built-in") or as standalone shared libraries ("modules") that the library loads at run time, so third-party or hardware-specific backends can be plugged in without recompiling or relinking the library.

### Build:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```


## Architecture

The library is organized around three main concepts:

```text
                    User Application
                          │
                          ▼
                    Public NTT API
                          │
                          ▼
                       NTT Context
                          │
                          ▼
                       NTT Adapter
                          │
             ┌────────────┼────────────┐
             │            │            │
             ▼            ▼            ▼
         Scalar Toy    Scalar       External Modules
             │            │            │
             ▼            ▼            ▼
        Arithmetic    Arithmetic    External adapter
        Implementation Implementation in a .so / .dll
```

Two mechanisms tie this architecture together:

* **Injected Core API.** The library hands every Adapter a small, versioned
  dispatch table (`ntt_core_api`) of function pointers covering the opaque
  `ntt_config` accessors and shared utilities (e.g., primality testing).
  Adapters, built-in *and* externally loaded, read the configuration
  exclusively through this bundle, never by calling the library directly.
* **Runtime module loading.** An external Adapter is a shared library exposing
  `ntt_adapter_module_init`. It is loaded with `ntt_adapter_load()` and
  validated against the library's Adapter ABI and Core API version before use.

### NTT Context

The `ntt_ctx` represents an initialized NTT operational context.

The context is created from:

* an NTT Adapter
* an NTT configuration.

The common library owns the context and is responsible for dispatching operations to the selected Adapter.
The common layer does not need to know how the Adapter performs modular arithmetic or how it internally represents its state.
Backend-specific state is therefore kept opaque to the common library.

Conceptually:

```c
ntt_ctx
    ├── NTT configuration
    ├── selected Adapter
    └── opaque Adapter state
```

This allows each Adapter to maintain its own internal representation and precomputed data.

For example, an Adapter may internally use:

* canonical modular integers;
* Barrett reduction constants;
* Montgomery representation;
* Shoup precomputed constants;
* signed or centered coefficients;
* SIMD vector registers;
* hardware-specific data structures.

The common NTT context does not depend on any of these implementation details.

---

## NTT Configuration

The `ntt_config` object describes the **mathematical parameters** of an NTT.

Typical parameters include:

```text
q      Prime modulus
n      Transform size
omega  Primitive n-th root of unity
psi    Primitive 2n-th root of unity
type   Convolution type (negacyclic or cyclic)
flags  Requested configuration options
```

The convolution `type` selects the mathematics modulo `x^n + 1` (negacyclic,
the default, using `psi`) or modulo `x^n - 1` (cyclic, using `omega`). Roots
of unity that are not supplied explicitly are derived automatically when the
modulus supports the requested size.

The configuration describes **what NTT computation is required**. It does not describe how the computation is implemented.
The Adapter is responsible for interpreting the configuration and preparing any implementation-specific state required to execute it.
This separation allows the same mathematical configuration to potentially be executed by different implementations.

For example:

```text
                 Same NTT Configuration
                         │
              ┌──────────┼──────────┐
              │          │          │
              ▼          ▼          ▼
          Scalar Toy   Scalar     AVX2
              │          │          │
              ▼          ▼          ▼
          Reference    Barrett    Vectorized
          Arithmetic   Arithmetic Arithmetic
```

---

## NTT Adapters

An NTT Adapter provides a concrete implementation of the NTT operations. The Adapter abstraction is the main extensibility mechanism of the library.

An Adapter is responsible for:

* validating supported moduli;
* initializing implementation-specific state;
* releasing implementation-specific state;
* performing the forward NTT;
* performing the inverse NTT;
* performing negacyclic polynomial multiplication.

The common library only interacts with the Adapter through its defined interface.

The internal state returned by the Adapter setup function is opaque:

```c
void *state;
```

The common library never inspects this state.

This means an Adapter can define any internal representation it requires.

For example:

```text
Scalar Toy Adapter
    └── simple scalar arithmetic

Scalar Adapter
    └── optimized modular reduction (Barrett or Montgomery, selected through configuration flags)

Montgomery-dedicated Adapter
    └── Montgomery-domain representation
    └── Montgomery constants

Future Shoup Adapter
    └── precomputed Shoup constants

Future AVX2 Adapter
    └── vectorized arithmetic
    └── SIMD-specific tables

Future Hardware Adapter
    └── hardware-specific state
```

The Adapter interface therefore provides a stable abstraction while allowing implementations to be completely different internally.

---

## NTT Core API

An Adapter describes the NTT operations, but how does it read the NTT configuration? It does so through the **injected Core API** (`ntt_core_api`): a tiny, versioned dispatch table that the library passes to every Adapter.

The Core API is append-only. It lists a set of well-known function identifiers — currently the `ntt_config` accessors (`get_modulus`, `get_size`, `get_transform_type`, `get_flags`, `get_omega`, `get_psi`) and a shared utility (`is_prime`) — together with the version of the Core API the library provides.

```c
typedef struct {
    uint32_t version;             /* NTT_CORE_API_VERSION */
    uint32_t struct_size;         /* bytes in this table    */
    const ntt_dispatch *ops;      /* NULL-terminated stream  */
} ntt_core_api;
```

Typed inline accessors (`ntt_core_get_modulus()`, `ntt_core_get_size()`, `ntt_core_is_prime()`, ...) hide the dispatching. Adapters never hand-cast the streamed function pointers themselves.

Why inject the table instead of let Adapters call the library?

* A dynamically loaded module links against **nothing but libc** — it cannot call `ntt_config_get_modulus()` directly.
* Each Adapter states the minimum Core API version it requires (`ntt_core_is_compatible()`). Because the API only grows, a newer library core is a strict superset and satisfies older modules; a library core that is older than a module's requirement causes the module to be **rejected at load time**, never a silent wrong value.
* The library guarantees its own dispatch table is complete at compile time, so built-in accessors cannot miss.

NOTE: This mirrors how OpenSSL 3.0 Providers pass their core dispatch table to loadable modules.

---

## Adapter Modules

An Adapter is resolved by name from the **built-in registry** (currently `scalar_toy` and `scalar`) or loaded from a **module**: a shared library named `libntt_adapter_<name>.so` (`.dll` on Windows) that exports a single entry point:

```c
NTT_MODULE_EXPORT int ntt_adapter_module_init(const ntt_core_api *core,
                                              const ntt_adapter **out);
```

The entry point first checks `ntt_core_is_compatible(core, NTT_CORE_API_VERSION)`, then fills an `ntt_adapter` descriptor and returns it. The descriptor carries an ABI version and a `struct_size`, which the library checks so an older/newer pair fails loudly instead of misbehaving.

Public loading controls:

```c
const ntt_adapter *ntt_adapter_load(const char *name, const char *module_dir);
const ntt_adapter *ntt_adapter_get(ntt_adapter_selector selector);
const ntt_adapter *ntt_adapter_get_default(void);
int  ntt_adapter_set_default(const char *name, const char *module_dir);
void ntt_adapter_unload_all(void);
```

`ntt_adapter_load(name, NULL)` resolves a built-in from the registry; `ntt_adapter_load(name, dir)` loads `<dir>/libntt_adapter_<name>.so`. The scalar adapters are also available explicitly via `ntt_adapter_get(NTT_ADAPTER_SCALAR)` / `ntt_adapter_get(NTT_ADAPTER_SCALAR_TOY)`.

The **default adapter** is resolved, from highest to lowest precedence, as:

1. an explicit override installed with `ntt_adapter_set_default()`;
2. the `adapter` / `module_dir` keys of the global configuration file (path from `NTT_CONFIG_FILE`, defaulting to `$HOME/.config/libntt/ntt.conf`);
3. the `NTT_ADAPTER_MODULE_DIR` environment variable, overriding `module_dir`;
4. the built-in `scalar` Adapter as a final fallback (so the default never fails).

`ntt_adapter_unload_all()` releases every dynamically loaded module and clears the resolution cache and the explicit override.

The `examples/adapters/naive` module demonstrates a fully external Adapter: it includes only the public headers, links nothing, reads the configuration through the injected Core API, and is exercised by `examples/ntt_example_oneshot` and `examples/ntt_example_stepwise`, which let the user pick any adapter (built-in or module) and default to the resolved library default.

---

## Modular Arithmetic

Modular arithmetic is intentionally **not exposed as the primary public polymorphic interface**. This is important because different arithmetic techniques have different requirements.

For example:

#### Standard modular arithmetic

```text
a, b ∈ [0, q)
```

#### Barrett reduction

```text
a, b ∈ [0, q)
```

with precomputed reduction constants.

#### Montgomery arithmetic

```text
aR mod q
```

where values are represented in the Montgomery domain.

#### Shoup multiplication

A twiddle factor may require additional precomputed data:

```text
(w, w_shoup)
```

#### Vectorized implementations

The underlying data may be represented as:

```text
__m256i
```

or another architecture-specific vector type.

These implementations cannot all be naturally expressed through a single interface such as:

```c
uint64_t mulmod(uint64_t a, uint64_t b, uint64_t q);
```

Therefore, arithmetic implementations are considered **internal implementation details of an Adapter**.

The public API and the common layer work with 64-bit coefficients, allowing moduli well beyond 32 bits (`q <= 2^63 - 1` for the general scalar path). Consequently the built-in scalar Adapter implements each reduction strategy on two paths:

* **Barrett reduction** — a single-word path (64-bit product) with a precomputed reciprocal for `q < 2^32`, and a two-word path (128-bit product, emulated without a compiler-provided 128-bit integer type) for general moduli up to `2^63 - 1`.
* **Montgomery reduction** — radix `R = 2^32` when `q < 2^32`, and `R = 2^64` for the general path, with the usual `q_inv`, `R^2 mod q` precomputation.

The reduction backend is selected per context through the configuration flags (`NTT_CONFIG_REDUCTION_BARRETT` or `NTT_CONFIG_REDUCTION_MONTGOMERY`).

Common arithmetic algorithms can still be provided as reusable internal helpers, but an Adapter is free to use them, replace them, or implement its own arithmetic.

This makes it possible to experiment with new reduction strategies without changing the common NTT API.

---

## Adapter Capabilities and Flags

Adapters can advertise capabilities and supported configuration flags. Applications can query an Adapter before creating an NTT context.

For example:

```c
ntt_adapter_get_capabilities(adapter);
ntt_adapter_get_supported_flags(adapter);
ntt_adapter_supports_flags(adapter, flags);
```

This allows an application to determine whether a requested configuration is supported before initialization.

Conceptually:

```text
Application
    │
    ├── Select Adapter
    │
    ├── Build NTT configuration
    │
    ├── Check Adapter capabilities
    │
    ├── Check requested flags
    │
    ▼
Create NTT Context
    │
    ▼
Adapter setup()
    │
    ▼
Opaque Adapter state
    │
    ▼
NTT operations
```

This model also allows applications to select different Adapters at runtime: built-in Adapters are resolved by name through the internal registry (`ntt_adapter_get()`, `ntt_adapter_load(name, NULL)`), while external Adapters are loaded as modules from a directory (`ntt_adapter_load(name, dir)`). The same hardware/architecture-driven selection can be expressed as an Adapter module without recompiling the library.

For example, a benchmark application could execute the same NTT operation using different adapters without changing the mathematical input or the public NTT API.

---

## Public API vs Internal Implementation

The project separates public API from internal implementation.

Public headers expose:

* NTT context management;
* NTT configuration;
* NTT operations;
* Adapter definitions and Adapter ABI;
* Adapter discovery, capability, module-loading, and default-resolution APIs;
* the injected Core API (`ntt_core_api`), i.e. how an Adapter reads the configuration.

Internal source code contains:

* Adapter implementations;
* modular arithmetic;
* reduction algorithms;
* precomputation;
* lookup tables;
* implementation-specific state;
* the built-in Adapter registry;
* the runtime module loader (`dlopen`/`LoadLibrary` plus the version handshake);
* the global configuration file handling.

Users of the library only need the public headers.

Adapter developers can implement their own Adapter using the Adapter interface without modifying the common NTT implementation.

---

## Design Principles

The architecture follows these principles:

1. **The public API describes the NTT operation, not the implementation.**

2. **The NTT configuration describes mathematical parameters, not arithmetic representation.**

3. **Adapters own their implementation-specific state.**

4. **The common library treats Adapter state as opaque.**

5. **Modular arithmetic is an implementation detail of an Adapter.**

6. **Different reduction techniques are free to use different internal representations.**

7. **Scalar and vectorized implementations can coexist behind the same Adapter abstraction.**

8. **Applications can select and query Adapters at runtime, and external Adapters can be loaded as modules without relinking the library.**

9. **Adapters read the configuration through an injected, versioned Core API, so a module links against nothing but libc.**

10. **New Adapters can be developed without modifying the common NTT logic.**

11. **Public API and common layer support 64-bit moduli; reduction backends provide both fast 32-bit paths and a general 64-bit path.**

12. **The architecture is intended to support future optimized and hardware-specific implementations.**

The central design goal was forced from the very beginning by one idea: give *anyone* the ability to easily optimize whatever NTT operation their project relies on (e.g., PQC, FHE, or anything else), and hand them a high degree of freedom to modify and optimize an implementation through Adapters according to their own constraints. The architecture exists to make that possible:

> **Define a stable interface for the mathematical NTT operation while allowing complete freedom in how an Adapter implements arithmetic and transforms internally.**

No matter what an Adapter does internally — a different reduction strategy, a different coefficient representation, targeted assembly, or hardware-specific state — the public API and the common layer stay untouched, so a user only ever writes the part that matters to them.
