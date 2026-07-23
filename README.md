# NTT

A generic and extensible Number Theoretic Transform (NTT) library written in C.

The library is designed to separate the **mathematical NTT interface** from the **implementation of the underlying arithmetic and transform algorithms**. This allows different implementations, such as reference scalar code, optimized scalar arithmetic, SIMD/vectorized implementations, or hardware-specific implementations, to coexist behind a common interface.

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
         Scalar Toy    Scalar       Future SIMD
             │            │            │
             ▼            ▼            ▼
        Arithmetic    Arithmetic    Hardware /
        Implementation Implementation Vectorized
```

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
flags  Requested configuration options
```

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
    └── optimized modular reduction

Future Montgomery Adapter
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
uint32_t mulmod(uint32_t a, uint32_t b, uint32_t q);
```

Therefore, arithmetic implementations are considered **internal implementation details of an Adapter**.

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

This model also allows applications to select different Adapters built for a target architecture (TODO) at runtime.

For example, a benchmark application could execute the same NTT operation using different adapters without changing the mathematical input or the public NTT API.

---

## Public API vs Internal Implementation

The project separates public API from internal implementation.

Public headers expose:

* NTT context management;
* NTT configuration;
* NTT operations;
* Adapter definitions and Adapter ABI;
* Adapter discovery and capability queries.

Internal source code contains:

* Adapter implementations;
* modular arithmetic;
* reduction algorithms;
* precomputation;
* lookup tables;
* implementation-specific state.

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

8. **Applications can select and query Adapters at runtime.**

9. **New Adapters can be developed without modifying the common NTT logic.**

10. **The architecture is intended to support future optimized and hardware-specific implementations.**

The central design goal is therefore:

> **Define a stable interface for the mathematical NTT operation while allowing complete freedom in how an Adapter implements arithmetic and transforms internally.**
