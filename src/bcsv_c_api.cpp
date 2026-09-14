/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include "bcsv/bcsv_c_api.h"
#include <string>
#include <sstream>
#include <exception>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// Include full implementations (headers + .hpp files)
#include "bcsv/bcsv.h"  // This includes all implementations

// Sampler includes
#include "bcsv/sampler/sampler.h"
#include "bcsv/sampler/sampler.hpp"

#if defined(__GNUC__) || defined(__clang__)
// initial-exec: the hot getters read/write g_has_error on every successful
// call; without this, a shared library gets global-dynamic TLS and pays a
// __tls_get_addr PLT call per getter. glibc's static-TLS surplus covers this
// image (~96 B of TLS segment) even under dlopen. MSVC's TLS is already
// IE-equivalent.
#define BCSV_TLS_FAST __attribute__((tls_model("initial-exec")))
#else
#define BCSV_TLS_FAST
#endif

namespace {
BCSV_TLS_FAST thread_local std::string g_last_error;
BCSV_TLS_FAST thread_local bool g_has_error = false;   // flag-based: avoids string::clear() per call
BCSV_TLS_FAST thread_local std::string g_fmt_buf;      // reusable buffer for to_string helpers

inline void clear_last_error() noexcept {
    g_has_error = false;
}

void set_last_error(const char* where, const std::exception& ex) noexcept {
    g_has_error = true;
    g_last_error = std::string(where) + ": " + ex.what();
}

void set_last_error_unknown(const char* where) noexcept {
    g_has_error = true;
    g_last_error = std::string(where) + ": unknown exception";
}

/// Returns true (and sets error) when @p h is nullptr.
static bool null_handle(const char* where, const void* h) noexcept {
    if (!h) [[unlikely]] {
        g_has_error = true;
        g_last_error = std::string(where) + ": NULL handle";
        return true;
    }
    return false;
}

// ---- Writer handle: pre-bound function pointers (select-once pattern) ------
// Mirrors the RowCodecDispatch approach used in the C++ Reader: function
// pointers are wired once at construction time, so every subsequent call goes
// through a single indirect call instead of a per-call switch/case.

struct WriterHandle {
    enum class Type { Flat, ZoH, Delta } type;   // kept for open() flag auto-set
    void* ptr;                                    // concrete Writer<L,Codec>*
    bcsv::Row* cached_row;                        // stable pointer to writer's Row

