# Internal Architecture of Libntt

## Introduction

This document provides a formal description of the **NTT library**. Its purpose is to explain not only *what* each library's component is, but also *why* it exists and *how* it cooperates with the others to deliver a portable and extensible Number Theoretic Transform.

The library is best understood as a carefully layered system. At the front is a small, stable public interface; in the middle, a body of shared logic independent of any particular arithmetic representation; and at the back, a set of interchangeable implementations that provide the actual transform. The value of this separation is architectural rather than functional: users interact with a single, consistent interface, while implementers can replace or extend the back-end layer without modifying the code that surrounds it. The present document formalizes this arrangement and traces the data and control flow that binds the layers together.

This guide complements [CONFIGURATION.md](docs/CONFIGURATION.md). That document describes the configuration surface of the library — the build-time options, environment variables, and configuration files that tune its behavior. This document instead describes the internal architecture behind that surface: the components themselves and how they interact. The two documents are best read together.

## 1. Layering

The library is organized as a strict hierarchy of three layers. The dependency direction is always downward: a component in one layer may call into the layer below it, but never into the layer above it, and no layer may bypass an intervening layer.

| Layer | Location | Responsibility |
|---|---|---|
| Public API | `include/ntt/` | The user-facing contract: types, functions, and constants. |
| Common core | `src/common/` | Adapter-independent logic: lifecycle, dispatch, loading, logging. |
| Backends | `src/adapters/` | Concrete implementations of the transform and its arithmetic. |

**Public API.** The public-interface components declare the user-facing contract. They live in `include/ntt/` and are the only include files a library user sees. They constitute the ABI contract, and everything below is invisible to the caller. Each public namespace groups a coherent set of types and functions, such as, the context and its lifecycle, the transform parameters, the adapter descriptor and its selection, the injected core API, the logging interface, and the number-theoretic utilities. Their interface shape is fixed by the ABI, so the members of a namespace may grow, but the namespace itself is stable.

**Common core.** The common-core components implement the adapter-independent machinery in `src/common/`, classified by the concern they serve: context lifecycle and state; NTT configuration, meaning the parameter object and its on-disk source; adapter selection and module loading; transform dispatch into the backend; the injected core API and the platform dynamic-loader shim; and leveled logging. Any future concern slots into one of these categories.

**Backends.** The backends implement the transform proper. The library reaches them only through the adapter dispatch table, and they reach the library only through the injected core API, which they use solely to read the configuration set by the user. A backend is either compiled into the library (a *built-in*) or shipped separately and loaded at run time (a *module*). Built-ins today come in two flavors — an optimized sclar implementation and a plain-reference implementation used as ground truth (**both still in progress**) — but the set of backends and their flavors is open-ended, exactly as the adapter contract intends. The contract itself is described in Section 2.3.

An *external module* is a backend that is shipped as a standalone shared library and loaded at run time. It obeys exactly the same adapter contract as a built-in backend, and is therefore indistinguishable from one once it has been loaded. Modules are discussed in detail in Section 2.5 and Section 3.4.

## 2. Components in detail

This section treats each component in turn. For each, it describes the component's responsibility, the shape of the interface it exposes, and the properties that make it correct in the context of the whole library. The interaction protocols that connect these components are discussed separately, in Section 3.

### 2.1 The `ntt` namespace: context and lifecycle

The context is the central handle of the library: every transform operation runs through a context, and no mathematical state is ever held anywhere else in the common layer. Its layout, defined in `src/common/headers/ntt_internal.h`, is deliberately minimal:

```c
struct ntt_ctx_s {
    uint64_t q;                  /* prime modulus              */
    uint32_t n;                  /* transform size, power of 2 */
    const ntt_adapter *adapter;  /* selected backend vtable    */
    void *state;                 /* opaque backend state       */
};
```

