# Internal Architecture of Libntt

## Introduction

This document provides a formal description of the **NTT library**. Its purpose is to explain not only *what* each library's component is, but also *why* it exists and *how* it cooperates with the others to deliver a portable and extensible Number Theoretic Transform.

The library is best understood as a layered system where at the front is a small, stable public interface, in the middle is a body of shared configuration and dispach logic, and at the back is a set of interchangeable implementations that provide the actual transforms. Thanks to this separation users interact with a single, consistent interface, while implementers can replace or extend the backend layer without modifying the code that surrounds it. The present document formalizes this arrangement and traces the data and control flow that binds the layers together.

## 1. Layering

The library is organized as a strict hierarchy of three layers. The dependency direction is always downward: a component in one layer may depend on the layer below it, never on the layer above it, and no layer may bypass an intervening layer.

[!NOTE]
This is a rule about *dependencies*, not about call direction: a backend does call upward into the core API at run time (its config getters), but only through the injected dispatch table of [Section 2.4](#24-the-ntt_core-namespace-injected-api-and-platform-shim), so no compile-time or link-time dependency ever points upward.

The following table summarizes the role and location of each layer:
| Layer | Location | Responsibility |
|---|---|---|
| Public API | `include/ntt/` | The user-facing contract: types, functions, and constants. |
| Common core | `src/common/` | Adapter-independent logic: lifecycle, dispatch, loading, logging. |
| Backends | `src/adapters/` | Concrete implementations of the transform and its arithmetic. |

The layers are described more in detail below.

- **Public API.** The public-interface components declare the user-facing contract. They live in `include/ntt/` and are the only header files a library user sees. They constitute the ABI contract, and everything below is invisible to the caller. Each public namespace groups a coherent set of types and functions, such as, the context and its lifecycle, the configurable transform parameters, the adapter descriptor and its selection, the injected core API, the logging interface, and the number-theoretic utilities. Their interface shape is fixed by the ABI, so the members of a namespace may grow, but the namespace itself is stable.

- **Common core.** The common-core components implement the adapter-independent logic in `src/common/`, classified by the concern they serve: 
  - context lifecycle and state (`ntt_ctx`);
  - NTT's parameters configuration (`ntt_config`); 
  - adapter selection and module loading (`ntt_adapter`, `ntt_module`);
  - the on-disk configuration file (`ntt_cfg_file`);
  - transform dispatch into the backend (`ntt_api`); 
  - the injected core API and the platform dynamic-loader shim (`ntt_core`); 
  - shared number theory (`ntt_utils`);
  - leveled logging (`ntt_log`). 
  
  Any future concern slots into one of these categories.

- **Backends.** The backends implement the transform proper. The library reaches them only through the adapter dispatch table, and they reach the library only through the injected core API, which they use solely to read the configuration set by the user. A backend is either compiled into the library (a *built-in*) or shipped separately and loaded at run time (a *module*). Built-ins today come in two flavors: an optimized scalar implementation (**still in progress**) and a plain-reference implementation (**next to be dropped**). Nevertheless, the set of backends and their flavors is open-ended, exactly as the adapter contract intends. The contract itself is described in [Section 2.3](#23-the-ntt_adapter-namespace-backend-descriptor-and-selection).

An *external module* is a backend shipped as a standalone shared library and loaded at run time. It exports an adapter that follows exactly the same contract as a built-in backend, so once loaded the two are indistinguishable from each other. Modules are discussed in detail in [Section 2.5](#25-the-ntt_module-namespace-built-ins-and-run-time-loading) and [Section 3.4](#34-module-loading-lifecycle).

## 2. Components in detail

This section walks through each component in turn, explaining its role, the interface it exposes, and the properties that determine its behavior within the library as a whole. The interactions between these components are covered separately, in [Section 3](#3-how-the-pieces-work-together).

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

The fields `q` and `n` store the mathematical parameters of the transform, and serve primarily to describe what the context represents, rather than how the backend performs the computation. Everything that is implementation-specific, such as twiddle tables, reduction constants, coefficient representations, and any precomputed material, resides in the opaque `state` object returned by `adapter->setup()`.

The common layer never inspects, mutates, or even type-checks `state`. It merely stores it and passes it back to the *vtable* callbacks. This arrangement ensures that the common code treats the adapter state as opaque, while giving each adapter complete freedom to represent and manage its state according to its implementation requirements.

Context creation through `ntt_create()` is where the library performs its most important validation and initialization work. The full pipeline is described in [Section 3.1](#31-context-creation). For the present discussion it suffices to say that `ntt_create()` verifies the adapter's ABI, validates the mathematical parameters and any reduction flags, resolves any unspecified roots of unity, and then hands control to the backend's `setup` callback. Context destruction through `ntt_destroy()` performs the reverse operation: it invokes `adapter->teardown(state)` to release backend resources and then frees the context itself.

### 2.2 The `ntt_config` namespace: transform parameters

`ntt_config` constitutes the user-facing interface for parameterizing a transform. 
The process of turning a specification such as *"a negacyclic convolution modulo `q`, of size `n`"* into a working context is straightforward: allocate the configuration with `ntt_config_new()`, set the mathematical parameters through the `ntt_config_set_*()` setters, and pass the completed configuration to `ntt_create()`. Once `ntt_create()` returns, the resulting context owns everything required to perform the transform, so the configuration can be released with `ntt_config_free()`.

The configuration object has two important properties that shape how it is used. 

1. The setters and getters are trivial field accessors with NULL guards. **No value validation is performed at set time**. All validation is deferred to `ntt_create()`. This is a deliberate choice: the caller assembles the configuration incrementally, field by field, so at the time any individual setter runs the remaining fields may not be known yet. Moreover, individual values only make sense in combination (e.g., `q` must be prime, `n` a power of two, and `omega`/`psi` consistent with the transform type) so checking each setter in isolation would either reject configurations prematurely or scatter validation across many error paths. Deferring every check to `ntt_create()`, when the configuration is complete, concentrates all validation in a single coherent error path.

2. The config object is the *only* piece of common state that crosses the backend boundary. Because backends are forbidden from calling into the library, they read the configuration exclusively through the injected core API ([Section 2.4](#24-the-ntt_core-namespace-injected-api-and-platform-shim)). The config is therefore always passed to a backend read-only, and the backend never retains a reference to it after `setup` returns. In practice this means `ntt_create()` operates on a private copy, so that no call ever mutates the caller's config.

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

The first five fields describe the adapter and provide enough information to determine whether it can be safely used by the current library version. `abi_version` and `struct_size` together allow `ntt__adapter_is_compatible()` to reject a descriptor that is newer than the library, or too small to contain the required trailing fields. The consequence is that a mix of a newer and an older binary fails loudly at load time rather than misbehaving silently at run time. The `capabilities` bitmask advertises the backend's abilities — whether it is vectorized, which reduction algorithms it implements, and so on — and `supported_flags` narrows this to the subset of `NTT_CONFIG_*` flags the backend honors. The selection layer, described below, uses both fields, together with `ntt_adapter_supports_flags()`, to reject configurations the backend cannot honor.

Adapter selection proceeds along three paths, from simplest to most explicit. 
 - `ntt_adapter_get(selector)` selects a built-in adapter by enum (`NTT_ADAPTER_SCALAR`, `NTT_ADAPTER_SCALAR_TOY`, or `NTT_ADAPTER_DEFAULT`), so nothing is touched on disk. 
 - `ntt_adapter_load(name, module_dir)` is the file-based path, and its behavior hinges entirely on `module_dir`. If `module_dir` is NULL, the function consults only the built-in registry by name and fails if the name is unknown. If a directory is supplied, the registry is skipped and the function goes straight to the module loader: it assembles the shared-library path `<module_dir>/<prefix>ntt_adapter_<name><ext>` ([Section 2.5](#25-the-ntt_module-namespace-built-ins-and-run-time-loading)) and loads it, returning NULL on any failure. In other words, `module_dir` is simply where external adapter modules live on disk — pass NULL to stay within the built-ins, pass a directory to load an external module of that name.
 - Finally, the pair `ntt_adapter_set_default()` / `ntt_adapter_get_default()` implements a process-local override and the full default-resolution chain, which is described in [Section 3.3](#33-default-adapter-resolution).

### 2.4 The `ntt_core` namespace: injected API and platform shim

The `ntt_core` namespace serves two distinct purposes, both focused on keeping the backend boundary safe and portable.

The **first** responsibility is the *injected core API*. For an external module to function without linking against libntt, the library must deliver its services to the module by other means. It does so by handing every adapter a `ntt_core_api`: a small, NULL-terminated dispatch table of function pointers covering the configuration accessors (e.g., getters) and the shared utilities (e.g., from `ntt_utils.h`). The table is versioned (`NTT_CORE_API_VERSION`) and append-only: a newer core is always a strict superset of an older one, so a module compiled against an older version continues to function against a newer library. A backend never reaches the library through ordinary symbol references, it uses the typed wrappers defined in `ntt_core.h` instead. For example, a backend reads the modulus with:

```c
uint64_t q = ntt_core_get_modulus(api, config);
```

which expands internally to:

```c
const ntt_dispatch *d = ntt_core_find(api, NTT_FUNC_CONFIG_GET_MODULUS);
return ((uint64_t (*)(const ntt_config *))d->fn)(config);
```

`ntt_core_find()` walks `api->ops` — a NULL-terminated stream of `{id, fn}` pairs — until it locates the requested dispatch id. The typed wrapper then casts the opaque function pointer to the known signature and calls it. This pattern is the *only* route by which a backend may reach the common layer.

Two checks guard this handshake, one per side. On the library's side, `ntt__module_core_is_valid()` refuses to hand over a core with a wrong version, an undersized `struct_size`, or a NULL `ops` stream. On the module's side, `ntt_core_is_compatible(core, min_version)` is the authoritative gate: the module declares the minimum core version it was built against and rejects an older core. Both ends enforce the same version floor, so neither side is ever surprised. 

The **second** responsibility is the *platform dynamic-loader shim*. Because the library must run on both POSIX and Windows systems, it wraps the operating system's loader so that no other component needs to be aware of the platform. The shim consists of three functions and three string constants:

| Function | POSIX | Windows |
|---|---|---|
| `ntt__dlopen` | `dlopen(RTLD_NOW)` | `LoadLibraryA` |
| `ntt__dlsym` | `dlsym` | `GetProcAddress` |
| `ntt__dlclose` | `dlclose` | `FreeLibrary` |
| `ntt__dl_extension` | `.so` | `.dll` |
| `ntt__dl_prefix` | `lib` | (empty) |
| `ntt__dl_separator` | `/` | `\` |

The export macro `NTT_MODULE_EXPORT` (`ntt_core.h`) is likewise `__declspec(dllexport)` on Windows and `visibility("default")` on GCC and Clang. The net effect is that the rest of the library, and in particular the module loader of [Section 2.5](#25-the-ntt_module-namespace-built-ins-and-run-time-loading), is written in a platform-neutral way and yet produces correct behavior on every supported system.

### 2.5 The `ntt_module` namespace: built-ins and run-time loading

The `ntt_module` component is the name-resolution layer of the library through two complementary mechanisms.

The built-in registry is simply a static table that maps adapter names to the getters for adapters compiled into the library. `ntt_adapter_load()` uses `ntt__registry_lookup()` as its fallback when no external module is loaded.

For external modules, `ntt__module_load_from_dir()` handles the loading process:

- It builds the module path according to the platform conventions described in [Section 2.4](#24-the-ntt_core-namespace-injected-api-and-platform-shim): `<module_dir><sep><prefix>ntt_adapter_<name><ext>`.
- It opens the shared library with `ntt__dlopen`.
- It looks up the single symbol that a module must export, `ntt_adapter_module_init`, using `ntt__dlsym`. This function is the module's entry point. The loader passes it the injected core API and a pointer to an adapter slot. The module checks that it can use the supplied core with `ntt_core_is_compatible()` and, if everything is compatible, stores its static adapter descriptor in the slot.
- Finally, the loader validates the injected core with `ntt__module_core_is_valid()` and checks the returned adapter descriptor against the same ABI compatibility rules used for built-in adapters.

There is one important point about keeping modules loaded. An adapter obtained from a module contains function pointers into that module's code, so unloading the module while the adapter is still in use would leave those pointers dangling. To avoid this, the loader keeps successfully opened module handles in a fixed-size cache of 16 slots. The handles remain open until `ntt__module_unload_all()`, called by `ntt_adapter_unload_all()`, closes them. If loading fails at any point, the loader closes the handle immediately and returns NULL, leaving `ntt_adapter_load()` free to fall back to a built-in adapter.

### 2.6 The `ntt_cfg_file` namespace: configuration file

The configuration file is a minimal `key = value` file that picks the default adapter and the module directory. The process with it is simple:

1. **Locate it.** `ntt__config_default_path()` uses `NTT_CONFIG_FILE` if set, otherwise the home directory (`$HOME/.config/libntt/ntt.conf`, with Windows `USERPROFILE` / `HOMEDRIVE`+`HOMEPATH` variants). No file or no resolvable home simply means no config.
2. **Parse it.** `ntt__config_file_load()` reads it line by line, skipping `#` comments and blanks, trimming whitespace, and keeping only the two recognized keys: **`adapter`** and **`module_dir`**. Unknown keys are ignored.
3. **Use the values.** They feed the default-adapter resolution of [Section 3.3](#33-default-adapter-resolution).  With none set, resolution falls through to the built-in `scalar`.

The exact path rules and precedence are in `docs/CONFIGURATION.md`.

### 2.7 The `ntt_api` namespace: transform dispatch

The public transform entry points — `ntt_forward()`, `ntt_inverse()`, and `ntt_negacyclic_mul()` in `src/common/ntt_api.c` — are deliberately thin. Each function has two basic tasks. First, it validates its arguments: the context, adapter, state, coefficient arrays, and the required vtable entry are checked for NULL before anything is dereferenced. Invalid arguments therefore result in an error code rather than an invalid memory access. Second, it forwards the operation to the corresponding adapter callback, passing the context's opaque state as the first argument.

The common layer does not perform any number theory, arithmetic, or transform-specific work at this point. All of that is left to the backend. Once the checks have passed, the dispatch path is essentially a single indirect call through the adapter's vtable. This keeps the common layer independent of the implementation while keeping the runtime overhead of the abstraction small.

### 2.8 The `ntt_log` namespace: leveled logging

Logging is provided by the `ntt_log` component. The macro `NTT_LOG(level, ...)` expands to a call into `ntt__log_write()`, and the message is emitted only if the requested level meets the configured threshold, where the levels are ordered `none < error < info < debug`.

The log level can be controlled in two ways, with the explicit setting taking precedence. A call to `ntt_log_set_level()` overrides the environment and remains in effect until changed again. If the caller has not set a level explicitly, `NTT_LOG_LEVEL` is checked each time a message is written. This makes it possible to change the library's verbosity at run time without reconfiguring or rebuilding it, simply by changing the environment variable.

The level flag is stored in an `_Atomic` type so that reads and writes are safe across threads. When the library is built with `-DNTT_ENABLE_LOGGING=OFF`, `NTT_LOG()` is compiled out entirely, so logging adds zero cost to a production build.

### 2.9 The `ntt_utils` namespace: shared number theory

The `ntt_utils` component provides the pure-arithmetic helpers required at *setup* time: never in the transform hot path which depend on the selected adapter. Its functions perform deterministic Miller-Rabin primality testing over the full `uint64_t` domain, factor `q - 1` (using a fast-path table for well-known moduli (to be updated) and trial division plus Pollard's rho otherwise), search for a primitive root, compute modular square roots with the Tonelli-Shanks algorithm, and provide helpers like `ntt_is_power_of_two()` and `ntt_reverse_bits()`.

**NOTE:** their relative slowness is of no consequence: these functions run at most once per context creation, and that one-time cost is amortized over the entire lifetime of the transform.

### 2.10 The backends (wip)

The library ships two built-in backends. Both are **still work in progress and subject to breaking changes**:

- **`scalar`** — the optimized backend. Accepts a runtime modulus. The transform is radix-2 Cooley-Tukey for the forward pass and radix-2 Gentleman-Sande for the inverse. Modular arithmetic is provided by the two reduction backends, Barrett and Montgomery: operands are kept as canonical residues under Barrett and in the `xR mod q` representation under Montgomery. Barrett reduction is the default, Montgomery is selectable via `NTT_CONFIG_REDUCTION_MONTGOMERY`. `negacyclic_mul` is assembled as twist, forward, pointwise multiply, inverse, untwist. Setup and teardown build and release the reduction constants and any precomputed state.
- **`scalar_toy`** — the reference backend. Exposes the same adapter contract but uses plain `(a * b) % q` multiplication, working for moduli up to `q <= 2^63` (**this adapter requires major adjustments, but I'll eventually drop it once the scalar adapter reaches a mature state**)

Users implementing their own external adapters may consult the example module implementation under `examples/adapters/naive/`, which links nothing against libntt and exports `ntt_adapter_module_init(core, &adapter)` as its only symbol.

## 3. How the pieces work together

[Section 2](#2-components-in-detail) looked at each component on its own. This section looks at how they cooperate: the sequences of calls and the flow of data that bind them together. The library relies on five such protocols, which cover its whole life: creating a context, dispatching a transform, resolving the default adapter, loading a module, and negotiating the adapter boundary.

### 3.1 Context creation

The creation of a context is the single most involved protocol in the library, because it is where every invariant that later operations depend on is established. `ntt_create(adapter, config)` proceeds through six stages:

1. **ABI check**: `ntt__adapter_is_compatible(adapter)` verifies the adapter's ABI version and struct size ([Section 2.3](#23-the-ntt_adapter-namespace-backend-descriptor-and-selection)).
2. **Modulus validation**: `adapter->validate_modulus(config, core)` lets the backend enforce its own intrinsic limits, such as `scalar`'s `q <= 2^63` (with Montgomery additionally requiring an odd modulus).
3. **Flag support**: `ntt_adapter_supports_flags(adapter, flags)` rejects a requested reduction the backend does not honor.
4. **Parameter validation**: `ntt__validate_transform_params(q, n, type)` applies the common domain rules of the NTT.
5. **Root resolution**: `ntt__resolve_roots()` works on a private copy of the config: it factors `q - 1`, finds a primitive root, and derives `omega` and `psi` subject to the rules below.
6. **Setup**: `adapter->setup(&resolved, core)` returns the opaque state, and the context is assembled as `{q, n, adapter, state}`.

Root resolution follows the rules: the cyclic transform requires `n | q - 1`, the negacyclic requires `2n | q - 1`; the two roots satisfy `psi^2 == omega`, and the missing one is derived from the other.

Two aspects of this design are particularly important. First, the common layer handles validation and root derivation, while the backend is responsible for the arithmetic representation and precomputation. This keeps the responsibilities clearly separated. Second, because resolution operates on a copy, `ntt_create()` never modifies the caller's configuration object.

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

The context only stores `q`, `n`, and `omega`/`psi` for reference. Everything the transform actually needs lives in the backend's state. As a result, the hot path is a single indirect call into backend code, with nothing else happening in the common layer.

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

The chain is ordered from the most explicit source to the least: a process-local override set by the application takes precedence over the configuration file, which takes precedence over a loadable module, with the built-in `scalar` adapter as the final fallback. The resolved adapter is cached until `ntt_adapter_set_default()` is called again or `ntt_adapter_unload_all()` clears the cache. The full specification of the sources (e.g., environment variables, file location, and format) is given in `docs/CONFIGURATION.md`.

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

Each stage of the loading process can fail independently. If the path cannot be assembled, the library cannot be opened, the entry symbol is missing, the injected core fails validation, or the returned descriptor fails the ABI check, the handle is closed and the load returns `NULL`. The adapter is handed to the caller only after all checks succeed, while the module handle remains resident so that the vtable inside it cannot dangle.

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

The consequence is that a backend never references a single symbol from libntt. It links only against libc. It receives the configuration through the injected `ntt_core_api` table, and it is the library, not the module, that resolves all its own symbols. This is what makes a third-party backend buildable and shippable with no dependency on the library at all, and why the version handshake of [Section 2.4](#24-the-ntt_core-namespace-injected-api-and-platform-shim) exists: the only contract between the two sides is the versioned, append-only core table.

## 4. Extension points

The architecture is designed so that adding a new backend requires no change to the common layer. The complete procedure is:

1. Implement an `ntt_adapter` descriptor — name, capabilities, supported flags, and the six callbacks — in a new directory `src/adapters/<name>/`.
2. Register it as a built-in by adding a getter to the built-in registry in `ntt_module.c` and its source to `src/CMakeLists.txt`. Alternatively, ship it as a module: a standalone shared library that exports `ntt_adapter_module_init` and nothing else.
3. If the backend imposes modulus constraints beyond the common rules, implement `validate_modulus`. If it supports a selectable reduction, or any other feature, declare the corresponding flags in `supported_flags`.

No modification of the common core is required: the adapter descriptor and the injected core API are the entire interface to which a new backend must conform.

## Conclusion

The library can be summarized as a stable, narrow public interface that sits on top of a portable, adapter-independent core. Behind that boundary, interchangeable backends can be either compiled into the library or loaded at run time. They implement the actual transform while communicating with the core exclusively through the versioned, injected API.

The components described in this document exist to keep that separation clear and enforceable without adding unnecessary complexity. The protocols in [Section 3](#3-how-the-pieces-work-together) provide the mechanisms that make the boundary work in practice.

For anyone extending the library, the important pieces are the adapter descriptor and the injected core API. Everything else is supporting infrastructure and can remain out of the way.