    // Pre-bound trampolines — set once by createWriterHandle()
    void   (*writeRow_fn)(void*);
    void   (*write_fn)(void*, const bcsv::Row&);
    void   (*close_fn)(void*);
    void   (*flush_fn)(void*);
    void   (*delete_fn)(void*);
    bool   (*isOpen_fn)(const void*);
    bool   (*isPoisoned_fn)(const void*);
    size_t (*rowCount_fn)(const void*);
    const bcsv::Layout* (*layout_fn)(const void*);
    const std::string*  (*errorMsg_fn)(const void*);
    uint8_t (*compressionLevel_fn)(const void*);
    int     (*fileFlags_fn)(const void*);
#ifdef _WIN32
    const wchar_t* (*filePath_fn)(const void*);
#else
    const char* (*filePath_fn)(const void*);
#endif
    bool (*open_fn)(void*, const char*, bool, size_t, size_t, bcsv::FileFlags);
};

/// Factory: wires all function pointers once for a concrete Writer type W.
template<typename W>
WriterHandle* createWriterHandle(WriterHandle::Type type, W* writer) {
    auto* h = new WriterHandle();
    h->type       = type;
    h->ptr        = writer;
    h->cached_row = &writer->row();
    h->writeRow_fn = [](void* p) { static_cast<W*>(p)->writeRow(); };
    h->write_fn    = [](void* p, const bcsv::Row& r) { static_cast<W*>(p)->write(r); };
    h->close_fn    = [](void* p) { static_cast<W*>(p)->close(); };
    h->flush_fn    = [](void* p) { static_cast<W*>(p)->flush(); };
    h->delete_fn   = [](void* p) { delete static_cast<W*>(p); };
    h->isOpen_fn   = [](const void* p) -> bool    { return static_cast<const W*>(p)->isOpen(); };
    h->isPoisoned_fn = [](const void* p) -> bool  { return static_cast<const W*>(p)->isPoisoned(); };
    h->rowCount_fn = [](const void* p) -> size_t  { return static_cast<const W*>(p)->rowCount(); };
    h->layout_fn   = [](const void* p) -> const bcsv::Layout*  { return &static_cast<const W*>(p)->layout(); };
    h->errorMsg_fn = [](const void* p) -> const std::string*   { return &static_cast<const W*>(p)->getErrorMsg(); };
    h->compressionLevel_fn = [](const void* p) -> uint8_t { return static_cast<const W*>(p)->compressionLevel(); };
    h->fileFlags_fn = [](const void* p) -> int { return static_cast<int>(static_cast<const W*>(p)->fileFlags()); };
    h->filePath_fn = [](const void* p) { return static_cast<const W*>(p)->filePath().c_str(); };
    h->open_fn     = [](void* p, const char* fn, bool ow, size_t cl, size_t bs, bcsv::FileFlags ff) -> bool {
        return static_cast<W*>(p)->open(fn, ow, cl, bs, ff);
    };
    return h;
}

// ---- Sampler handle -------------------------------------------------------
struct SamplerHandle {
    bcsv::Sampler<bcsv::Layout>* sampler;
    std::string                  error_msg;  // cached error message
};

// ---- Columnar read state (per reader, reused across calls) -----------------
struct ColumnarReadState {
    std::vector<std::vector<std::string>> string_cols;  // [col][row]
    void clear() { for (auto& v : string_cols) v.clear(); }
    void resize(size_t num_cols) { string_cols.resize(num_cols); }
};

// Attach columnar state to each reader via a side map (avoids changing
// the Reader class — we use the opaque handle address as key).
// Process-wide (registry-mutex-guarded), not thread_local: a reader can be
// destroyed on a finalizer thread other than the one that read, and a
// thread-local map would strand the entry for a recycled reader address to
// inherit. Columnar calls are bulk/cold, so one shared lock per call is fine.
struct ColumnarStateTable {
    std::mutex mtx;
    std::unordered_map<const void*, ColumnarReadState> by_reader;
};
inline ColumnarStateTable& columnar_table() {
    static auto* t = new ColumnarStateTable();   // leaky: finalizers may touch after exit begins
    return *t;
}
inline void columnar_erase(const void* reader) {
    auto& t = columnar_table();
    std::lock_guard<std::mutex> lk(t.mtx);
    t.by_reader.erase(reader);
}
inline void columnar_clear_all() {
    auto& t = columnar_table();
    std::lock_guard<std::mutex> lk(t.mtx);
    t.by_reader.clear();
}
// Lock only covers the lookup; dereferencing the result follows the same
// "one handle, one thread at a time" contract as every other handle call.
inline const ColumnarReadState* columnar_find(const void* reader) {
    auto& t = columnar_table();
    std::lock_guard<std::mutex> lk(t.mtx);
    auto it = t.by_reader.find(reader);
    return it == t.by_reader.end() ? nullptr : &it->second;
}

// ---- Handle registry (1.5.20) ----------------------------------------------
// Process-wide table of live handles. Two goals:
//   1. Idempotent *_destroy: a second destroy (e.g. GC finalizer racing an
//      explicit Dispose on the main thread) claims the handle once, finds the
//      slot empty the second time, and becomes a logged no-op instead of a
//      double-free. The corruption a double-free causes (glibc heap metadata)
//      otherwise aborts later inside unrelated teardown — the "free():
//      invalid size" at player exit.
//   2. bcsv_shutdown(): deterministic bulk teardown callable from any host
//      exit hook, on whatever thread finalizers will probe later.
// Deliberately NEVER destroyed: a registry whose mutex could be torn down by
// static destructors while a finalizer thread is still calling destroy would
// recreate the very exit-order hazard this fixes. One small leak at exit is
// the correct trade. Create/destroy are cold lifecycle calls (rows are
// obtained from a writer/reader without allocation), so the mutex never sits
// on a per-row hot path.
enum class HandleKind : uint8_t { Row, Sampler, Reader, CsvReader, CsvWriter, Writer, Layout };

struct HandleRegistry {
    std::mutex mtx;
    std::unordered_map<void*, HandleKind> live;
};

inline HandleRegistry& registry() {
    // Leaky singleton, thread-safe magic static.
    static HandleRegistry* r = new HandleRegistry();
    return *r;
}

void* reg_register(void* h, HandleKind k) {
    auto& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.live.emplace(h, k);
    return h;
}

/// destroy() guard: claim-or-report in one lock. Returns true when the caller
/// now owns @p h for destruction; false = logged no-op (double destroy,
/// borrowed handle such as bcsv_writer_row(), or foreign pointer). A mutex
/// failure is reported, not propagated: this runs inside extern "C".
bool reg_take(const char* where, void* h) {
    try {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        auto it = reg.live.find(h);
        if (it != reg.live.end()) { reg.live.erase(it); return true; }
    } catch (...) {
        g_has_error = true;
        g_last_error = std::string(where) + ": handle registry unavailable (destroy ignored)";
        return false;
    }
    g_has_error = true;
    g_last_error = std::string(where) + ": handle already destroyed or not owned by this API (destroy ignored)";
    return false;
}

/// close/flush guard: is @p h still a live registered handle? A handle freed
/// by bcsv_shutdown() (or an earlier destroy) must never be touched again —
/// a GC finalizer running Dispose (close-before-destroy) after shutdown would
/// otherwise call through freed memory, the same abort class this registry
/// exists to prevent. Find-without-erase: the handle stays live for its owner.
bool reg_contains(const char* where, void* h) {
    try {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        if (reg.live.contains(h)) return true;
    } catch (...) {
        g_has_error = true;
        g_last_error = std::string(where) + ": handle registry unavailable (call ignored)";
        return false;
    }
    g_has_error = true;
    g_last_error = std::string(where) + ": handle already destroyed or not owned by this API (call ignored)";
    return false;
}

// ---- Cell-type dispatch helpers (1.5.20, docs/adr/0006) --------------------
// The C API must never reinterpret a cell as a non-matching type. Older
// builds routed every bcsv_row_get_* through the strict C++ Row::get<T>(),
// which only type-checks when RANGE_CHECKING is enabled; with the constant
// flipped off (embedded builds) a bcsv_row_get_double on a FLOAT column read
// 4 bytes past the cell. Here the column type is checked unconditionally,
// once per call, and the typed accessor is called only for the case that
// matches: same observable behavior in every build configuration, and the
// mismatch path is an error instead of a silent 0.
//
// Mismatch paths are [[unlikely]]-annotated throwers, so the success path
// stays the fall-through in the hot trace: consumer loops read the same
// (kind-stable) column layout every row and the checks predict near-perfectly.
BCSV_ALWAYS_INLINE bool row_index_ok(const bcsv::Row* r, int col) noexcept {
    return col >= 0 && static_cast<size_t>(col) < r->layout().columnCount();
}

[[noreturn]] inline void row_index_error(const char* fn, int col) {
    throw std::out_of_range(std::string(fn) + ": column index " + std::to_string(col) + " out of range");
}

[[noreturn]] inline void row_type_error(const char* fn, const bcsv::Row* r, size_t col, std::string_view wanted) {
    throw std::runtime_error(std::string(fn) + ": type mismatch at column " + std::to_string(col) +
                             ". Requested: " + std::string(wanted) +
                             ", Actual: " + std::string(toString(r->layout().columnType(col))));
}

/// Strict scalar cell read: value iff the column type is exactly T's.
/// Never throws on success; out-of-range/type mismatch throw (caught by the
/// BCSV_ROW_GET / try_get wrappers, never seen as a C type-pun by any caller).
/// inlined into every exported getter, one fewer indirect hop per cell.
template<typename T>
BCSV_ALWAYS_INLINE bool row_cell_strict(
        const bcsv::Row* r, int col, const char* fn, T* out) {
    if (!row_index_ok(r, col)) [[unlikely]] row_index_error(fn, col);
    const auto idx = static_cast<size_t>(col);
    if (r->layout().columnType(idx) != bcsv::toColumnType<T>()) [[unlikely]]
        row_type_error(fn, r, idx, toString(bcsv::toColumnType<T>()));
    *out = r->get<T>(idx);   // type verified above; no reinterpret hazard
    return true;
}

/// Strict scalar read returning the value (plain getters; same rule as above).
template<typename T>
BCSV_ALWAYS_INLINE T row_cell_value(const bcsv::Row* r, int col, const char* fn) {
    T v{};
    row_cell_strict(r, col, fn, &v);
    return v;
}

/// STRING cell read: pointer into the cell, valid until the cell changes.
/// Kept apart from row_cell_strict<std::string>: a std::string return by value
/// through the twins would dangle at the next cell write.
inline const char* row_cell_string(const bcsv::Row* r, int col, const char* fn) {
    if (!row_index_ok(r, col)) [[unlikely]] row_index_error(fn, col);
    const auto idx = static_cast<size_t>(col);
    if (r->layout().columnType(idx) != bcsv::ColumnType::STRING) [[unlikely]]
        row_type_error(fn, r, idx, "string");
    return r->get<std::string>(idx).c_str();
}

/**
 * Lossless widening read for bcsv_row_get_double (ADR-0006): BOOL, integers
 * up to 32 bit and FLOAT convert to double exactly; INT64/UINT64 (> 2^53 is
 * inexact) and STRING stay strict type errors, as before this change.
 * The DOUBLE hit — the by-type majority — is an inline compare + load; the
 * widening arms live in this noinline cold function so the exported getter
 * stays inside its inline budget (measured 2026-09-14: a single-function
 * form cost 4x on DOUBLE columns — GCC kept it out-of-line and the switch
 * re-expanded core range checks as PLT calls). Widening consumers hit the
 * same arm every iteration (layout kinds are stable), so the switch
 * predicts.
 */
BCSV_NOINLINE double row_cell_widen(const bcsv::Row* r, size_t idx, const char* fn) {
    switch (r->layout().columnType(idx)) {
        case bcsv::ColumnType::BOOL:   return r->get<bool>(idx) ? 1.0 : 0.0;
        case bcsv::ColumnType::INT8:   return static_cast<double>(r->get<int8_t>(idx));
        case bcsv::ColumnType::INT16:  return static_cast<double>(r->get<int16_t>(idx));
        case bcsv::ColumnType::INT32:  return static_cast<double>(r->get<int32_t>(idx));
        case bcsv::ColumnType::UINT8:  return static_cast<double>(r->get<uint8_t>(idx));
        case bcsv::ColumnType::UINT16: return static_cast<double>(r->get<uint16_t>(idx));
        case bcsv::ColumnType::UINT32: return static_cast<double>(r->get<uint32_t>(idx));
        case bcsv::ColumnType::FLOAT:  return static_cast<double>(r->get<float>(idx));
        default: [[unlikely]] row_type_error(fn, r, idx, "double (lossless-widened set: bool/int8..int32/uint8..uint32/float)");
    }
}

BCSV_ALWAYS_INLINE double row_cell_to_double(
        const bcsv::Row* r, int col, const char* fn) {
    if (!row_index_ok(r, col)) [[unlikely]] row_index_error(fn, col);
    const auto idx = static_cast<size_t>(col);
    if (r->layout().columnType(idx) == bcsv::ColumnType::DOUBLE) return r->get<double>(idx);
    return row_cell_widen(r, idx, fn);
}

} // namespace

#define BCSV_CAPI_TRY_RETURN(where, fallback, expr) \
    try { \
        clear_last_error(); \
        return (expr); \
    } catch (const std::exception& ex) { \
        set_last_error(where, ex); \
        return (fallback); \
    } catch (...) { \
        set_last_error_unknown(where); \
        return (fallback); \
    }

#define BCSV_CAPI_TRY_VOID(where, stmt) \
    try { \
        clear_last_error(); \
        stmt; \
    } catch (const std::exception& ex) { \
        set_last_error(where, ex); \
    } catch (...) { \
        set_last_error_unknown(where); \
    }

// Lean hot-path macros for row get/set — skip the null-handle check (row
// handles always come from a writer/reader that validated its own handle).
// Fresh error channel per call: docs/ERROR_HANDLING.md §5. The clear goes
// BEFORE the read so the cell access stays a tail call. The try/catch is
// required to prevent exceptions propagating through extern "C".

