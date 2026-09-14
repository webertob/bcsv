# ADR-0007: Hide all non-C-API symbols in the shared C library

**Status:** Accepted
**Date:** 2026-09-14

## Context

`libbcsv_c_api` is a C ABI over a C++ implementation. Self-contained packaging
(Unity, NuGet, installs onto old glibc hosts) builds it on Linux with
`-static-libstdc++ -static-libgcc` so the artifact needs no particular
`GLIBCXX` on the target. The static runtime links with default visibility:
the shipped `.so` re-exported ~2300 C++ symbols — `std::`, `__cxa_`, vtables,
out-of-line `operator new/delete` (60 `_ZNSt6locale*` alone, `nm -D` against
`v1.5.19-upm`/`v1.5.21-upm`).

ELF resolves global symbols process-wide: the copy loaded first interposes.
A host that already carries a libstdc++ copy — a Unity player, Mono, an
embedded Python, anything that loads the library after its own runtime —
wins the lookup for every `std::` call not bound directly by
`-fno-semantic-interposition` (1.5.20), i.e. everything reaching a PLT,
vtable, or out-of-line path. One runtime copy then constructs what the other
destroys; the heap is corrupted, the first destroy often survives, the
second aborts (`free(): invalid size`, SIGABRT). The first field report was
a Unity player-exit abort on the second handle destroy
(`com.bcsv.unity` defect 3, 1.5.19/1.5.21).

The failure is a property of the deployment shape — a static C++ runtime
plus a foreign libstdc++ in one global namespace — not of any bcsv object.
Handle bookkeeping cannot fix it: with two runtimes tearing objects down
across each other, any destroy can be the fatal one.

## Decision

On ELF platforms, link `bcsv_c_api` with a version script that exports only
the `bcsv_*` C API and forces every other symbol local to the DSO
(`cmake/bcsv_c_api_exports.lds` via `target_link_options`):

```lds
{ global: bcsv_*; local: *; };
```

This applies to every ELF build of the library — system install, NuGet,
Unity — not only the static-runtime ones; for a dynamically linked build the
script hides nothing the host could not already resolve through the real
libstdc++. Two tripwires assert the dynamic table defines exactly `bcsv_*`:
CTest `bcsv_c_api_export_surface` for the unit tree, and
`scripts/check_versions.py --native` — the artifact gate every packaging
workflow already runs, including the jobs that build with `BUILD_TESTS=OFF`.

## Options considered

1. **`-Wl,--exclude-libs,ALL`** — drops exported symbols from *archive*
   members, so it targets only static-libstdc++ and its exact scope varies
   across ld/lld/gold. Rejected; the version script covers archive and
   directly-defined symbols in one portable, auditable mechanism.
2. **`-fvisibility=hidden` + a per-function export attribute** — same result,
   but a header attribute per function (168), consumer vendored copies, and a
   new rule per API addition. Rejected for churn.
3. **Ship a dynamically linked libstdc++** — abandons the glibc/glibcxx floor
   the artifacts are built for. (With this fix, dynamic linking is trivially
   safe — one runtime per process — which is why locally built debug copies
   never showed the bug.)
4. **Remove the faulting C++ use (`std::locale`) from the file layer** — not
   feasible: `basic_ifstream`/`basic_ofstream` members own a locale, and any
   other C++ object destroyed through an interposed `operator delete` fails
   the same way.
5. **Consumer-side workaround only** (skip destroys, leak handles to shutdown)
   — relocates the abort to process exit, does not fix it. Rejected as a fix;
   it becomes deletable with this change.

## Consequences

- The library's C++ symbols can no longer be seen — and therefore not
  interposed — by another runtime copy in the host; each side allocates and
  frees its own objects. Field crashes from this shape have no path left.
- The export surface is a tested contract: a non-`bcsv_*` export, or removing
  the version script, breaks `ctest` and the packaging job loudly.
- Windows (COFF, `WINDOWS_EXPORT_ALL_SYMBOLS`) and macOS (two-level
  namespace) are unchanged: neither has ELF interposition.
- A host that linked against `libbcsv_c_api` *expecting* stray C++ symbols
  gets an unresolved-symbol error: that relied on an implementation detail,
  not ABI.
