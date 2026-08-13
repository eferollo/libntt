# libntt (WIP)

A portable and extensible Number Theoretic Transform (NTT) library written in C.

## The idea

The library is designed to separate the **mathematical NTT interface** from the **implementation of the underlying arithmetic and transform algorithms**. The implementation is delegated to interchangeable backends called *Adapters*: a reference scalar backend, an optimized scalar backend, and — whenever you need them — your own SIMD, vectorized, or hardware-specific implementations, all behind the same interface.

The central design goal was forced from the very beginning by one idea: give *anyone* the ability to easily optimize whatever NTT operation their project relies on (e.g., PQC, FHE, or anything else), and hand them a high degree of freedom to modify and optimize an implementation through Adapters according to their own constraints. The architecture exists to make that possible:

> **Define a stable interface for the mathematical NTT operation while allowing complete freedom in how an Adapter implements arithmetic and transforms internally.**

No matter what an Adapter does internally — a different reduction strategy, a different coefficient representation, targeted assembly, or hardware-specific state — the public API and the common layer stay untouched, so a user only ever writes the part that matters to them.

The general schema at a glance is the following:
1. The **public API** is the only thing a user includes. Everything below it is invisible.
2. The **common core** implements the adapter-independent logic: context lifecycle, configuration, adapter selection, module loading, transform dispatch, and logging.
3. The **backends** implement the actual transform and arithmetic, and are reached only through the adapter dispatch table and the injected core API — never through direct library calls. The same contract also admits external modules, standalone shared libraries loaded at run time without recompiling or relinking the library.

**NOTE:** The built-in Adapters are still under development: their behavior and configuration flags are subject to change until the project stabilizes.

> [!CAUTION]
> I drew inspiration from projects like OpenSSL Providers but simplified and adapted the design to fit this project. For a clearer picture of what I've built, please read [ARCHITECTURE.md](docs/ARCHITECTURE.md).
>
>Everything is still at an alpha stage and not ready for use. I've laid the first stone, but I'm sure there's plenty to change, refine, and improve, and I'm very open to better ideas and approaches. I have some experience in this field, but far less than many potential contributors—especially when it comes to the mathematical side.
>
>This project started while I was studying lattice-based PQC, partly as a way to deepen my understanding and improve my chances of landing a job in this field. So any feedback, suggestion, or improvement is more than welcome. Feel free to open an issue or reach out by email. I haven't written contributing guidelines yet, but feel free to open an MR or issue in whatever way feels most appropriate.

## Repository layout

- [include/ntt](include/ntt/) — public API (the only headers a user includes).
- [src/common](src/common/) — shared NTT logic (context, config, dispatch).
- [src/common/headers](src/common/headers/) — private internal headers.
- [src/adapters](src/adapters/) — backend implementations (`scalar_toy/`, `scalar/`).
- [tests](tests/) — unit tests and integration tests.
- [docs](docs/) — Useful insights on the library and Doxygen documentation.
- [verification](verification/) — Formal verification of the built-in Adapters (Lean 4) - **TBD**.
- [INSTALL.md](INSTALL.md) — build, test, install, and uninstall instructions.

## Documentation

- `INSTALL.md` — build, test, install, clean, and uninstall the library.
- `docs/CONFIGURATION.md` — build-time options, environment variables, and the configuration file.
- `docs/ARCHITECTURE.md` — a detailed description of the library components and how they interact.
- An HTML API reference can be generated on demand with `cmake --build build --target docs` (see `INSTALL.md`).

## Authors

- Francesco Rollo <eferollo@gmail.com>

## License

Copyright 2026 Francesco Rollo.

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full text.