#define BCSV_ROW_GET(fallback, expr) \
    try { \
        clear_last_error(); \
        return (expr); \
    } catch (const std::exception& ex) { \
        set_last_error(__func__, ex); \
        return (fallback); \
    } catch (...) { \
        set_last_error_unknown(__func__); \
        return (fallback); \
    }

#define BCSV_ROW_SET(stmt) \
    try { \
        stmt; \
        clear_last_error(); \
    } catch (const std::exception& ex) { \
        set_last_error(__func__, ex); \
    } catch (...) { \
        set_last_error_unknown(__func__); \
    }

extern "C" {

// ============================================================================
// Version API
// ============================================================================
const char* bcsv_version(void) {
    return bcsv::version::STRING;
}
int bcsv_version_major(void) { return bcsv::VERSION_MAJOR; }
int bcsv_version_minor(void) { return bcsv::VERSION_MINOR; }
int bcsv_version_patch(void) { return bcsv::VERSION_PATCH; }

// ============================================================================
// Lifecycle API
// ============================================================================
// Closes every open writer/csv-writer (footer lands while the runtime is
// alive), then destroys all registered handles, borrowers before lenders.
// Contract, rationale and the recycled-address caveat: docs/adr/0006.
void bcsv_shutdown(void) {
    std::unordered_map<void*, HandleKind> batch;
    {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        batch.swap(reg.live);   // claim everything; later destroys see empty
    }

    // Best-effort: shutdown must never propagate — extern "C", process exit
    // path; every stage runs even if an earlier handle's teardown threw.
    auto swallow = [](auto&& fn) noexcept { try { fn(); } catch (...) {} };

    // Phase 1: finalize files while the runtime is fully alive, so the last
    // packet and footer land on disk deterministically.
    for (auto& [ptr, kind] : batch) {
        if (kind == HandleKind::Writer)
            swallow([&]{ auto* h = static_cast<WriterHandle*>(ptr); h->close_fn(h->ptr); });
        else if (kind == HandleKind::CsvWriter)
            swallow([&]{ static_cast<bcsv::CsvWriter<bcsv::Layout>*>(ptr)->close(); });
    }

    // Phase 2: destroy — kind-ordered so borrowers die before lenders
    // (writer borrows its layout's schema; sampler borrows its reader).
    for (auto pass = 0; pass < 2; ++pass) {
        for (auto& [ptr, kind] : batch) {
            if (pass == 0) {   // owners & borrowers
                switch (kind) {
                    case HandleKind::Writer:
                        swallow([&]{ auto* h = static_cast<WriterHandle*>(ptr); h->delete_fn(h->ptr); delete h; });
                        break;
                    case HandleKind::CsvWriter:
                        swallow([&]{ delete static_cast<bcsv::CsvWriter<bcsv::Layout>*>(ptr); });
                        break;
                    case HandleKind::Sampler:
                        swallow([&]{ auto* h = static_cast<SamplerHandle*>(ptr); delete h->sampler; delete h; });
                        break;
                    case HandleKind::Row:
                        swallow([&]{ delete static_cast<bcsv::Row*>(ptr); });
                        break;
                    default: break;
                }
            } else {           // lenders
                switch (kind) {
                    case HandleKind::Reader:
                        swallow([&]{ columnar_erase(ptr); delete static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(ptr); });
                        break;
                    case HandleKind::CsvReader:
                        swallow([&]{ delete static_cast<bcsv::CsvReader<bcsv::Layout>*>(ptr); });
                        break;
                    case HandleKind::Layout:
                        swallow([&]{ delete static_cast<bcsv::Layout*>(ptr); });
                        break;
                    default: break;
                }
            }
        }
    }
    columnar_clear_all();
}
bcsv_layout_t bcsv_layout_create() {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_create", nullptr, reg_register(new bcsv::Layout(), HandleKind::Layout))
}

bcsv_layout_t bcsv_layout_clone(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_clone", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_clone", nullptr, reg_register(new bcsv::Layout(static_cast<const bcsv::Layout*>(layout)->clone()), HandleKind::Layout))
}

void bcsv_layout_destroy(bcsv_layout_t layout) {
    if (!layout) return;
    if (!reg_take("bcsv_layout_destroy", layout)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_destroy", delete static_cast<bcsv::Layout*>(layout))
}

bool bcsv_layout_has_column(const_bcsv_layout_t layout, const char* name) {
    if (null_handle("bcsv_layout_has_column", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_has_column", false, static_cast<const bcsv::Layout*>(layout)->hasColumn(name))
}

size_t bcsv_layout_column_count(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_column_count", layout)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_count", 0u, static_cast<const bcsv::Layout*>(layout)->columnCount())
}

size_t bcsv_layout_column_index(const_bcsv_layout_t layout, const char* name) {
    if (null_handle("bcsv_layout_column_index", layout)) return SIZE_MAX;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_index", SIZE_MAX, static_cast<const bcsv::Layout*>(layout)->columnIndex(name))
}

const char* bcsv_layout_column_name(const_bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_column_name", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_name", static_cast<const char*>(nullptr), static_cast<const bcsv::Layout*>(layout)->columnName(index).c_str())
}

bcsv_type_t bcsv_layout_column_type(const_bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_column_type", layout)) return (bcsv_type_t)255;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_type", (bcsv_type_t)255, (bcsv_type_t)static_cast<const bcsv::Layout*>(layout)->columnType(index))
}

bool bcsv_layout_set_column_name(bcsv_layout_t layout, size_t index, const char* name) {
    if (null_handle("bcsv_layout_set_column_name", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_set_column_name", false, (static_cast<bcsv::Layout*>(layout)->setColumnName(index, name), true))
}

void bcsv_layout_set_column_type(bcsv_layout_t layout, size_t index, bcsv_type_t type) {
    if (null_handle("bcsv_layout_set_column_type", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_set_column_type", static_cast<bcsv::Layout*>(layout)->setColumnType(index, static_cast<bcsv::ColumnType>(type)))
}

bool bcsv_layout_add_column(bcsv_layout_t layout, size_t index, const char* name, bcsv_type_t type) {
    if (null_handle("bcsv_layout_add_column", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_add_column", false, ([&]() {
        bcsv::ColumnDefinition colDef = {name, static_cast<bcsv::ColumnType>(type)};
        static_cast<bcsv::Layout*>(layout)->addColumn(colDef, index);
        return true;
    })())
}

void bcsv_layout_remove_column(bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_remove_column", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_remove_column", static_cast<bcsv::Layout*>(layout)->removeColumn(index))
}

void bcsv_layout_clear(bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_clear", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_clear", static_cast<bcsv::Layout*>(layout)->clear())
}

bool bcsv_layout_is_compatible(const_bcsv_layout_t layout1, const_bcsv_layout_t layout2) {
    if (null_handle("bcsv_layout_is_compatible", layout1) || null_handle("bcsv_layout_is_compatible", layout2)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_is_compatible", false, ([&]() {
        auto& l1 = *static_cast<const bcsv::Layout*>(layout1);
        auto& l2 = *static_cast<const bcsv::Layout*>(layout2);
        return l1.isCompatible(l2);
    })())
}

void bcsv_layout_assign(bcsv_layout_t dest, const_bcsv_layout_t src) {
    if (null_handle("bcsv_layout_assign", dest) || null_handle("bcsv_layout_assign", src)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_assign", ([&]() {
        auto& d = *static_cast<bcsv::Layout*>(dest);
        const auto& s = *static_cast<const bcsv::Layout*>(src);
        /* Use clear + addColumn to deep-copy (default operator= is shallow). */
        d.clear();
        for (size_t i = 0; i < s.columnCount(); ++i) {
            d.addColumn({s.columnName(i), s.columnType(i)}, i);
        }
    })())
}

size_t bcsv_layout_column_count_by_type(const_bcsv_layout_t layout, bcsv_type_t type) {
    if (null_handle("bcsv_layout_column_count_by_type", layout)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_count_by_type", 0u, ([&]() -> size_t {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        size_t count = 0;
        auto ct = static_cast<bcsv::ColumnType>(type);
        for (size_t i = 0; i < l->columnCount(); ++i) {
            if (l->columnType(i) == ct) ++count;
        }
        return count;
    })())
}