The design intention behind this minimalism is worth making explicit. The fields `q` and `n` record the mathematical parameters of the transform, and serve primarily as bookkeeping: they describe what the context *means*, not how the backend *computes*. Everything that is implementation-specific — twiddle tables, reduction constants, coefficient representations, and any precomputed material — resides in the opaque `state` object produced by `adapter->setup()`. The common layer never inspects, mutates, or even type-checks `state`. It merely stores it and passes it back to the *vtable* callbacks. This arrangement ensures that the common code treats adapter state as opaque, while allowing each adapter to represent and manage that state according to its implementation requirements.

Context creation, `ntt_create()`, is where the library performs its most important validation and initialization work. The full pipeline is described in Section 3.1. For the present discussion it suffices to say that `ntt_create()` verifies the adapter's ABI, validates the mathematical parameters and any reduction flags, resolves any unspecified roots of unity, and then hands control to the backend's `setup` callback. Destruction, `ntt_destroy()`, is the mirror image: it invokes `adapter->teardown(state)` to release backend resources and then frees the context itself.

### 2.2 The `ntt_config` namespace: transform parameters

`ntt_config` is an opaque, heap-allocated structure that carries the mathematical parameters of a transform: the prime modulus `q`, the size (order) `n`, the transform type (cyclic or negacyclic), the reduction flags, and the optional roots `omega` and `psi`, where a value of zero indicates that the root should be derived automatically.

Two properties of this component deserve particular attention. First, the setters and getters are trivial field accessors with NULL guards. **No value validation is performed at set time**. All validation is deferred to `ntt_create()`. This is a deliberate choice: the caller assembles the configuration incrementally, field by field, so at the time any individual setter runs the remaining fields may not be known yet. Moreover, individual values only make sense in combination (e.g., `q` must be prime, `n` a power of two, and `omega`/`psi` consistent with the transform type) so checking each setter in isolation would either reject configurations prematurely or scatter validation across many error paths. Deferring every check to `ntt_create()`, when the configuration is complete, concentrates all validation in a single coherent error path.

Second, the config object is the *only* piece of common state that crosses the backend boundary. Because backends are forbidden from calling into the library, they read the configuration exclusively through the injected core API (Section 2.4). The config is therefore always passed to a backend read-only, and the backend never retains a reference to it after `setup` returns. In practice this means `ntt_create()` operates on a private copy, so that no call ever mutates the caller's config.

### 2.3 The `ntt_adapter` namespace: backend descriptor and selection

A backend is described by a static, immutable descriptor of type `ntt_adapter`, defined in `ntt_adapter.h`:

```c
struct ntt_adapter_s {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *name;
    uint32_t capabilities;      /* NTT_CAP_*     */
    uint32_t supported_flags;   /* NTT_CONFIG_*  */
    ntt_validate_modulus_fn validate_modulus;
    ntt_adapter_setup_fn setup;
    ntt_adapter_teardown_fn teardown;
    ntt_adapter_forward_fn forward;
    ntt_adapter_inverse_fn inverse;
    ntt_adapter_negacyclic_mul_fn negacyclic_mul;
};
```

The descriptor plays two roles simultaneously: 
* the *specification* of what the backend can do;
* the *vtable* of what the backend can be asked to do. 

The specification is carried by the first five fields. The versioning mechanism deserves particular note. `abi_version` and `struct_size` together allow `ntt__adapter_is_compatible()` to reject a descriptor that is newer than the library, or too small to contain the required trailing fields. The consequence is that a mix of a newer and an older binary fails loudly at load time rather than misbehaving silently at run time. The `capabilities` bitmask advertises the backend's abilities — whether it accepts a run-time modulus, whether it is vectorized, which reduction algorithms it implements, and so on — and `supported_flags` narrows this to the subset of `NTT_CONFIG_*` flags the backend honors. The selection layer, described below, uses both fields, together with `ntt_adapter_supports_flags()`, to reject configurations the backend cannot honor.

