# BCSV Examples — AI Skills Reference

> Quick-reference for AI agents working with example programs.
> For CLI tool documentation, see: [src/tools/CLI_TOOLS.md](../src/tools/CLI_TOOLS.md)

## Build

```bash
# All examples build with the default cmake configuration
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Build a single target
cmake --build build --target example -j$(nproc)
```

All executables output to `build/bin/`.

## Build Targets (7 total)

| Target | Source | Description |
|--------|--------|-------------|
| `example` | example.cpp | Flexible layout — dynamic schema, write + read |
| `example_static` | example_static.cpp | Static layout — compile-time typed schema |
| `example_zoh` | example_zoh.cpp | Zero-Order-Hold compression (flexible layout) |
| `example_zoh_static` | example_zoh_static.cpp | ZoH with static layout |
| `visitor_examples` | visitor_examples.cpp | Visit pattern examples (const + mutable visitors) |
| `c_api_vectorized_example` | c_api_vectorized_example.c | C API vectorized read example |
| `example_sampler` | example_sampler.cpp | Sampler API demonstration (filter & project) |

## Source Structure

All example source code is in `examples/`:
- `CMakeLists.txt` defines all 7 targets, outputs to `${CMAKE_BINARY_DIR}/bin`
- `README.md` has API usage patterns and troubleshooting