const char* bcsv_layout_to_string(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_to_string", layout)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_layout_to_string", "", ([&]() -> const char* {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        std::ostringstream oss;
        oss << *l;
        g_fmt_buf = oss.str();
        return g_fmt_buf.c_str();
    })())
}

// ============================================================================
// Reader API
// ============================================================================
bcsv_reader_t bcsv_reader_create(void) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_create", nullptr, reg_register(new bcsv::ReaderDirectAccess<bcsv::Layout>(), HandleKind::Reader))
}

void bcsv_reader_destroy(bcsv_reader_t reader) {
    if (!reader) return;
    if (!reg_take("bcsv_reader_destroy", reader)) [[unlikely]] return;
    columnar_erase(reader);
    BCSV_CAPI_TRY_VOID("bcsv_reader_destroy", delete static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader))
}

void bcsv_reader_close(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_close", reader)) return;
    if (!reg_contains("bcsv_reader_close", reader)) [[unlikely]] return;
    columnar_erase(reader);
    BCSV_CAPI_TRY_VOID("bcsv_reader_close", static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->close())
}

size_t bcsv_reader_count_rows(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_count_rows", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_count_rows", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowCount())
}

bool bcsv_reader_open(bcsv_reader_t reader, const char* filename) {
    if (null_handle("bcsv_reader_open", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->open(filename);
        if (ok) {
            clear_last_error();
        } else {
            g_last_error = r->getErrorMsg();
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_open", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_open");
        return false;
    }
}

bool bcsv_reader_is_open(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_is_open", reader)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_is_open", false, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_reader_filename(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const wchar_t*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#else
const char* bcsv_reader_filename(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const char*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#endif

const_bcsv_layout_t bcsv_reader_layout(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_layout", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

bool bcsv_reader_next(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_next", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->readNext();
        if (!ok) [[unlikely]] {
            const auto& msg = r->getErrorMsg();
            if (!msg.empty()) { g_has_error = true; g_last_error = msg; }
            // lean: no clear_last_error on success or empty-error EOF
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_next", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_next");
        return false;
    }
}

const_bcsv_row_t bcsv_reader_row(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_row", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_row", static_cast<const_bcsv_row_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto x = &(r->row());
        return reinterpret_cast<const_bcsv_row_t>(x);
    })())
}
size_t bcsv_reader_index(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_index", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_index", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowPos())
}

bool bcsv_reader_open_ex(bcsv_reader_t reader, const char* filename, bool rebuild_footer) {
    if (null_handle("bcsv_reader_open_ex", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->open(filename, rebuild_footer);
        if (ok) { clear_last_error(); } else { g_last_error = r->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_reader_open_ex", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_reader_open_ex"); return false; }
}

bool bcsv_reader_read(bcsv_reader_t reader, size_t index) {
    if (null_handle("bcsv_reader_read", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->read(index);
        if (!ok && !r->getErrorMsg().empty()) { g_last_error = r->getErrorMsg(); } else { clear_last_error(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_reader_read", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_reader_read"); return false; }
}

const char* bcsv_reader_error_msg(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_error_msg", reader)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_reader_error_msg", "", static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->getErrorMsg().c_str())
}

uint8_t bcsv_reader_compression_level(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_compression_level", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_compression_level", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->compressionLevel())
}

// ============================================================================
// Writer API
// ============================================================================
bcsv_writer_t bcsv_writer_create(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            reg_register(createWriterHandle(WriterHandle::Type::Flat,
                               // WriterFlat, not Writer<Layout>: the latter takes
                               // the default template argument, which is
                               // RowCodecDelta002.  Until 1.5.17 this function
                               // was documented as the flat writer, tagged its
                               // handle Type::Flat, and returned a delta writer —
                               // so "flat" was unreachable through the C API and
                               // through both C# bindings, which route it here.
                               new bcsv::WriterFlat<bcsv::Layout>(l)), HandleKind::Writer));
    })())
}

bcsv_writer_t bcsv_writer_create_zoh(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_zoh", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            reg_register(createWriterHandle(WriterHandle::Type::ZoH,
                               new bcsv::WriterZoH<bcsv::Layout>(l)), HandleKind::Writer));
    })())
}

bcsv_writer_t bcsv_writer_create_delta(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_delta", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            reg_register(createWriterHandle(WriterHandle::Type::Delta,
                               new bcsv::WriterDelta<bcsv::Layout>(l)), HandleKind::Writer));
    })())
}

void bcsv_writer_destroy(bcsv_writer_t writer) {
    if (!writer) return;
    if (!reg_take("bcsv_writer_destroy", writer)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_writer_destroy", ([&]() {
        auto* h = static_cast<WriterHandle*>(writer);
        h->delete_fn(h->ptr);
        delete h;
    })())
}

void bcsv_writer_close(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_close", writer)) return;
    if (!reg_contains("bcsv_writer_close", writer)) [[unlikely]] return;
    auto* h = static_cast<WriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_close", h->close_fn(h->ptr))
}

void bcsv_writer_flush(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_flush", writer)) return;
    if (!reg_contains("bcsv_writer_flush", writer)) [[unlikely]] return;
    auto* h = static_cast<WriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_flush", h->flush_fn(h->ptr))
}

bool bcsv_writer_open(bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags) {
    if (null_handle("bcsv_writer_open", writer)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        // Auto-set codec flags so the file header is correct
        if (h->type == WriterHandle::Type::ZoH) {
            flags = static_cast<bcsv_file_flags_t>(flags | BCSV_FLAG_ZOH);
        } else if (h->type == WriterHandle::Type::Delta) {
            flags = static_cast<bcsv_file_flags_t>(flags | BCSV_FLAG_DELTA_ENCODING);
        }
        bool ok = h->open_fn(h->ptr, filename, overwrite,
                             static_cast<size_t>(compress),
                             static_cast<size_t>(block_size_kb),
                             static_cast<bcsv::FileFlags>(flags));
        if (ok) {
            clear_last_error();
        } else {
            g_has_error = true;
            g_last_error = *h->errorMsg_fn(h->ptr);
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_open", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_open");
        return false;
    }
}

bool bcsv_writer_is_open(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_is_open", writer)) return false;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_is_open", false, h->isOpen_fn(h->ptr))
}

bool bcsv_writer_is_poisoned(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_is_poisoned", writer)) return false;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_is_poisoned", false, h->isPoisoned_fn(h->ptr))
}

#ifdef _WIN32
const wchar_t* bcsv_writer_filename(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const wchar_t*>(nullptr), h->filePath_fn(h->ptr))
}
#else
const char* bcsv_writer_filename(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const char*>(nullptr), h->filePath_fn(h->ptr))
}
#endif

const_bcsv_layout_t bcsv_writer_layout(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_layout", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_layout", static_cast<const_bcsv_layout_t>(nullptr),
        static_cast<const_bcsv_layout_t>(h->layout_fn(h->ptr)))
}

bool bcsv_writer_next(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_next", writer)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        h->writeRow_fn(h->ptr);   // single indirect call, no switch
        clear_last_error();       // fresh channel: a later failure is never stale
        return true;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_next", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_next");
        return false;
    }
}

bcsv_row_t bcsv_writer_row(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_row", writer)) return nullptr;
    return static_cast<bcsv_row_t>(static_cast<WriterHandle*>(writer)->cached_row);
}

size_t bcsv_writer_index(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_index", writer)) return 0u;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_index", 0u, h->rowCount_fn(h->ptr))
}

bool bcsv_writer_write(bcsv_writer_t writer, const_bcsv_row_t row) {
    if (null_handle("bcsv_writer_write", writer) || null_handle("bcsv_writer_write", row)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        h->write_fn(h->ptr, *static_cast<const bcsv::Row*>(row));
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_writer_write", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_writer_write"); return false; }
}

const char* bcsv_writer_error_msg(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_error_msg", writer)) return "";
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_error_msg", "", h->errorMsg_fn(h->ptr)->c_str())
}

uint8_t bcsv_writer_compression_level(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_compression_level", writer)) return 0u;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_compression_level", 0u, h->compressionLevel_fn(h->ptr))
}

int bcsv_writer_file_flags(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_file_flags", writer)) return 0;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_file_flags", 0, h->fileFlags_fn(h->ptr))
}

