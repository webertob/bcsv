# ADR-002: No Explicit Version Compatibility Check on Read

**Status:** Accepted  
**Date:** 2026-04-19  
**Review:** v1.5.6 critical review (finding #12)

## Context

When reading a BCSV file, the Reader does not explicitly validate that the
file's `version_major.minor.patch` from the header is one it "understands."
A reviewer suggested adding a compatibility check to reject files from future
versions that might use an incompatible wire format.

## Decision

**Do not add a version-range check.** Rely on the existing version-gated codec
registry and structural validation instead.

### Rationale

1. **Version-gated codec registry** (introduced in v1.5.0) already maps
   `(fileMinor, flags)` → concrete codec. If a file uses an unknown codec
   combination, `resolveRowCodecId()` / `resolveFileCodecId()` will return an
   error or fall through to a mismatch, causing a clear read failure.

2. **Structural validation** catches incompatible formats: magic number, column
   type bounds (added in v1.5.6), packet size bounds (added in v1.5.6), and
   column count limits all reject corrupted or incompatible headers.

3. **Hard version checks are brittle.** A `if (file.major > MY_MAJOR) reject`
   rule would prevent reading files that are actually compatible (e.g. a v1.6
   file that uses only v1.5 codecs). The wire format is designed to be
   extensible via flags and codec IDs, not via version gating.

4. **Fail-fast on actual incompatibility** is already the behavior: if the codec
   dispatch fails or the deserialization encounters unexpected data, the Reader
   returns `false` with a descriptive error message.

## Consequences

- A file written by a future version using a new codec will fail at codec
  dispatch or deserialization, not at a version check. The error message will
  describe the structural problem (e.g. "unknown row codec ID") rather than
  "incompatible version."
- This is acceptable because BCSV is pre-2.0 and does not yet guarantee
  backward compatibility across minor versions (documented in README).