Adapter selection proceeds along three paths. The function `ntt_adapter_get(selector)` chooses a built-in adapter by enum (`NTT_ADAPTER_SCALAR`, `NTT_ADAPTER_SCALAR_TOY`, or `NTT_ADAPTER_DEFAULT`). The function `ntt_adapter_load(name, module_dir)` looks up a built-in adapter by name and, if the name is unknown and a module directory is supplied, falls back to the module loader. Finally, the pair `ntt_adapter_set_default()` / `ntt_adapter_get_default()` implements process-local override and the full default-resolution chain, which is described in Section 3.3.

### 2.4 The `ntt_core` namespace: injected API and platform shim

The `ntt_core` namespace carries two responsibilities that are conceptually distinct but share a single motivation: making the backend boundary safe and portable.

The **first** responsibility is the *injected core API*. For an external module to function without linking against libntt, the library must deliver its services to the module by other means. It does so by handing every adapter a `ntt_core_api` — a small, NULL-terminated dispatch table of function pointers covering the configuration accessors (e.g., getters) and the shared utilities (e.g., from `ntt_utils.h`). The table is versioned (`NTT_CORE_API_VERSION`) and append-only: a newer core is always a strict superset of an older one, so a module compiled against an older version continues to function against a newer library. Adapters reach the typed accessors through `ntt_core_find()` followed by a cast to the specific accessor type. This is the *only* route by which a backend may reach the common layer.

The loader, defends this handshake from both sides. On the library's side, `ntt__module_core_is_valid()` performs a defensive check of the injected core — its version, its `struct_size`, and the presence of a non-NULL stream — before the core is handed to a module. On the module's side, the module's own `ntt_core_is_compatible(core, min_version)` is the authoritative version gate: the module declares the version it was compiled against, and the pair of checks ensures that neither side can be surprised by the other.

The **second** responsibility is the *platform dynamic-loader shim*. Because the library must run on both POSIX and Windows systems, it wraps the operating system's loader so that no other component needs to be aware of the platform. The shim consists of three functions and three string constants:

| Function | POSIX | Windows |
|---|---|---|
| `ntt__dlopen` | `dlopen(RTLD_NOW)` | `LoadLibraryA` |
| `ntt__dlsym` | `dlsym` | `GetProcAddress` |
| `ntt__dlclose` | `dlclose` | `FreeLibrary` |
| `ntt__dl_extension` | `.so` | `.dll` |
| `ntt__dl_prefix` | `lib` | (empty) |
| `ntt__dl_separator` | `/` | `\` |

The export macro `NTT_MODULE_EXPORT` (`ntt_core.h`) is likewise `__declspec(dllexport)` on Windows and `visibility("default")` on GCC and Clang. The net effect is that the rest of the library, and in particular the module loader of Section 2.5, is written in a platform-neutral way and yet produces correct behavior on every supported system.

### 2.5 The `ntt_module` namespace: built-ins and run-time loading

The `ntt_module` component is the name-resolution layer of the library through two complementary mechanisms. The built-in registry maps a name to the adapter already compiled into the library; the loader covers the rest, turning an on-disk shared library into a live backend.

The built-in registry is a static name-to-getter table holding the adapters compiled into the library at build time; `ntt__registry_lookup()` is consulted as the fallback by `ntt_adapter_load()`. Loading a module proceeds in `ntt__module_load_from_dir()` through the following steps:

- The module path is assembled from the platform shim's conventions (Section 2.4): `<module_dir><sep><prefix>ntt_adapter_<name><ext>`.
- The shared library is opened with `ntt__dlopen`.
- A single required symbol is resolved with `ntt__dlsym`: `ntt_adapter_module_init`, the module's entry point and its entire exported surface. The loader invokes it as a handshake, passing the injected core API (the module's only channel to the library) and a pointer to an adapter slot. The module first accepts the core through `ntt_core_is_compatible()`, then writes its static adapter descriptor into that slot.
- The loader validates the injected core with `ntt__module_core_is_valid()` and subjects the returned descriptor to the same ABI compatibility check as a built-in.

Handle management is an important detail of correctness. A module must not be unloaded from the process while an adapter obtained from it is still in use, since the adapter's vtable points into the module's code. The loader therefore keeps every successfully loaded handle resident in a **fixed-size cache** (16 slots), and closes them all only in `ntt__module_unload_all()`, which is invoked by `ntt_adapter_unload_all()`. If any step of the load fails, the partially opened handle is closed immediately and the load reports NULL, allowing the caller to fall back to a built-in adapter.

### 2.6 The `ntt_cfg_file` namespace: configuration file

The configuration file is the on-disk complement of the environment variables: a minimal `key = value` file that supplies a default adapter name and a module directory. 

The parser, in `ntt_cfg_file.c`, skips `#` comment lines and blank lines, trims surrounding whitespace from each key, stores the recognized keys `adapter` and `module_dir`, and silently ignores any others. 