// ============================================================================
// CSV Reader API
// ============================================================================
bcsv_csv_reader_t bcsv_csv_reader_create(bcsv_layout_t layout, char delimiter, char decimal_sep) {
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_create", nullptr, ([&]() -> bcsv_csv_reader_t {
        bcsv::CsvReader<bcsv::Layout>* r = nullptr;
        if (layout) {
            r = new bcsv::CsvReader<bcsv::Layout>(
                *static_cast<bcsv::Layout*>(layout), delimiter, decimal_sep);
        } else {
            bcsv::Layout empty;
            r = new bcsv::CsvReader<bcsv::Layout>(empty, delimiter, decimal_sep);
        }
        return static_cast<bcsv_csv_reader_t>(reg_register(r, HandleKind::CsvReader));
    })())
}

void bcsv_csv_reader_destroy(bcsv_csv_reader_t reader) {
    if (!reader) return;
    if (!reg_take("bcsv_csv_reader_destroy", reader)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_reader_destroy", delete static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader))
}

bool bcsv_csv_reader_open(bcsv_csv_reader_t reader, const char* filename, bool has_header) {
    if (null_handle("bcsv_csv_reader_open", reader)) return false;
    try {
        auto* r = static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader);
        bool ok = r->open(filename, has_header);
        if (ok) { clear_last_error(); } else { g_last_error = r->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_reader_open", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_reader_open"); return false; }
}

void bcsv_csv_reader_close(bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_close", reader)) return;
    if (!reg_contains("bcsv_csv_reader_close", reader)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_reader_close", static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader)->close())
}

bool bcsv_csv_reader_is_open(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_is_open", reader)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_is_open", false, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_csv_reader_filename(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_filename", static_cast<const wchar_t*>(nullptr),
        static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->filePath().c_str())
}
#else
const char* bcsv_csv_reader_filename(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_filename", static_cast<const char*>(nullptr),
        static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->filePath().c_str())
}
#endif

const_bcsv_layout_t bcsv_csv_reader_layout(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_layout", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->layout()))
}

bool bcsv_csv_reader_next(bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_next", reader)) return false;
    try {
        auto* r = static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader);
        bool ok = r->readNext();
        if (!ok && !r->getErrorMsg().empty()) { g_last_error = r->getErrorMsg(); } else { clear_last_error(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_reader_next", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_reader_next"); return false; }
}

const_bcsv_row_t bcsv_csv_reader_row(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_row", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_row", static_cast<const_bcsv_row_t>(nullptr),
        reinterpret_cast<const_bcsv_row_t>(&static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->row()))
}

size_t bcsv_csv_reader_index(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_index", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_index", 0u, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->rowPos())
}

size_t bcsv_csv_reader_file_line(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_file_line", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_file_line", 0u, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->fileLine())
}

const char* bcsv_csv_reader_error_msg(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_error_msg", reader)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_error_msg", "", static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->getErrorMsg().c_str())
}

// ============================================================================
// CSV Writer API
// ============================================================================
bcsv_csv_writer_t bcsv_csv_writer_create(bcsv_layout_t layout, char delimiter, char decimal_sep) {
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_create", nullptr, ([&]() -> bcsv_csv_writer_t {
        bcsv::CsvWriter<bcsv::Layout>* w = nullptr;
        if (layout) {
            w = new bcsv::CsvWriter<bcsv::Layout>(
                *static_cast<bcsv::Layout*>(layout), delimiter, decimal_sep);
        } else {
            bcsv::Layout empty;
            w = new bcsv::CsvWriter<bcsv::Layout>(empty, delimiter, decimal_sep);
        }
        return static_cast<bcsv_csv_writer_t>(reg_register(w, HandleKind::CsvWriter));
    })())
}

void bcsv_csv_writer_destroy(bcsv_csv_writer_t writer) {
    if (!writer) return;
    if (!reg_take("bcsv_csv_writer_destroy", writer)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_writer_destroy", delete static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer))
}

bool bcsv_csv_writer_open(bcsv_csv_writer_t writer, const char* filename, bool overwrite, bool include_header) {
    if (null_handle("bcsv_csv_writer_open", writer)) return false;
    try {
        auto* w = static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer);
        bool ok = w->open(filename, overwrite, include_header);
        if (ok) { clear_last_error(); } else { g_last_error = w->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_open", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_open"); return false; }
}

void bcsv_csv_writer_close(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_close", writer)) return;
    if (!reg_contains("bcsv_csv_writer_close", writer)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_writer_close", static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->close())
}

bool bcsv_csv_writer_is_open(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_is_open", writer)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_is_open", false, static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_csv_writer_filename(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_filename", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_filename", static_cast<const wchar_t*>(nullptr),
        static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->filePath().c_str())
}
#else
const char* bcsv_csv_writer_filename(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_filename", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_filename", static_cast<const char*>(nullptr),
        static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->filePath().c_str())
}
#endif

const_bcsv_layout_t bcsv_csv_writer_layout(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_layout", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->layout()))
}

bool bcsv_csv_writer_next(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_next", writer)) return false;
    try {
        static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->writeRow();
        clear_last_error();
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_next", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_next"); return false; }
}

bool bcsv_csv_writer_write(bcsv_csv_writer_t writer, const_bcsv_row_t row) {
    if (null_handle("bcsv_csv_writer_write", writer) || null_handle("bcsv_csv_writer_write", row)) return false;
    try {
        static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->write(*static_cast<const bcsv::Row*>(row));
        clear_last_error();
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_write", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_write"); return false; }
}

bcsv_row_t bcsv_csv_writer_row(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_row", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_row", static_cast<bcsv_row_t>(nullptr),
        static_cast<bcsv_row_t>(&static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->row()))
}

size_t bcsv_csv_writer_index(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_index", writer)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_index", 0u, static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->rowCount())
}

const char* bcsv_csv_writer_error_msg(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_error_msg", writer)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_error_msg", "", static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->getErrorMsg().c_str())
}

// ============================================================================
// Row API
// ============================================================================

// Row lifecycle
bcsv_row_t bcsv_row_create(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_row_create", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_create", static_cast<bcsv_row_t>(nullptr), ([&]() {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        return static_cast<bcsv_row_t>(reg_register(new bcsv::Row(*l), HandleKind::Row));
    })())
}

bcsv_row_t bcsv_row_clone(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_clone", row)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_clone", static_cast<bcsv_row_t>(nullptr), static_cast<bcsv_row_t>(reg_register(new bcsv::Row(*static_cast<const bcsv::Row*>(row)), HandleKind::Row)))
}

