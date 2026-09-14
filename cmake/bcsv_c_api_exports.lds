/*
 * Version script for libbcsv_c_api on ELF platforms (docs/adr/0007).
 *
 * The library's entire ABI is the bcsv_* C API. Every other symbol must stay
 * local to this DSO — above all the C++ runtime, which self-contained
 * packaging builds link statically. Exported C++ symbols are interposable: a
 * host already carrying its own libstdc++ copy wins the global lookup for
 * calls this library makes through PLT or vtable paths, so one runtime
 * constructs an object the other destroys — a heap abort. With the definitions
 * local they cannot be replaced, only used.
 */
{
    global:
        bcsv_*;
    local:
        *;
};