The default path itself is resolved by `ntt__config_default_path()`, which honors `NTT_CONFIG_FILE` when set and otherwise falls back to the home directory, including the Windows `USERPROFILE` / `HOMEDRIVE`+`HOMEPATH` variants. The exact resolution rules, the line-length limit, and the precedence of every source of configuration are specified in `docs/CONFIGURATION.md`, Section 3.

### 2.7 The `ntt_api` namespace: transform dispatch

The public transform entry points — `ntt_forward()`, `ntt_inverse()`, and `ntt_negacyclic_mul()`, implemented in `src/common/ntt_api.c` — are the simplest component in the library, and deliberately so. Each is a thin dispatcher with two halves. The first half is argument validation: every pointer (context, adapter, state, and coefficient arrays) and every vtable slot is checked for NULL, and the operation fails with an error code rather than dereferencing an invalid pointer. The second half is the forwarding call itself: the operation is delegated to the corresponding adapter callback with the context's opaque state as its first argument.

The significance of this component is not in what it does but in what it avoids doing. No number theory, no arithmetic, and no transform logic appears in the common layer at transform time; every mathematical operation is performed inside the backend. The dispatch path is consequently a single indirect call, which is the price the architecture pays for its extensibility and is the entire cost of the design.

### 2.8 The `ntt_log` namespace: leveled logging

Logging is provided by the `ntt_log` component. The macro `NTT_LOG(level, ...)` expands to a call into `ntt__log_write()`, and the message is emitted only if the requested level meets the configured threshold, where the levels are ordered `none < error < info < debug`.

The threshold has two sources of truth, which interact in a deliberate way. An explicit `ntt_log_set_level()` always wins and is sticky: once the caller sets a level, it is respected indefinitely. Absent such a call, the environment variable `NTT_LOG_LEVEL` is consulted afresh on every write. This second behavior is a practical convenience: an application's verbosity can be raised or lowered at run time, without reconfiguring or rebuilding the library, by simply exporting the variable before running.

The level flag is stored in an `_Atomic` type so that reads and writes are safe across threads. When the library is built with `-DNTT_ENABLE_LOGGING=OFF`, `NTT_LOG()` is compiled out entirely, so logging adds zero cost to a production build.

### 2.9 The `ntt_utils` namespace: shared number theory