void bcsv_row_destroy(bcsv_row_t row) {
    if (!row) return;
    // Borrowed row handles (bcsv_writer_row / bcsv_reader_row / csv*) are not
    // registered with the registry — a destroy attempt on one is refused here
    // instead of deleting memory owned by the reader/writer.
    if (!reg_take("bcsv_row_destroy", row)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_row_destroy", delete static_cast<bcsv::Row*>(row))
}

void bcsv_row_clear(bcsv_row_t row) {
    if (null_handle("bcsv_row_clear", row)) return;
    BCSV_CAPI_TRY_VOID("bcsv_row_clear", static_cast<bcsv::Row*>(row)->clear())
}

void bcsv_row_assign(bcsv_row_t dest, const_bcsv_row_t src) {
    if (null_handle("bcsv_row_assign", dest) || null_handle("bcsv_row_assign", src)) return;
    BCSV_CAPI_TRY_VOID("bcsv_row_assign", ([&]() {
        auto& d = *static_cast<bcsv::Row*>(dest);
        auto& s = *static_cast<const bcsv::Row*>(src);
        d = s;
    })())
}

const_bcsv_layout_t bcsv_row_layout(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_layout", row)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::Row*>(row);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

// Scalar getters: dispatch on the column type once per call and only ever use
// the matching typed accessor (strict) — except bcsv_row_get_double, which
// widens losslessly (docs/adr/0006). Mismatch = error via the thread-local
// error channel + fallback value, never a silent 0 with a stale error string.
bool bcsv_row_get_bool(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(false, row_cell_value<bool>(static_cast<const bcsv::Row*>(row), col, __func__))
}
uint8_t bcsv_row_get_uint8(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint8_t{0}, row_cell_value<uint8_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
uint16_t bcsv_row_get_uint16(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint16_t{0}, row_cell_value<uint16_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
uint32_t bcsv_row_get_uint32(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint32_t{0}, row_cell_value<uint32_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
uint64_t bcsv_row_get_uint64(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint64_t{0}, row_cell_value<uint64_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
int8_t bcsv_row_get_int8(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int8_t{0}, row_cell_value<int8_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
int16_t bcsv_row_get_int16(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int16_t{0}, row_cell_value<int16_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
int32_t bcsv_row_get_int32(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int32_t{0}, row_cell_value<int32_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
int64_t bcsv_row_get_int64(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int64_t{0}, row_cell_value<int64_t>(static_cast<const bcsv::Row*>(row), col, __func__))
}
float bcsv_row_get_float(const_bcsv_row_t row, int col) {
    // Strict: a DOUBLE column is NOT narrowed to float (the result would
    // depend on the value — inf for |x| > FLT_MAX). Use get_double instead;
    // narrowing is a decision for the caller, made once in the binding.
    BCSV_ROW_GET(0.0f, row_cell_value<float>(static_cast<const bcsv::Row*>(row), col, __func__))
}
double bcsv_row_get_double(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(0.0, row_cell_to_double(static_cast<const bcsv::Row*>(row), col, __func__))
}
const char* bcsv_row_get_string(const_bcsv_row_t row, int col) {
    // Dispatch guards the STRING case explicitly: without this, a non-string
    // column index would be used to index the string store (a byte offset,
    // never a strg_ index) in RANGE_CHECKING=false builds.
    BCSV_ROW_GET(static_cast<const char*>(nullptr), row_cell_string(static_cast<const bcsv::Row*>(row), col, __func__))
}

// Checked twins of the getters above: report success/failure directly (bool
// return + out parameter) instead of a fallback value plus the thread-local
// error channel. Each twin applies exactly the type rule of its plain getter:
// strict for every type, except double — which accepts the same lossless
// widening set (bool/int8..int32/uint8..uint32/float; int64/uint64/string
// rejected as inexact or non-convertible).
// BCSV_ROW_TRY_GET(call): call may assign *out; any exception (bad index,
// type mismatch) is reported as false with a fresh error string; *out is then
// untouched. A NULL out is refused rather than trusted across the FFI.
#define BCSV_ROW_TRY_GET(call) \
    if (!out) [[unlikely]] { \
        g_has_error = true; \
        g_last_error = std::string(__func__) + ": out is NULL"; \
        return false; \
    } \
    clear_last_error(); \
    try { \
        call; \
        return true; \
    } catch (const std::exception& ex) { \
        set_last_error(__func__, ex); \
        return false; \
    } catch (...) { \
        set_last_error_unknown(__func__); \
        return false; \
    }

bool bcsv_row_try_get_bool(const_bcsv_row_t row, int col, bool* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_uint8(const_bcsv_row_t row, int col, uint8_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_uint16(const_bcsv_row_t row, int col, uint16_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_uint32(const_bcsv_row_t row, int col, uint32_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_uint64(const_bcsv_row_t row, int col, uint64_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_int8(const_bcsv_row_t row, int col, int8_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_int16(const_bcsv_row_t row, int col, int16_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_int32(const_bcsv_row_t row, int col, int32_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_int64(const_bcsv_row_t row, int col, int64_t* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_float(const_bcsv_row_t row, int col, float* out) {
    BCSV_ROW_TRY_GET(row_cell_strict(static_cast<const bcsv::Row*>(row), col, __func__, out))
}
bool bcsv_row_try_get_double(const_bcsv_row_t row, int col, double* out) {
    // Same lossless-widening set as bcsv_row_get_double — the twins must not
    // disagree on what a type accepts, and the managed bindings read the
    // widened rule through this entry point in one call.
    BCSV_ROW_TRY_GET(*out = row_cell_to_double(static_cast<const bcsv::Row*>(row), col, __func__))
}
bool bcsv_row_try_get_string(const_bcsv_row_t row, int col, const char** out) {
    BCSV_ROW_TRY_GET(*out = row_cell_string(static_cast<const bcsv::Row*>(row), col, __func__))
}

void bcsv_row_set_bool(bcsv_row_t row, int col, bool value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint8(bcsv_row_t row, int col, uint8_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint16(bcsv_row_t row, int col, uint16_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint32(bcsv_row_t row, int col, uint32_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint64(bcsv_row_t row, int col, uint64_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int8(bcsv_row_t row, int col, int8_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int16(bcsv_row_t row, int col, int16_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int32(bcsv_row_t row, int col, int32_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int64(bcsv_row_t row, int col, int64_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_float(bcsv_row_t row, int col, float value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_double(bcsv_row_t row, int col, double value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_string(bcsv_row_t row, int col, const char* value) {
    if (!value) [[unlikely]] { g_has_error = true; g_last_error = "bcsv_row_set_string: value is NULL"; return; }
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, std::string(value)))
}

// Vectorized get functions
void bcsv_row_get_bool_array(const_bcsv_row_t row, int start_col, bool* dst, size_t count) {
    auto s = std::span<bool>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<bool>(start_col, s))
}
void bcsv_row_get_uint8_array(const_bcsv_row_t row, int start_col, uint8_t* dst, size_t count) {
    auto s = std::span<uint8_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint8_t>(start_col, s))
}
void bcsv_row_get_uint16_array(const_bcsv_row_t row, int start_col, uint16_t* dst, size_t count) {
    auto s = std::span<uint16_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint16_t>(start_col, s))
}
void bcsv_row_get_uint32_array(const_bcsv_row_t row, int start_col, uint32_t* dst, size_t count) {
    auto s = std::span<uint32_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint32_t>(start_col, s))
}
void bcsv_row_get_uint64_array(const_bcsv_row_t row, int start_col, uint64_t* dst, size_t count) {
    auto s = std::span<uint64_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint64_t>(start_col, s))
}
void bcsv_row_get_int8_array(const_bcsv_row_t row, int start_col, int8_t* dst, size_t count) {
    auto s = std::span<int8_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int8_t>(start_col, s))
}
void bcsv_row_get_int16_array(const_bcsv_row_t row, int start_col, int16_t* dst, size_t count) {
    auto s = std::span<int16_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int16_t>(start_col, s))
}
void bcsv_row_get_int32_array(const_bcsv_row_t row, int start_col, int32_t* dst, size_t count) {
    auto s = std::span<int32_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int32_t>(start_col, s))
}
void bcsv_row_get_int64_array(const_bcsv_row_t row, int start_col, int64_t* dst, size_t count) {
    auto s = std::span<int64_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int64_t>(start_col, s))
}
void bcsv_row_get_float_array(const_bcsv_row_t row, int start_col, float* dst, size_t count) {
    auto s = std::span<float>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<float>(start_col, s))
}
void bcsv_row_get_double_array(const_bcsv_row_t row, int start_col, double* dst, size_t count) {
    auto s = std::span<double>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<double>(start_col, s))
}

// Vectorized set functions
void bcsv_row_set_bool_array(bcsv_row_t row, int start_col, const bool* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<bool>(start_col, std::span<const bool>(src, count)))
}
void bcsv_row_set_uint8_array(bcsv_row_t row, int start_col, const uint8_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint8_t>(start_col, std::span<const uint8_t>(src, count)))
}
void bcsv_row_set_uint16_array(bcsv_row_t row, int start_col, const uint16_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint16_t>(start_col, std::span<const uint16_t>(src, count)))
}
void bcsv_row_set_uint32_array(bcsv_row_t row, int start_col, const uint32_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint32_t>(start_col, std::span<const uint32_t>(src, count)))
}
void bcsv_row_set_uint64_array(bcsv_row_t row, int start_col, const uint64_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint64_t>(start_col, std::span<const uint64_t>(src, count)))
}
void bcsv_row_set_int8_array(bcsv_row_t row, int start_col, const int8_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int8_t>(start_col, std::span<const int8_t>(src, count)))
}
void bcsv_row_set_int16_array(bcsv_row_t row, int start_col, const int16_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int16_t>(start_col, std::span<const int16_t>(src, count)))
}
void bcsv_row_set_int32_array(bcsv_row_t row, int start_col, const int32_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int32_t>(start_col, std::span<const int32_t>(src, count)))
}
void bcsv_row_set_int64_array(bcsv_row_t row, int start_col, const int64_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int64_t>(start_col, std::span<const int64_t>(src, count)))
}
void bcsv_row_set_float_array(bcsv_row_t row, int start_col, const float* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<float>(start_col, std::span<const float>(src, count)))
}
void bcsv_row_set_double_array(bcsv_row_t row, int start_col, const double* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<double>(start_col, std::span<const double>(src, count)))
}

const char* bcsv_row_to_string(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_to_string", row)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_row_to_string", "", ([&]() -> const char* {
        const auto* r = static_cast<const bcsv::Row*>(row);
        std::ostringstream oss;
        oss << *r;
        g_fmt_buf = oss.str();
        return g_fmt_buf.c_str();
    })())
}

size_t bcsv_row_column_count(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_column_count", row)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_row_column_count", 0u, static_cast<const bcsv::Row*>(row)->layout().columnCount())
}

// ============================================================================
// Row Visit API
// ============================================================================
void bcsv_row_visit_const(const_bcsv_row_t row, size_t start_col, size_t count,
                           bcsv_visit_callback_t cb, void* user_data) {
    if (null_handle("bcsv_row_visit_const", row) || !cb) return;
    try {
        const auto* r = static_cast<const bcsv::Row*>(row);
        const auto& layout = r->layout();
        const size_t end = start_col + count;
        if (end > layout.columnCount()) {
            g_has_error = true;
            g_last_error = "bcsv_row_visit_const: column range out of bounds";
            return;
        }
        r->visitConst(start_col, [&](size_t col, auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                bool v = val;
                cb(col, BCSV_TYPE_BOOL, &v, user_data);
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                cb(col, BCSV_TYPE_UINT8, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint16_t>) {
                cb(col, BCSV_TYPE_UINT16, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                cb(col, BCSV_TYPE_UINT32, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                cb(col, BCSV_TYPE_UINT64, &val, user_data);
            } else if constexpr (std::is_same_v<T, int8_t>) {
                cb(col, BCSV_TYPE_INT8, &val, user_data);
            } else if constexpr (std::is_same_v<T, int16_t>) {
                cb(col, BCSV_TYPE_INT16, &val, user_data);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                cb(col, BCSV_TYPE_INT32, &val, user_data);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                cb(col, BCSV_TYPE_INT64, &val, user_data);
            } else if constexpr (std::is_same_v<T, float>) {
                cb(col, BCSV_TYPE_FLOAT, &val, user_data);
            } else if constexpr (std::is_same_v<T, double>) {
                cb(col, BCSV_TYPE_DOUBLE, &val, user_data);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const char* cstr = val.c_str();
                cb(col, BCSV_TYPE_STRING, cstr, user_data);
            }
        }, count);
    } catch (const std::exception& ex) {
        set_last_error("bcsv_row_visit_const", ex);
    } catch (...) {
        set_last_error_unknown("bcsv_row_visit_const");
    }
}

// ============================================================================
// Sampler API
// ============================================================================
bcsv_sampler_t bcsv_sampler_create(bcsv_reader_t reader) {
    if (null_handle("bcsv_sampler_create", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_create", nullptr, ([&]() -> bcsv_sampler_t {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto* h = new SamplerHandle();
        h->sampler = new bcsv::Sampler<bcsv::Layout>(*r);
        return static_cast<bcsv_sampler_t>(reg_register(h, HandleKind::Sampler));
    })())
}

void bcsv_sampler_destroy(bcsv_sampler_t sampler) {
    if (!sampler) return;
    if (!reg_take("bcsv_sampler_destroy", sampler)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_sampler_destroy", ([&]() {
        auto* h = static_cast<SamplerHandle*>(sampler);
        delete h->sampler;
        delete h;
    })())
}

bool bcsv_sampler_set_conditional(bcsv_sampler_t sampler, const char* expr) {
    if (null_handle("bcsv_sampler_set_conditional", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        auto result = h->sampler->setConditional(expr ? expr : "");
        if (result.success) {
            h->error_msg.clear();
            clear_last_error();
            return true;
        } else {
            h->error_msg = result.error_msg;
            g_has_error = true;
            g_last_error = "bcsv_sampler_set_conditional: " + result.error_msg;
            return false;
        }
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_set_conditional", ex);
        auto* h = static_cast<SamplerHandle*>(sampler);
        h->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_set_conditional");
        return false;
    }
}

bool bcsv_sampler_set_selection(bcsv_sampler_t sampler, const char* expr) {
    if (null_handle("bcsv_sampler_set_selection", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        auto result = h->sampler->setSelection(expr ? expr : "");
        if (result.success) {
            h->error_msg.clear();
            clear_last_error();
            return true;
        } else {
            h->error_msg = result.error_msg;
            g_has_error = true;
            g_last_error = "bcsv_sampler_set_selection: " + result.error_msg;
            return false;
        }
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_set_selection", ex);
        auto* h = static_cast<SamplerHandle*>(sampler);
        h->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_set_selection");
        return false;
    }
}

const char* bcsv_sampler_get_conditional(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_conditional", sampler)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_conditional", "", static_cast<const SamplerHandle*>(sampler)->sampler->getConditional().c_str())
}

const char* bcsv_sampler_get_selection(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_selection", sampler)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_selection", "", static_cast<const SamplerHandle*>(sampler)->sampler->getSelection().c_str())
}

void bcsv_sampler_set_mode(bcsv_sampler_t sampler, bcsv_sampler_mode_t mode) {
    if (null_handle("bcsv_sampler_set_mode", sampler)) return;
    BCSV_CAPI_TRY_VOID("bcsv_sampler_set_mode",
        static_cast<SamplerHandle*>(sampler)->sampler->setMode(static_cast<bcsv::SamplerMode>(mode)))
}

bcsv_sampler_mode_t bcsv_sampler_get_mode(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_mode", sampler)) return BCSV_SAMPLER_TRUNCATE;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_mode", BCSV_SAMPLER_TRUNCATE,
        static_cast<bcsv_sampler_mode_t>(static_cast<const SamplerHandle*>(sampler)->sampler->getMode()))
}

bool bcsv_sampler_next(bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_next", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        return h->sampler->next();
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_next", ex);
        static_cast<SamplerHandle*>(sampler)->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_next");
        return false;
    }
}

const_bcsv_row_t bcsv_sampler_row(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_row", sampler)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_row", static_cast<const_bcsv_row_t>(nullptr),
        reinterpret_cast<const_bcsv_row_t>(&static_cast<const SamplerHandle*>(sampler)->sampler->row()))
}

const_bcsv_layout_t bcsv_sampler_output_layout(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_output_layout", sampler)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_output_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const SamplerHandle*>(sampler)->sampler->outputLayout()))
}

size_t bcsv_sampler_source_row_pos(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_source_row_pos", sampler)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_source_row_pos", 0u,
        static_cast<const SamplerHandle*>(sampler)->sampler->sourceRowPos())
}

const char* bcsv_sampler_error_msg(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_error_msg", sampler)) return "";
    return static_cast<const SamplerHandle*>(sampler)->error_msg.c_str();
}

// ============================================================================
// Error API
// ============================================================================
const char* bcsv_last_error() {
    return g_has_error ? g_last_error.c_str() : "";
}

void bcsv_clear_last_error() {
    g_has_error = false;
}

} // extern "C" (main)

// ============================================================================
// Columnar Bulk I/O — helpers (outside extern "C", internal linkage)
// ============================================================================