The `ntt_utils` component provides the pure-arithmetic helpers required at *setup* time: never in the transform hot path which depend on the selected adapter. Its functions perform deterministic Miller-Rabin primality testing over the full `uint64_t` domain, factor `q - 1` (using a fast-path table for well-known moduli (to be updated) and trial division plus Pollard's rho otherwise), search for a primitive root, compute modular square roots with the Tonelli-Shanks algorithm, and provide helpers like `ntt_is_power_of_two()` and `ntt_reverse_bits()`.

**NOTE:** heir relative slowness is of no consequence: these functions run at most once per context creation, and that one-time cost is amortized over the entire lifetime of the transform.

### 2.10 The backends (wip)

The library ships two built-in backends. Both are **still work in progress and subject to breaking changes**:

- **`scalar`** — the optimized backend. Accepts a runtime modulus. The transform is radix-2 Cooley-Tukey for the forward pass and radix-2 Gentleman-Sande for the inverse. Modular arithmetic is provided by the two reduction backends, Barrett and Montgomery: operands are kept as canonical residues under Barrett and in the `xR mod q` representation under Montgomery. Barrett reduction is the default, Montgomery is selectable via `NTT_CONFIG_REDUCTION_MONTGOMERY`. `negacyclic_mul` is assembled as twist, forward, pointwise multiply, inverse, untwist. Setup and teardown build and release the reduction constants and any precomputed state.
- **`scalar_toy`** — the reference backend. Exposes the same adapter contract but uses plain `(a * b) % q` multiplication, working for moduli up to `q <= 2^63` (this adapter requires major adjustments).

Users implementing their own external adapters may consult the example module implementation under `examples/adapters/naive/`, which links nothing against libntt and exports `ntt_adapter_module_init(core, &adapter)` as its only symbol.

## 3. How the pieces work together

Section 2 looked at each component on its own. This section looks at how they cooperate: the sequences of calls and the flow of data that bind them together. The library relies on five such protocols, which cover its whole life: creating a context, dispatching a transform, resolving the default adapter, loading a module, and negotiating the adapter boundary.

### 3.1 Context creation

The creation of a context is the single most involved protocol in the library, because it is where every invariant that later operations depend on is established. `ntt_create(adapter, config)` proceeds through six stages:

1. **ABI check** -- `ntt__adapter_is_compatible(adapter)` verifies the adapter's ABI version and struct size (Section 2.3).
2. **Modulus validation** -- `adapter->validate_modulus(config, core)` lets the backend enforce its own intrinsic limits, such as `scalar`'s `q <= 2^63` (with Montgomery additionally requiring an odd modulus).
3. **Flag support** -- `ntt_adapter_supports_flags(adapter, flags)` rejects a requested reduction the backend does not honor.
4. **Parameter validation** --`ntt__validate_transform_params(q, n, type)` applies the common domain rules of the NTT.
5. **Root resolution** -- `ntt__resolve_roots()` works on a private copy of the config: it factors `q - 1`, finds a primitive root, and derives `omega` and `psi` subject to the rules below.
6. **Setup** -- `adapter->setup(&resolved, core)` returns the opaque state, and the context is assembled as `{q, n, adapter, state}`.

Root resolution follows the rules: the cyclic transform requires `n | q - 1`, the negacyclic requires `2n | q - 1`; the two roots satisfy `psi^2 == omega`, and the missing one is derived from the other.

Two properties of this protocol are worth retaining. First, the common layer owns validation and root derivation, while the backend owns arithmetic representation and precomputation; the division of labor is clean. Second, because resolution runs on a copy, `ntt_create()` is guaranteed not to mutate the caller's configuration object.

### 3.2 Transform dispatch

Once a context exists, a transform operation is the simplest protocol in the library. The entire flow is a guarded indirect call:

```c
ntt_forward(ctx, a)
  -> ctx->adapter->forward(ctx->state, a)   # backend executes the NTT

ntt_inverse(ctx, a)
  -> ctx->adapter->inverse(ctx->state, a)

ntt_negacyclic_mul(a, b, c, ctx)
  -> ctx->adapter->negacyclic_mul(ctx->state, a, b, c)

TODO: ntt_cyclic_mul(a, b, c, ctx)
```

The context only stores `q`, `n`, and `omega`/`psi` for reference; everything the transform actually needs lives in the backend's state. As a result, the hot path is a single indirect call into backend code, with nothing else happening in the common layer.

### 3.3 Default adapter resolution

When a user creates a context without naming an adapter, the library must choose one. The choice is governed by a strict precedence chain, evaluated in order, in which the first successful source wins:

```
ntt_adapter_get_default()
  1. ntt_adapter_set_default(name, module_dir)?   -> use it (process-local)
  2. config file `adapter` key?  -> use it
  3. module named <adapter> in module_dir?
       module_dir = config `module_dir`, overridden by NTT_ADAPTER_MODULE_DIR
  4. otherwise built-in `scalar`
```

The chain is deliberately ordered from the most explicit source to the most implicit: a process-local override set by the application itself outranks the configuration file, which in turn outranks a loadable module, and the built-in `scalar` adapter is the unconditional last resort. The resolved adapter is cached until `ntt_adapter_set_default()` is called again or `ntt_adapter_unload_all()` clears the cache. The full specification of the sources (e.g., environment variables, file location, and format) is given in `docs/CONFIGURATION.md`.

### 3.4 Module loading lifecycle

The protocol by which an on-disk shared library becomes a usable adapter is the most safety-critical flow in the library, since it is the one place where untrusted-adjacent code is introduced into the process. It proceeds as follows:

```
ntt_adapter_load(name, module_dir)
  -> ntt__module_load_from_dir(dir, name)
       path = <dir><sep><prefix>ntt_adapter_<name><ext>   # platform shim
       handle = ntt__dlopen(path)
       entry  = ntt__dlsym(handle, "ntt_adapter_module_init")
       ntt__module_core_is_valid()              # defensive core check
       entry(ntt__core_api(), &adapter)         # module-side handshake
       ntt__adapter_is_compatible(adapter)      # ABI check
       cache handle (16 slots)                  # keep module resident
  -> adapter
```

Each stage is a gate: if the path cannot be assembled, the library cannot be opened, the entry symbol is absent, the injected core fails validation, or the returned descriptor fails the ABI check, the handle is closed and the load reports `NULL`. Only when every gate has been passed is the adapter handed to the caller, with the module's handle held resident so that the vtable inside it cannot dangle.

### 3.5 The adapter boundary: why modules never link libntt

The protocols above all rest on a single architectural inversion, which is worth stating precisely. Configuration and utilities flow *downward* — from the common layer into a backend — through injected function pointers, rather than through symbols resolved against the library:

```
backend / module                      common core
----------------                      -----------
  |-- validate_modulus(config, core)      |
  |-- setup(config, core)                 |  reads config ONLY via
  |-- forward(state, a) ................. |  ntt_core_get_*(core, config)
                                          |
   core points at the library's           |
   static ntt_core_api dispatch table ----+
```

The consequence is that a backend never references a single symbol from libntt. It links only against libc. It receives the configuration through the injected `ntt_core_api` table, and it is the library, not the module, that resolves all its own symbols. This is what makes a third-party backend buildable and shippable with no dependency on the library at all, and why the version handshake of Section 2.4 exists: the only contract between the two sides is the versioned, append-only core table.

## 4. Extension points

The architecture is designed so that adding a new backend requires no change to the common layer. The complete procedure is:

1. Implement an `ntt_adapter` descriptor — name, capabilities, supported flags, and the six callbacks — in a new directory `src/adapters/<name>/`.
2. Register it as a built-in by adding a getter to the built-in registry in `ntt_module.c` and its source to `src/CMakeLists.txt`. Alternatively, ship it as a module: a standalone shared library that exports `ntt_adapter_module_init` and nothing else.
3. If the backend imposes modulus constraints beyond the common rules, implement `validate_modulus`. If it supports a selectable reduction, or any other feature, declare the corresponding flags in `supported_flags`.

No modification of the common core is required: the adapter descriptor and the injected core API are the entire interface to which a new backend must conform.

## Conclusion

The library can be summarized in a single sentence: a stable, narrow public interface sits atop a portable, adapter-independent core, behind which interchangeable backends, either compiled in or loaded at run time, freely implement the transform while communicating with the core exclusively through a versioned, injected API.

The components described in this document exist to make this separation of responsibilities both rigorous and lightweight, while the protocols in Section 3 provide the mechanisms that enforce it.

A reader wishing to extend the library need only understand the adapter descriptor and the injected core. Everything else is supporting infrastructure that an extension never needs to touch.