namespace {

// Helper: fill one cell from row into a column-oriented typed buffer at offset row_idx.
inline void fill_column_cell(const bcsv::Row& row, size_t col,
                             bcsv::ColumnType type, void* buf, size_t row_idx) {
    switch (type) {
    case bcsv::ColumnType::BOOL:   static_cast<bool*>(buf)[row_idx]     = row.get<bool>(col);     break;
    case bcsv::ColumnType::UINT8:  static_cast<uint8_t*>(buf)[row_idx]  = row.get<uint8_t>(col);  break;
    case bcsv::ColumnType::UINT16: static_cast<uint16_t*>(buf)[row_idx] = row.get<uint16_t>(col); break;
    case bcsv::ColumnType::UINT32: static_cast<uint32_t*>(buf)[row_idx] = row.get<uint32_t>(col); break;
    case bcsv::ColumnType::UINT64: static_cast<uint64_t*>(buf)[row_idx] = row.get<uint64_t>(col); break;
    case bcsv::ColumnType::INT8:   static_cast<int8_t*>(buf)[row_idx]   = row.get<int8_t>(col);   break;
    case bcsv::ColumnType::INT16:  static_cast<int16_t*>(buf)[row_idx]  = row.get<int16_t>(col);  break;
    case bcsv::ColumnType::INT32:  static_cast<int32_t*>(buf)[row_idx]  = row.get<int32_t>(col);  break;
    case bcsv::ColumnType::INT64:  static_cast<int64_t*>(buf)[row_idx]  = row.get<int64_t>(col);  break;
    case bcsv::ColumnType::FLOAT:  static_cast<float*>(buf)[row_idx]    = row.get<float>(col);    break;
    case bcsv::ColumnType::DOUBLE: static_cast<double*>(buf)[row_idx]   = row.get<double>(col);   break;
    default: break; // STRING handled separately
    }
}

// Helper: fill one cell from column-oriented typed buffer at row_idx into a row.
inline void fill_row_cell(bcsv::Row& row, size_t col,
                          bcsv::ColumnType type, const void* buf, size_t row_idx) {
    switch (type) {
    case bcsv::ColumnType::BOOL:   row.set(col, static_cast<const bool*>(buf)[row_idx]);     break;
    case bcsv::ColumnType::UINT8:  row.set(col, static_cast<const uint8_t*>(buf)[row_idx]);  break;
    case bcsv::ColumnType::UINT16: row.set(col, static_cast<const uint16_t*>(buf)[row_idx]); break;
    case bcsv::ColumnType::UINT32: row.set(col, static_cast<const uint32_t*>(buf)[row_idx]); break;
    case bcsv::ColumnType::UINT64: row.set(col, static_cast<const uint64_t*>(buf)[row_idx]); break;
    case bcsv::ColumnType::INT8:   row.set(col, static_cast<const int8_t*>(buf)[row_idx]);   break;
    case bcsv::ColumnType::INT16:  row.set(col, static_cast<const int16_t*>(buf)[row_idx]);  break;
    case bcsv::ColumnType::INT32:  row.set(col, static_cast<const int32_t*>(buf)[row_idx]);  break;
    case bcsv::ColumnType::INT64:  row.set(col, static_cast<const int64_t*>(buf)[row_idx]);  break;
    case bcsv::ColumnType::FLOAT:  row.set(col, static_cast<const float*>(buf)[row_idx]);    break;
    case bcsv::ColumnType::DOUBLE: row.set(col, static_cast<const double*>(buf)[row_idx]);   break;
    default: break; // STRING handled separately
    }
}

} // namespace

extern "C" {

// ============================================================================
// Columnar Bulk I/O API
// ============================================================================

size_t bcsv_reader_read_columns(bcsv_reader_t reader, void** bufs,
                                size_t num_cols, size_t max_rows) {
    if (null_handle("bcsv_reader_read_columns", reader)) return 0;
    try {
        clear_last_error();
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& layout = r->layout();
        const size_t actual_cols = layout.columnCount();
        if (num_cols != actual_cols) {
            g_has_error = true;
            g_last_error = "bcsv_reader_read_columns: num_cols (" +
                           std::to_string(num_cols) + ") != layout column count (" +
                           std::to_string(actual_cols) + ")";
            return 0;
        }

        // Cache column types and detect string columns
        std::vector<bcsv::ColumnType> col_types(num_cols);
        std::vector<bool> is_string(num_cols, false);
        for (size_t c = 0; c < num_cols; ++c) {
            col_types[c] = layout.columnType(c);
            is_string[c] = (col_types[c] == bcsv::ColumnType::STRING);
        }

        // Prepare columnar string storage
        auto& state = [&]() -> ColumnarReadState& {
            auto& t = columnar_table();
            std::lock_guard<std::mutex> lk(t.mtx);
            return t.by_reader[reader];
        }();
        state.resize(num_cols);
        state.clear();
        for (size_t c = 0; c < num_cols; ++c) {
            if (is_string[c]) state.string_cols[c].reserve(max_rows > 1024 ? 1024 : max_rows);
        }

        // Read loop — hot path
        size_t row_idx = 0;
        while (row_idx < max_rows && r->readNext()) {
            const auto& row = r->row();
            for (size_t c = 0; c < num_cols; ++c) {
                if (is_string[c]) {
                    state.string_cols[c].emplace_back(row.get<std::string>(c));
                } else {
                    fill_column_cell(row, c, col_types[c], bufs[c], row_idx);
                }
            }
            ++row_idx;
        }
        return row_idx;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_read_columns", ex);
        return 0;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_read_columns");
        return 0;
    }
}

const char* bcsv_reader_column_string(bcsv_reader_t reader, size_t col, size_t row) {
    const auto* state = columnar_find(reader);
    if (!state) return "";
    if (col >= state->string_cols.size()) return "";
    if (row >= state->string_cols[col].size()) return "";
    return state->string_cols[col][row].c_str();
}

size_t bcsv_reader_column_string_count(bcsv_reader_t reader, size_t col) {
    const auto* state = columnar_find(reader);
    if (!state) return 0;
    if (col >= state->string_cols.size()) return 0;
    return state->string_cols[col].size();
}

size_t bcsv_reader_column_strings_packed(bcsv_reader_t reader, size_t col,
                                          char* out_buf, size_t buf_size) {
    const auto* state = columnar_find(reader);
    if (!state) return 0;
    if (col >= state->string_cols.size()) return 0;
    const auto& strings = state->string_cols[col];

    // Calculate total size needed (each string null-terminated)
    size_t total = 0;
    for (const auto& s : strings)
        total += s.size() + 1;

    if (out_buf == nullptr || buf_size == 0)
        return total;

    // Fill buffer with concatenated null-terminated strings
    size_t offset = 0;
    for (const auto& s : strings) {
        size_t needed = s.size() + 1;
        if (offset + needed > buf_size) break;
        std::memcpy(out_buf + offset, s.data(), s.size());
        out_buf[offset + s.size()] = '\0';
        offset += needed;
    }
    return offset;
}

bool bcsv_writer_write_columns(bcsv_writer_t writer, const void** bufs,
                               size_t num_cols, size_t num_rows) {
    if (null_handle("bcsv_writer_write_columns", writer)) return false;
    try {
        clear_last_error();
        auto* h = static_cast<WriterHandle*>(writer);
        const auto* layout = h->layout_fn(h->ptr);
        const size_t actual_cols = layout->columnCount();
        if (num_cols != actual_cols) {
            g_has_error = true;
            g_last_error = "bcsv_writer_write_columns: num_cols (" +
                           std::to_string(num_cols) + ") != layout column count (" +
                           std::to_string(actual_cols) + ")";
            return false;
        }

        // Cache column types
        std::vector<bcsv::ColumnType> col_types(num_cols);
        std::vector<bool> is_string(num_cols, false);
        for (size_t c = 0; c < num_cols; ++c) {
            col_types[c] = layout->columnType(c);
            is_string[c] = (col_types[c] == bcsv::ColumnType::STRING);
        }

        // Write loop
        auto* row = h->cached_row;
        for (size_t r = 0; r < num_rows; ++r) {
            for (size_t c = 0; c < num_cols; ++c) {
                if (is_string[c]) {
                    const char* const* str_array = static_cast<const char* const*>(bufs[c]);
                    row->set(c, std::string(str_array[r] ? str_array[r] : ""));
                } else {
                    fill_row_cell(*row, c, col_types[c], bufs[c], r);
                }
            }
            h->writeRow_fn(h->ptr);
        }
        return true;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_write_columns", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_write_columns");
        return false;
    }
}

size_t bcsv_layout_row_data_size(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_row_data_size", layout)) return 0;
    try {
        clear_last_error();
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        size_t total = 0;
        for (size_t c = 0; c < l->columnCount(); ++c) {
            auto t = l->columnType(c);
            if (t != bcsv::ColumnType::STRING) {
                total += bcsv::sizeOf(t);
            }
        }
        return total;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_layout_row_data_size", ex);
        return 0;
    } catch (...) {
        set_last_error_unknown("bcsv_layout_row_data_size");
        return 0;
    }
}

int bcsv_reader_file_flags(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_file_flags", reader)) return 0;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_file_flags", 0,
        static_cast<int>(static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->fileFlags()))
}

} // extern "C" (columnar)
