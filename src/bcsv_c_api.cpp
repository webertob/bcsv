/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include "bcsv/bcsv_c_api.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <exception>
#include <mutex>
#include <type_traits>
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
enum class HandleKind : uint8_t { Row, Sampler, Reader, CsvReader, CsvWriter, Writer, Layout,
                                  UnifiedReader, UnifiedWriter };

struct RegEntry {
    HandleKind kind;
    bool       owned;   // false = borrowed member of a unified container, exposed
                        // via *_native_u — destroyable by nobody but its container
};

struct HandleRegistry {
    std::mutex mtx;
    std::unordered_map<void*, RegEntry> live;
};

inline HandleRegistry& registry() {
    // Leaky singleton, thread-safe magic static.
    static HandleRegistry* r = new HandleRegistry();
    return *r;
}

void* reg_register(void* h, HandleKind k) {
    auto& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.live.emplace(h, RegEntry{k, true});
    return h;
}

/// Track a live unified member under its legacy kind (ADR-0009 escape hatch):
/// bcsv_sampler_create() and friends must treat the borrowed pointer as the
/// legacy object it is, while destroy guards still refuse to delete it.
void reg_register_borrowed(void* h, HandleKind k) {
    auto& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.live.insert_or_assign(h, RegEntry{k, false});
}

/// Drop a borrowed-member registration (close_u/destroy_u). No error when
/// absent (never registered, or claimed wholesale by bcsv_shutdown()).
void reg_forget(void* h) {
    auto& reg = registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.live.erase(h);
}

/// destroy() guard: claim-or-report in one lock, WITH FAMILY CHECK (ADR-0009).
/// Returns true when the caller now owns @p h for destruction; false = logged
/// no-op (double destroy, borrowed handle such as bcsv_writer_row() or an
/// *_native_u member, foreign pointer, or a handle of a DIFFERENT call family
/// — a unified _u handle through a legacy name poisons nothing and frees
/// nothing). A mutex failure is reported, not propagated: extern "C".
bool reg_take(const char* where, void* h, HandleKind expected) {
    try {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        auto it = reg.live.find(h);
        if (it != reg.live.end()) {
            if (it->second.kind != expected) {
                g_has_error = true;
                g_last_error = std::string(where) + ": handle belongs to another call family (destroy ignored)";
                return false;
            }
            if (!it->second.owned) {
                g_has_error = true;
                g_last_error = std::string(where) + ": borrowed member handle, owned by its unified container (destroy ignored)";
                return false;
            }
            reg.live.erase(it);
            return true;
        }
    } catch (...) {
        g_has_error = true;
        g_last_error = std::string(where) + ": handle registry unavailable (destroy ignored)";
        return false;
    }
    g_has_error = true;
    g_last_error = std::string(where) + ": handle already destroyed or not owned by this API (destroy ignored)";
    return false;
}

/// close/flush guard: is @p h a live registered handle of the expected family?
/// A handle freed by bcsv_shutdown() (or an earlier destroy) must never be
/// touched again — a GC finalizer running Dispose (close-before-destroy) after
/// shutdown would otherwise call through freed memory, the same abort class
/// this registry exists to prevent. Borrowed members pass (both close paths
/// are idempotent through isOpen()); wrong-family handles are refused.
/// Find-without-erase: the handle stays live for its owner.
bool reg_contains(const char* where, void* h, HandleKind expected) {
    try {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        auto it = reg.live.find(h);
        if (it != reg.live.end() && it->second.kind == expected) return true;
    } catch (...) {
        g_has_error = true;
        g_last_error = std::string(where) + ": handle registry unavailable (call ignored)";
        return false;
    }
    g_has_error = true;
    g_last_error = std::string(where) + ": handle already destroyed, from another call family, or not owned by this API (call ignored)";
    return false;
}

/// Cold-path type guard (sampler create): @p h must currently be a handle of
/// @p expected kind — owned or borrowed. Replaces the pre-ADR-0009 blind cast
/// that made passing any wrong pointer undefined behavior.
bool reg_is_a(const char* where, void* h, HandleKind expected) {
    try {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        auto it = reg.live.find(h);
        if (it != reg.live.end() && it->second.kind == expected) return true;
    } catch (...) {
        g_has_error = true;
        g_last_error = std::string(where) + ": handle registry unavailable";
        return false;
    }
    g_has_error = true;
    g_last_error = std::string(where) + ": not a live matching reader handle (for a unified reader pass bcsv_reader_native_u())";
    return false;
}

// ---- Unified format-flexible containers (ADR-0009) --------------------------
// One _u handle hosts a .bcsv member OR a CSV/.lz4 member, selected and
// constructed at open time from content magic (AUTO) or the pinned format.
// No C++ class is modified: the container just owns whichever concrete
// object the open resolved to, plus the last error. Borrowed-member
// registration under the member's legacy kind (reg_register_borrowed) lets
// the *_native_u escape hatch feed the deep legacy APIs (sampler,
// read_columns) with a correctly-kicked registry entry, while destroy
// guards still refuse to free a member anyone but the container owns.

/// Content classification by magic only — never trusts the name (ADR-0008
/// rule, extended across all three formats by ADR-0009). ok=false means the
/// path is not an openable regular file. Short/empty files classify as CSV
/// (they can only parse as text; the CSV reader reports "empty header" too).
bcsv_format_t sniff_file_format(const char* filename, bool& ok) {
    ok = false;
    std::error_code ec;
    if (!filename || !std::filesystem::is_regular_file(filename, ec) || ec) return BCSV_FORMAT_AUTO;
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in.is_open()) return BCSV_FORMAT_AUTO;
    char head[4] = {0, 0, 0, 0};
    in.read(head, 4);
    const size_t got = static_cast<size_t>(in.gcount());
    ok = true;
    if (got >= 4 && std::memcmp(head, bcsv::MAGIC_BYTES_BCSV, 4) == 0) return BCSV_FORMAT_BCSV;
    if (bcsv::lz4_frame_sniff(std::string_view(head, got))) return BCSV_FORMAT_CSV_LZ4;
    return BCSV_FORMAT_CSV;
}

std::string bcsv_format_name(bcsv_format_t f) {
    switch (f) {
        case BCSV_FORMAT_BCSV:     return "BCSV";
        case BCSV_FORMAT_CSV:      return "CSV";
        case BCSV_FORMAT_CSV_LZ4:  return "CSV_LZ4";
        default:                   return "AUTO";
    }
}

/// Pinned-format content check (reader): the asked format must match what
/// the bytes are, or the open fails loudly — silent reinterpretation was the
/// hazard ADR-0009 closes (e.g. a .bcsv fed to the CSV reader with
/// has_header=false parsed as garbage rows). Returns "" on accept, else a
/// reader-ready error message.
std::string pinned_format_error(bcsv_format_t pinned, bcsv_format_t found) {
    if (pinned == found) return "";
    // CSV and CSV_LZ4 share the text member; the CsvReader self-sniffs LZ4,
    // so pinned-CSV onto a CSV_LZ4 file silently DECOMPRESSES — wrong for a
    // pinned caller who meant raw text, and wrong the other way around.
    if (pinned == BCSV_FORMAT_CSV && found == BCSV_FORMAT_CSV_LZ4)
        return "Error: file carries an LZ4 frame magic — pinned CSV refuses it; use BCSV_FORMAT_CSV_LZ4";
    if (pinned == BCSV_FORMAT_CSV_LZ4 && found == BCSV_FORMAT_CSV)
        return "Error: file has no LZ4 frame magic — pinned CSV_LZ4 refuses it; use BCSV_FORMAT_CSV";
    return "Error: pinned format " + bcsv_format_name(pinned) +
           " does not match file content (" + bcsv_format_name(found) + ")";
}

/// Resolve a reader open format: AUTO = classify content; pinned = verify.
/// On refusal sets g_error_out and returns false.
bool resolve_reader_format(bcsv_format_t fmt, const std::string& path,
                           bcsv_format_t& resolved, std::string& err) {
    bool ok = false;
    const bcsv_format_t found = sniff_file_format(path.c_str(), ok);
    if (!ok) {
        err = "Error: cannot probe file: " + path;   // let open() produce the real IO error later
        // Still try the open — permission/missing-file messages are better there.
        resolved = (fmt == BCSV_FORMAT_AUTO) ? found : fmt;
        return true;
    }
    if (fmt == BCSV_FORMAT_AUTO) { resolved = found; return true; }
    err = pinned_format_error(fmt, found);
    return err.empty();
}

std::string opts_error(const char* field, const char* why) {
    return std::string("Error: bcsv_open_opts.") + field + " " + why;
}

// Tri-state int decoding: -1 (unset) -> default; otherwise the given value.
inline int tri(int v, int def) { return v == -1 ? def : v; }
inline char chr(char c, char def) { return c == 0 ? def : c; }

// ---- bcsv_open_opts forward-extension rule (ADR-0009) ----------------------
// The rule, fixed at the first release that ships it — it can never change:
//   accept   BCSV_OPEN_OPTS_V1_SIZE <= opts->struct_size <= sizeof(bcsv_open_opts)
//   copy     the first opts->struct_size bytes onto an INIT-initialized
//            instance → fields the caller's (older, smaller) struct does not
//            carry stay unset (-1 / NULL) and take the documented default.
// BCSV_OPEN_OPTS_V1_SIZE = byte offset just past the LAST field of the
// original 1.5.x struct (lz4_level). Today it equals sizeof(bcsv_open_opts);
// when a future version APPENDS fields, sizeof grows and old caller structs
// keep working, while a caller from a NEWER build (struct_size > this build's
// sizeof) is refused: this build cannot know what the extra tail bytes mean.
constexpr size_t BCSV_OPEN_OPTS_V1_SIZE =
    offsetof(bcsv_open_opts, lz4_level) + sizeof(((bcsv_open_opts*)0)->lz4_level);
// Anchors the append-only rule mechanically: a renamed trailing field fails
// the offsetof above (compile error), and appending must not silently move
// the v1 floor off the struct end — if the second assert fires because a new
// field was appended, V1_SIZE must be re-anchored to the NEW last field of
// the ORIGINAL v1 field set only (never move it onto the appended field).
static_assert(BCSV_OPEN_OPTS_V1_SIZE <= sizeof(bcsv_open_opts),
              "v1 floor cannot exceed the struct");
static_assert(std::is_trivially_copyable<bcsv_open_opts>::value,
              "bcsv_open_opts crosses the C ABI via memcpy - must stay trivially copyable");

/// Decode the caller's opts onto @p o (reset to INIT first). NULL opts = all
/// defaults. Returns false + reason when struct_size is outside the range. 
bool load_open_opts(const bcsv_open_opts* opts, bcsv_open_opts& o, std::string& err) {
    o = BCSV_OPEN_OPTS_INIT;
    if (!opts) return true;
    const size_t ss = opts->struct_size;
    if (ss < BCSV_OPEN_OPTS_V1_SIZE || ss > sizeof(bcsv_open_opts)) {
        err = "Error: bcsv_open_opts.struct_size out of range (must be between " +
              std::to_string(BCSV_OPEN_OPTS_V1_SIZE) + " and " +
              std::to_string(sizeof(bcsv_open_opts)) + ")";
        return false;
    }
    std::memcpy(&o, opts, ss);   // prefix copy; never reads past the caller's struct
    return true;
}

/// Shared members of both container kinds: resolved format + borrowed-member
/// registry bookkeeping. While a member is registered the container re-checks
/// it under its LEGACY kind (owned=false), so an *_native_u escape-hatch
/// handle passes the registry checks of the deep legacy APIs (sampler create)
/// while destroy guards still refuse to free a member the container owns.
struct UnifiedBase {
    bcsv_format_t resolved = BCSV_FORMAT_AUTO;   // what AUTO pinned to (post-open)
    void*         member   = nullptr;            // current member object (null before open / after release)

    bool opened() const { return resolved != BCSV_FORMAT_AUTO; }
    virtual HandleKind member_kind() const = 0;  // the member's legacy kind

    void adopt(void* m) {
        if (member) reg_forget(member);
        member = m;
        if (m) reg_register_borrowed(m, member_kind());
    }
    void release_registration() {
        if (member) { reg_forget(member); member = nullptr; }
    }
    virtual ~UnifiedBase() = default;
};

struct UnifiedReaderHandle : UnifiedBase {
    std::unique_ptr<bcsv::ReaderDirectAccess<bcsv::Layout>> bin;
    std::unique_ptr<bcsv::CsvReader<bcsv::Layout>>          csv;
    std::string  err;                              // container-level error (pre-member)

    bool binary() const { return resolved == BCSV_FORMAT_BCSV; }
    // The adopted member impersonates its own legacy kind while exposed via
    // bcsv_reader_native_u: the binary member IS a ReaderDirectAccess, the
    // CSV member IS a CsvReader — registry checks route them accordingly.
    HandleKind member_kind() const override { return binary() ? HandleKind::Reader : HandleKind::CsvReader; }

    // ── Active-member access (ADR-0009 kind-switch correctness) ──
    // Every navigation entry point MUST go through these: they key on the
    // RESOLVED format, never on member existence. A member kept allocated by
    // close-for-same-format-reuse must never serve rows after a kind switch
    // (reading the closed object returned 0 rows silently on .bcsv→.csv).
    // Branch-cheap: one enum compare, no virtuals (ADR-0009 hot-path rule).
    bcsv::ReaderDirectAccess<bcsv::Layout>* bin_member() {
        return binary() ? bin.get() : nullptr;
    }
    bcsv::CsvReader<bcsv::Layout>* csv_member() {
        return (opened() && !binary()) ? csv.get() : nullptr;
    }
    const bcsv::ReaderDirectAccess<bcsv::Layout>* bin_member() const {
        return binary() ? bin.get() : nullptr;
    }
    const bcsv::CsvReader<bcsv::Layout>* csv_member() const {
        return (opened() && !binary()) ? csv.get() : nullptr;
    }

    // Freeing an adopted member while its borrowed registry entry is alive
    // strands a stale entry (MINOR-1): release_registration() always first.
    void discard_bin() {
        if (!bin) return;
        if (member == bin.get()) release_registration();
        columnar_erase(bin.get());
        bin.reset();   // dtor closes the file
    }
    void discard_csv() {
        if (!csv) return;
        if (member == csv.get()) release_registration();
        csv.reset();   // dtor closes the file / LZ4 frame
    }

    // close keeps members allocated (a reopen of the SAME format reuses them);
    // a format switch in open_u discards the replaced member. Columnar state
    // is erased on close, mirroring bcsv_reader_close. resolved returns to
    // AUTO so format_u/is_open_u never claim a format that is no longer open.
    void close_member() {
        if (bin) { columnar_erase(bin.get()); if (bin->isOpen()) bin->close(); }
        if (csv) { if (csv->isOpen()) csv->close(); }
        release_registration();
        resolved = BCSV_FORMAT_AUTO;
    }

    ~UnifiedReaderHandle() {
        release_registration();
        if (bin) columnar_erase(bin.get());   // member dtors close files themselves
    }
};

struct UnifiedWriterHandle : UnifiedBase {
    bcsv::Layout     layout;                       // snapshot from create_u (survives the caller's layout handle)
    bcsv_row_codec_t codec = BCSV_CODEC_FLAT;      // names the .bcsv member codec
    WriterHandle*    bin  = nullptr;               // legacy pre-bound handle; container owns BOTH it and its object
    std::unique_ptr<bcsv::CsvWriter<bcsv::Layout>> csv;
    std::string  err;

    bool binary() const { return resolved == BCSV_FORMAT_BCSV; }
    // As above for writers: the binary member handle IS a WriterHandle,
    // the CSV member IS a CsvWriter — the shapes legacy code path expects.
    HandleKind member_kind() const override { return binary() ? HandleKind::Writer : HandleKind::CsvWriter; }

    // Member access keyed on the RESOLVED format — see UnifiedReaderHandle.
    // A replaced member must never serve writes (kind-switch fix, ADR-0009).
    WriterHandle* bin_member() { return binary() ? bin : nullptr; }
    bcsv::CsvWriter<bcsv::Layout>* csv_member() {
        return (opened() && !binary()) ? csv.get() : nullptr;
    }
    const WriterHandle* bin_member() const { return binary() ? bin : nullptr; }
    const bcsv::CsvWriter<bcsv::Layout>* csv_member() const {
        return (opened() && !binary()) ? csv.get() : nullptr;
    }

    // close releases the resolved claim; members stay allocated for same-format
    // reuse (a kind switch in open_u discards them). Resolved resets to AUTO.
    void close_members() {
        if (bin) bin->close_fn(bin->ptr);
        if (csv) csv->close();
        release_registration();
        resolved = BCSV_FORMAT_AUTO;
    }

    // WriterHandle is a plain struct, not a smart pointer: the concrete
    // writer object must go through delete_fn (the select-once trampoline)
    // before the handle itself is freed — exactly as bcsv_writer_destroy does.
    // Freeing an adopted member while its borrowed registry entry is alive
    // strands a stale entry (MINOR-1): release_registration() always first.
    void discard_bin() {
        if (!bin) return;
        if (member == bin) release_registration();
        bin->delete_fn(bin->ptr);
        delete bin;
        bin = nullptr;
    }
    void discard_csv() {
        if (!csv) return;
        if (member == csv.get()) release_registration();
        csv.reset();   // dtor closes the file / LZ4 frame (exceptions swallowed inside)
    }
    ~UnifiedWriterHandle() {
        release_registration();
        if (bin) { bin->delete_fn(bin->ptr); delete bin; }   // member dtors close files
    }                                                       // csv dtor via unique_ptr likewise
};

// Container-level error first: after a failed reopen the (closed) member may
// still hold a stale message while h.err carries the decisive reason (M5).
inline std::string writer_member_err(const UnifiedWriterHandle& h) {
    if (!h.err.empty()) return h.err;
    if (const auto* b = h.bin_member()) return *b->errorMsg_fn(b->ptr);
    if (const auto* c = h.csv_member()) return c->getErrorMsg();
    return std::string();
}
inline std::string reader_member_err(const UnifiedReaderHandle& h) {
    if (!h.err.empty()) return h.err;
    const std::string* member_err = nullptr;
    if (const auto* b = h.bin_member()) member_err = &b->getErrorMsg();
    else if (const auto* c = h.csv_member()) member_err = &c->getErrorMsg();
    return (member_err && !member_err->empty()) ? *member_err : std::string();
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
// Closes every open writer (binary or unified CSV/LZ4 member — the footer
// lands while the runtime is alive), then destroys all registered handles,
// borrowers before lenders. Call once from the host's last exit hook, on a
// process quiescent w.r.t. this API; after it, any *_destroy and any
// close/flush on a handle this API created is a logged no-op — never a
// double free or an access through freed memory. The refused-destroy
// guarantee holds while the handle's address stays unreclaimed; a double
// destroy interleaved with new handle creation can claim the recycled
// address. Supported pattern: one owner per handle, plus shutdown() at exit.
// Contract, rationale and the recycled-address caveat: docs/adr/0006.
// Borrowed entries (owned=false: a unified container's member exposed via
// *_native_u) are skipped — their container frees them exactly once, and
// closing that container's writer closes through the member anyway (the
// legacy close paths are idempotent through isOpen()).
void bcsv_shutdown(void) {
    std::unordered_map<void*, RegEntry> batch;
    {
        auto& reg = registry();
        std::lock_guard<std::mutex> lk(reg.mtx);
        batch.swap(reg.live);   // claim everything; later destroys see empty
    }

    // Best-effort: shutdown must never propagate — extern "C", process exit
    // path; every stage runs even if an earlier handle's teardown threw.
    auto swallow = [](auto&& fn) noexcept { try { fn(); } catch (...) {} };

    // Phase 1: finalize files while the runtime is fully alive, so the last
    // packet and footer land on disk deterministically. A unified writer's
    // CSV member keeps its LZ4 trailer contract: close flushes the frame.
    // Borrowed (owned=false) Writer/CsvWriter entries are exactly a unified
    // container's exposed members — closing the container's member here is
    // what the legacy cast would do; the container skips, closing members
    // through its own pointers below.
    for (auto& [ptr, e] : batch) {
        if (e.kind == HandleKind::Writer && e.owned)
            swallow([&]{ auto* h = static_cast<WriterHandle*>(ptr); h->close_fn(h->ptr); });
        else if (e.kind == HandleKind::UnifiedWriter && e.owned)
            swallow([&]{ auto* h = static_cast<UnifiedWriterHandle*>(ptr);
                         if (h->bin) h->bin->close_fn(h->bin->ptr);
                         if (h->csv) h->csv->close(); });
    }

    // Phase 2: destroy — kind-ordered so borrowers die before lenders
    // (writer borrows its layout's schema; sampler borrows its reader).
    for (auto pass = 0; pass < 2; ++pass) {
        for (auto& [ptr, e] : batch) {
            const HandleKind kind = e.kind;
            if (!e.owned) continue;   // borrowed member: its container owns it
            if (pass == 0) {   // owners & borrowers
                switch (kind) {
                    case HandleKind::Writer:
                        swallow([&]{ auto* h = static_cast<WriterHandle*>(ptr); h->delete_fn(h->ptr); delete h; });
                        break;
                    case HandleKind::UnifiedWriter:
                        swallow([&]{ delete static_cast<UnifiedWriterHandle*>(ptr); });
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
                    case HandleKind::UnifiedReader:
                        swallow([&]{ delete static_cast<UnifiedReaderHandle*>(ptr); });
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
    if (!reg_take("bcsv_layout_destroy", layout, HandleKind::Layout)) [[unlikely]] return;
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
    if (!reg_take("bcsv_reader_destroy", reader, HandleKind::Reader)) [[unlikely]] return;
    columnar_erase(reader);
    BCSV_CAPI_TRY_VOID("bcsv_reader_destroy", delete static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader))
}

void bcsv_reader_close(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_close", reader)) return;
    if (!reg_contains("bcsv_reader_close", reader, HandleKind::Reader)) [[unlikely]] return;
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
    if (!reg_take("bcsv_writer_destroy", writer, HandleKind::Writer)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_writer_destroy", ([&]() {
        auto* h = static_cast<WriterHandle*>(writer);
        h->delete_fn(h->ptr);
        delete h;
    })())
}

void bcsv_writer_close(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_close", writer)) return;
    if (!reg_contains("bcsv_writer_close", writer, HandleKind::Writer)) [[unlikely]] return;
    auto* h = static_cast<WriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_close", h->close_fn(h->ptr))
}

void bcsv_writer_flush(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_flush", writer)) return;
    if (!reg_contains("bcsv_writer_flush", writer, HandleKind::Writer)) [[unlikely]] return;
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
    // True after bcsv_writer_next/write rejected a row exceeding
    // MAX_ROW_LENGTH: row-codec state is desynced. Packet-based file codecs
    // (batch-LZ4 default) resynchronise at an empty-packet boundary via
    // flush(); stream codecs stay poisoned until close. Until then further
    // writes fail (reported as errors, not thrown).
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
    // Flags actually written to the header, not necessarily those passed to
    // bcsv_writer_open: the row-codec bits (ZERO_ORDER_HOLD, DELTA_ENCODING)
    // are replaced from the writer's own codec, so the header can never
    // disagree with it. Check here rather than assuming. Since 1.5.17.
    if (null_handle("bcsv_writer_file_flags", writer)) return 0;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_file_flags", 0, h->fileFlags_fn(h->ptr))
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
    if (!reg_take("bcsv_row_destroy", row, HandleKind::Row)) [[unlikely]] return;
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
    // Sampler needs the concrete binary reader. A _u unified handle is a
    // different object (UnifiedReaderHandle*); pass its binary member via
    // bcsv_reader_native_u(), which is registered as a live Reader. This
    // replaces the old blind cast that made any wrong pointer UB (ADR-0009).
    if (!reg_is_a("bcsv_sampler_create", reader, HandleKind::Reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_create", nullptr, ([&]() -> bcsv_sampler_t {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto* h = new SamplerHandle();
        h->sampler = new bcsv::Sampler<bcsv::Layout>(*r);
        return static_cast<bcsv_sampler_t>(reg_register(h, HandleKind::Sampler));
    })())
}

void bcsv_sampler_destroy(bcsv_sampler_t sampler) {
    if (!sampler) return;
    if (!reg_take("bcsv_sampler_destroy", sampler, HandleKind::Sampler)) [[unlikely]] return;
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

// ============================================================================
// Unified File API (ADR-0009)
// ============================================================================
extern "C" {

bool bcsv_probe_format(const char* filename, bcsv_format_t* out_format) {
    if (!out_format) {
        g_has_error = true;
        g_last_error = "bcsv_probe_format: NULL out_format";
        return false;
    }
    BCSV_CAPI_TRY_RETURN("bcsv_probe_format", false, ([&]() -> bool {
        bool ok = false;
        const bcsv_format_t fmt = sniff_file_format(filename, ok);
        if (!ok) {
            g_has_error = true;
            g_last_error = std::string("bcsv_probe_format: cannot read ") + (filename ? filename : "(null)");
            return false;
        }
        *out_format = fmt;
        return true;
    })())
}

// ---- Unified reader ---------------------------------------------------------

bcsv_reader_t bcsv_reader_create_u(void) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_create_u", nullptr,
        reg_register(new UnifiedReaderHandle(), HandleKind::UnifiedReader))
}

void bcsv_reader_destroy_u(bcsv_reader_t reader) {
    if (!reader) return;
    if (!reg_take("bcsv_reader_destroy_u", reader, HandleKind::UnifiedReader)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_reader_destroy_u", delete static_cast<UnifiedReaderHandle*>(reader))
}

bool bcsv_reader_open_u(bcsv_reader_t reader, const char* filename,
                        bcsv_format_t format, const bcsv_open_opts* opts) {
    if (null_handle("bcsv_reader_open_u", reader)) return false;
    try {
        auto* h = static_cast<UnifiedReaderHandle*>(reader);
        // Every exit below this point leaves resolved == AUTO (close_member
        // resets it): a failed open never leaves format_u lying (M5).
        if (h->opened()) h->close_member();          // reopen closes the previous file
        h->err.clear();
        if (!filename) { g_last_error = h->err = "Error: NULL filename"; return false; }
        if (format != BCSV_FORMAT_AUTO && format != BCSV_FORMAT_BCSV &&
            format != BCSV_FORMAT_CSV && format != BCSV_FORMAT_CSV_LZ4) {
            g_last_error = h->err = "Error: invalid format argument"; return false;
        }

        // opts validation (struct_size range + tri-state fields must be -1 or in range)
        bcsv_open_opts o;
        if (!load_open_opts(opts, o, h->err)) { g_last_error = h->err; return false; }
        if (o.rebuild_footer < -1 || o.rebuild_footer > 1) { g_last_error = h->err = opts_error("rebuild_footer", "must be -1/0/1"); return false; }
        if (o.has_header < -1 || o.has_header > 1)         { g_last_error = h->err = opts_error("has_header", "must be -1/0/1"); return false; }

        // Format resolution (ADR-0009): AUTO = sniff content; pinned = verify,
        // never fall back. resolve_reader_format owns the loud mismatch errors
        // incl. the CSV/CSV_LZ4 magic cross-refusals.
        bcsv_format_t resolved = format;
        if (!resolve_reader_format(format, filename, resolved, h->err)) {
            g_last_error = h->err;
            return false;
        }
        // After resolution — NOT just for the pinned enum — a text member
        // needs a schema: text files carry none (B2: the AUTO path used to
        // deref NULL here when opts==NULL or csv_layout==NULL).
        if ((resolved == BCSV_FORMAT_CSV || resolved == BCSV_FORMAT_CSV_LZ4) && !o.csv_layout) {
            g_last_error = h->err = "Error: CSV reader opens require opts->csv_layout (text files carry no schema)";
            return false;
        }

        bool ok_open = false;
        // resolved == BCSV_FORMAT_AUTO only when format == AUTO and probing
        // failed (missing/unreadable path): let the binary member produce
        // the real IO error; a successful open then means BCSV after all.
        if (resolved == BCSV_FORMAT_BCSV || resolved == BCSV_FORMAT_AUTO) {
            h->discard_csv();                       // switching kinds: truly replace the member
            if (!h->bin) h->bin = std::make_unique<bcsv::ReaderDirectAccess<bcsv::Layout>>();
            ok_open = h->bin->open(filename, tri(o.rebuild_footer, 0) != 0);
            if (ok_open && resolved == BCSV_FORMAT_AUTO) resolved = BCSV_FORMAT_BCSV;
            if (!ok_open) h->err = h->bin->getErrorMsg();
        } else {
            const auto* layout = static_cast<const bcsv::Layout*>(o.csv_layout);
            // ctor-frozen parse params: reuse only when layout AND
            // delimiter/decimal_sep are unchanged (M4 — comparing the layout
            // address alone silently kept the old parsing chars).
            const char delim = chr(o.delimiter, ',');
            const char dsep  = chr(o.decimal_sep, '.');
            const bool same = h->csv &&
                static_cast<const void*>(&h->csv->layout()) == static_cast<const void*>(layout) &&
                h->csv->delimiter() == delim && h->csv->decimalSeparator() == dsep;
            if (!same) {
                h->discard_csv();      // release registry claim BEFORE the free (MINOR-1)
                // (re)build only when a parse param changes: Row binds to its layout
                h->csv = std::make_unique<bcsv::CsvReader<bcsv::Layout>>(*layout, delim, dsep);
            }
            ok_open = h->csv->open(filename, tri(o.has_header, 1) != 0);
            if (!ok_open) h->err = h->csv->getErrorMsg();
        }
        if (!ok_open) {
            // Failed open: resolved still AUTO (close_member reset it); drop
            // any half-open member so nothing closed serves navigation.
            if (h->bin) h->discard_bin();
            if (h->csv) h->discard_csv();
            g_last_error = h->err.empty() ? "Error: open failed" : h->err;
            return false;
        }

        h->resolved = resolved;   // BEFORE adopt: member_kind() keys on it
        h->adopt(resolved == BCSV_FORMAT_BCSV ? static_cast<void*>(h->bin.get())
                                              : static_cast<void*>(h->csv.get()));
        clear_last_error();
        return true;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_open_u", ex);
        auto* h = static_cast<UnifiedReaderHandle*>(reader);
        h->err = ex.what();
        h->resolved = BCSV_FORMAT_AUTO;
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_open_u");
        static_cast<UnifiedReaderHandle*>(reader)->resolved = BCSV_FORMAT_AUTO;
        return false;
    }
}

void bcsv_reader_close_u(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_close_u", reader)) return;
    if (!reg_contains("bcsv_reader_close_u", reader, HandleKind::UnifiedReader)) [[unlikely]] return;
    auto* h = static_cast<UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_VOID("bcsv_reader_close_u", ([&]() { h->close_member(); })())
}

bool bcsv_reader_is_open_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_is_open_u", reader)) return false;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_is_open_u", false, ([&]() -> bool {
        if (const auto* b = h->bin_member()) return b->isOpen();
        if (const auto* c = h->csv_member()) return c->isOpen();
        return false;
    })())
}

bool bcsv_reader_next_u(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_next_u", reader)) return false;
    try {
        auto* h = static_cast<UnifiedReaderHandle*>(reader);
        bool ok = false;
        if (auto* b = h->bin_member()) ok = b->readNext();
        else if (auto* c = h->csv_member()) ok = c->readNext();
        else [[unlikely]] { g_last_error = "bcsv_reader_next_u: reader not open"; return false; }
        if (!ok) [[unlikely]] {
            const auto msg = reader_member_err(*h);
            if (!msg.empty()) { g_has_error = true; g_last_error = msg; }
        }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_reader_next_u", ex); return false; }
      catch (...) { set_last_error_unknown("bcsv_reader_next_u"); return false; }
}

const_bcsv_row_t bcsv_reader_row_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_row_u", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_row_u", static_cast<const_bcsv_row_t>(nullptr), ([&]() {
        auto* h = static_cast<const UnifiedReaderHandle*>(reader);
        if (const auto* b = h->bin_member()) return reinterpret_cast<const_bcsv_row_t>(&b->row());
        if (const auto* c = h->csv_member()) return reinterpret_cast<const_bcsv_row_t>(&c->row());
        return static_cast<const_bcsv_row_t>(nullptr);
    })())
}

size_t bcsv_reader_index_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_index_u", reader)) return 0u;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_index_u", 0u, ([&]() -> size_t {
        if (const auto* b = h->bin_member()) return b->rowPos();
        if (const auto* c = h->csv_member()) return c->rowPos();
        return 0u;
    })())
}

size_t bcsv_reader_file_line_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_file_line_u", reader)) return 0u;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_file_line_u", 0u,
        (h->csv_member() ? h->csv_member()->fileLine() : 0u))
}

const_bcsv_layout_t bcsv_reader_layout_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_layout_u", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_layout_u", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto* h = static_cast<const UnifiedReaderHandle*>(reader);
        if (const auto* b = h->bin_member()) return reinterpret_cast<const_bcsv_layout_t>(&b->layout());
        if (const auto* c = h->csv_member()) return reinterpret_cast<const_bcsv_layout_t>(&c->layout());
        return static_cast<const_bcsv_layout_t>(nullptr);
    })())
}

bcsv_format_t bcsv_reader_format_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_format_u", reader)) return BCSV_FORMAT_AUTO;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_format_u", BCSV_FORMAT_AUTO, h->resolved)
}

#ifdef _WIN32
const wchar_t* bcsv_reader_filename_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename_u", reader)) return nullptr;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename_u", static_cast<const wchar_t*>(nullptr), ([&]() -> const wchar_t* {
        if (const auto* b = h->bin_member()) return b->filePath().c_str();
        if (const auto* c = h->csv_member()) return c->filePath().c_str();
        return nullptr;
    })())
}
#else
const char* bcsv_reader_filename_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename_u", reader)) return nullptr;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename_u", static_cast<const char*>(nullptr), ([&]() -> const char* {
        if (const auto* b = h->bin_member()) return b->filePath().c_str();
        if (const auto* c = h->csv_member()) return c->filePath().c_str();
        return nullptr;
    })())
}
#endif

const char* bcsv_reader_error_msg_u(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_error_msg_u", reader)) return "";
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_error_msg_u", "", ([&]() -> const char* {
        g_fmt_buf = reader_member_err(*h);
        return g_fmt_buf.c_str();
    })())
}

void* bcsv_reader_native_u(const_bcsv_reader_t reader) {
    // The escape hatch for functions the unified reader does not carry: the
    // BCSV member is exposed as a bcsv_reader_t (deep binary APIs: sampler,
    // read(index), read_columns, …), the CSV member as the bcsv::CsvReader
    // object (C++ consumers). Borrowed: NEVER destroy the result and never
    // keep it across close_u/reopen_u — the container frees it. NULL when not
    // open. Registered under the member's legacy kind while open, so the
    // legacy registry guards accept it and refuse it after the close.
    if (null_handle("bcsv_reader_native_u", reader)) return nullptr;
    auto* h = static_cast<const UnifiedReaderHandle*>(reader);
    BCSV_CAPI_TRY_RETURN("bcsv_reader_native_u", static_cast<void*>(nullptr), ([&]() -> void* {
        if (const auto* b = h->bin_member()) return const_cast<void*>(static_cast<const void*>(b));
        if (const auto* c = h->csv_member()) return const_cast<void*>(static_cast<const void*>(c));
        return nullptr;
    })())
}

// ---- Unified writer ---------------------------------------------------------

bcsv_writer_t bcsv_writer_create_u(bcsv_layout_t layout, bcsv_row_codec_t row_codec) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_u", nullptr, ([&]() {
        if (row_codec != BCSV_CODEC_FLAT && row_codec != BCSV_CODEC_ZOH && row_codec != BCSV_CODEC_DELTA) {
            g_has_error = true;
            g_last_error = "bcsv_writer_create_u: invalid row_codec";
            return static_cast<bcsv_writer_t>(nullptr);
        }
        auto* h = new UnifiedWriterHandle();
        if (layout) h->layout = *static_cast<bcsv::Layout*>(layout);   // snapshot: layout handle may die first
        h->codec = row_codec;
        return static_cast<bcsv_writer_t>(reg_register(h, HandleKind::UnifiedWriter));
    })())
}

void bcsv_writer_destroy_u(bcsv_writer_t writer) {
    if (!writer) return;
    if (!reg_take("bcsv_writer_destroy_u", writer, HandleKind::UnifiedWriter)) [[unlikely]] return;
    BCSV_CAPI_TRY_VOID("bcsv_writer_destroy_u", delete static_cast<UnifiedWriterHandle*>(writer))
}

// Build the .bcsv member for the requested codec (or reuse the existing one
// when the codec is unchanged — WriterHandle caches its row pointer).
static void ensure_bin_member(UnifiedWriterHandle& h) {
    const auto want = (h.codec == BCSV_CODEC_ZOH)   ? WriterHandle::Type::ZoH
                    : (h.codec == BCSV_CODEC_DELTA) ? WriterHandle::Type::Delta
                                                    : WriterHandle::Type::Flat;
    if (h.bin && h.bin->type == want) return;
    h.discard_bin();
    h.bin = [&] {
        switch (want) {
            case WriterHandle::Type::ZoH:
                return createWriterHandle(want, new bcsv::WriterZoH<bcsv::Layout>(h.layout));
            case WriterHandle::Type::Delta:
                return createWriterHandle(want, new bcsv::WriterDelta<bcsv::Layout>(h.layout));
            case WriterHandle::Type::Flat:
            default:
                return createWriterHandle(want, new bcsv::WriterFlat<bcsv::Layout>(h.layout));
        }
    }();
}

bool bcsv_writer_open_u(bcsv_writer_t writer, const char* filename, bool overwrite,
                        bcsv_format_t format, const bcsv_open_opts* opts) {
    if (null_handle("bcsv_writer_open_u", writer)) return false;
    try {
        auto* h = static_cast<UnifiedWriterHandle*>(writer);
        // Every exit below leaves resolved == AUTO (close_members resets it):
        // a failed open never leaves format_u lying (M5).
        if (h->opened()) h->close_members();        // reopen closes the current file
        h->err.clear();
        if (!filename) { g_last_error = h->err = "Error: NULL filename"; return false; }

        bcsv_open_opts o;
        if (!load_open_opts(opts, o, h->err)) { g_last_error = h->err; return false; }
        if (o.compression < -1 || o.block_size_kb < -1)   { g_last_error = h->err = opts_error("compression/block_size_kb", "must be -1 or positive"); return false; }
        if (o.include_header < -1 || o.include_header > 1){ g_last_error = h->err = opts_error("include_header", "must be -1/0/1"); return false; }
        // FileFlags is uint16_t-backed; any value outside -1/its range would
        // reinterpret as a huge bit pattern through the static_cast below.
        if (o.file_flags < -1 || o.file_flags > 0xFFFF)   { g_last_error = h->err = opts_error("file_flags", "must be -1 or a uint16 bit pattern"); return false; }

        // Resolve format: AUTO = extension rule (".csv"→CSV, ".lz4"→CSV_LZ4,
        // else BCSV) — writers have no content to consult (ADR-0009).
        bcsv_format_t resolved = format;
        if (format == BCSV_FORMAT_AUTO) {
            const std::filesystem::path p(filename);
            const std::string ext = p.extension().string();
            resolved = (ext == ".lz4") ? BCSV_FORMAT_CSV_LZ4
                     : (ext == ".csv") ? BCSV_FORMAT_CSV
                                       : BCSV_FORMAT_BCSV;
        }

        bool ok_open = false;
        if (resolved == BCSV_FORMAT_BCSV) {
            h->discard_csv();                       // switching kinds: truly replace the member
            ensure_bin_member(*h);
            auto flags = (o.file_flags == -1)
                ? static_cast<bcsv_file_flags_t>(BCSV_FLAG_BATCH_COMPRESS |
                      (h->codec == BCSV_CODEC_ZOH ? BCSV_FLAG_ZOH :
                       h->codec == BCSV_CODEC_DELTA ? BCSV_FLAG_DELTA_ENCODING : 0))
                : static_cast<bcsv_file_flags_t>(o.file_flags);
            // Reuse bcsv_writer_open's codec-flag override semantics: the
            // handle type auto-sets its codec bit; when the caller pinned
            // flags we still let the writer enforce header truth.
            ok_open = h->bin->open_fn(h->bin->ptr, filename, overwrite,
                                      static_cast<size_t>(tri(o.compression, static_cast<int>(bcsv::DEFAULT_COMPRESSION_LEVEL))),
                                      static_cast<size_t>(tri(o.block_size_kb, static_cast<int>(bcsv::DEFAULT_PACKET_SIZE_KB))),
                                      static_cast<bcsv::FileFlags>(flags));
            if (!ok_open) h->err = *h->bin->errorMsg_fn(h->bin->ptr);
        } else if (resolved == BCSV_FORMAT_CSV || resolved == BCSV_FORMAT_CSV_LZ4) {
            if (o.lz4_block_id != -1 && (o.lz4_block_id < 4 || o.lz4_block_id > 7)) {
                g_last_error = h->err = opts_error("lz4_block_id", "must be -1 or 4..7");
                return false;
            }
            if (o.lz4_level < -1 || o.lz4_level > 12) { g_last_error = h->err = opts_error("lz4_level", "must be -1 or 0..12"); return false; }
            // The CSV writer's delimiter/decimal_sep are ctor-frozen, so the
            // member is REBUILT on every CSV open (M4: writing them into the
            // opts of a reused member would be a dead knob). CSV open is a
            // cold path; h->layout is the container's snapshot Layout.
            h->discard_csv();
            h->csv = std::make_unique<bcsv::CsvWriter<bcsv::Layout>>(
                h->layout, chr(o.delimiter, ','), chr(o.decimal_sep, '.'));
            auto* w = h->csv.get();
            // ADR-0008: setLz4/setLz4Mode are pre-open options — the frame
            // header is written by open().
            w->setLz4Mode(resolved == BCSV_FORMAT_CSV_LZ4 ? bcsv::CsvWriter<bcsv::Layout>::Lz4Mode::Frame
                                                          : bcsv::CsvWriter<bcsv::Layout>::Lz4Mode::Plain);
            if (resolved == BCSV_FORMAT_CSV_LZ4 && !w->setLz4(tri(o.lz4_block_id, 4), tri(o.lz4_level, 0))) {
                h->err = w->getErrorMsg();
                g_last_error = h->err;
                h->discard_csv();
                return false;
            }
            ok_open = w->open(filename, overwrite, tri(o.include_header, 1) != 0);
            if (!ok_open) h->err = w->getErrorMsg();
        } else {
            g_last_error = h->err = "Error: invalid format argument";
            return false;
        }
        if (!ok_open) {
            if (h->bin) h->discard_bin();   // drop the half-open member
            g_last_error = !h->err.empty() ? h->err : "Error: writer open failed";
            return false;
        }

        h->resolved = resolved;   // BEFORE adopt: member_kind() keys on it
        // Adopt EXACTLY what bcsv_writer_native_u hands out: the WriterHandle
        // for the binary member (legacy bcsv_writer_* take the handle, not the
        // concrete writer object), the CsvWriter itself for the CSV member —
        // so native-handle round-trips through the legacy deep APIs pass
        // their borrowed-kind registry checks (round-2 N1).
        h->adopt(resolved == BCSV_FORMAT_BCSV ? static_cast<void*>(h->bin)
                                              : static_cast<void*>(h->csv.get()));
        clear_last_error();
        return true;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_open_u", ex);
        auto* h = static_cast<UnifiedWriterHandle*>(writer);
        h->err = ex.what();
        h->resolved = BCSV_FORMAT_AUTO;
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_open_u");
        static_cast<UnifiedWriterHandle*>(writer)->resolved = BCSV_FORMAT_AUTO;
        return false;
    }
}

void bcsv_writer_close_u(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_close_u", writer)) return;
    if (!reg_contains("bcsv_writer_close_u", writer, HandleKind::UnifiedWriter)) [[unlikely]] return;
    auto* h = static_cast<UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_close_u", ([&]() {
        h->close_members();
    })())
}

bool bcsv_writer_is_open_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_is_open_u", writer)) return false;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_is_open_u", false, ([&]() -> bool {
        if (const auto* b = h->bin_member()) return b->isOpen_fn(b->ptr);
        if (const auto* c = h->csv_member()) return c->isOpen();
        return false;
    })())
}

bool bcsv_writer_next_u(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_next_u", writer)) return false;
    try {
        auto* h = static_cast<UnifiedWriterHandle*>(writer);
        if (auto* b = h->bin_member()) { b->writeRow_fn(b->ptr); clear_last_error(); return true; }
        if (auto* c = h->csv_member()) { c->writeRow();          clear_last_error(); return true; }
        g_last_error = "bcsv_writer_next_u: writer not open"; return false;
    } catch (const std::exception& ex) { set_last_error("bcsv_writer_next_u", ex); return false; }
      catch (...) { set_last_error_unknown("bcsv_writer_next_u"); return false; }
}

bool bcsv_writer_write_u(bcsv_writer_t writer, const_bcsv_row_t row) {
    if (null_handle("bcsv_writer_write_u", writer) || null_handle("bcsv_writer_write_u", row)) return false;
    try {
        auto* h = static_cast<UnifiedWriterHandle*>(writer);
        const auto* r = static_cast<const bcsv::Row*>(row);
        if (auto* b = h->bin_member()) { b->write_fn(b->ptr, *r); clear_last_error(); return true; }
        if (auto* c = h->csv_member()) { c->write(*r);            clear_last_error(); return true; }
        g_last_error = "bcsv_writer_write_u: writer not open"; return false;
    } catch (const std::exception& ex) { set_last_error("bcsv_writer_write_u", ex); return false; }
      catch (...) { set_last_error_unknown("bcsv_writer_write_u"); return false; }
}

bcsv_row_t bcsv_writer_row_u(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_row_u", writer)) return nullptr;
    auto* h = static_cast<UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_row_u", static_cast<bcsv_row_t>(nullptr), ([&]() -> bcsv_row_t {
        if (auto* b = h->bin_member()) return static_cast<bcsv_row_t>(b->cached_row);
        if (auto* c = h->csv_member()) return static_cast<bcsv_row_t>(&c->row());
        return nullptr;
    })())
}

size_t bcsv_writer_index_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_index_u", writer)) return 0u;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_index_u", 0u, ([&]() -> size_t {
        if (const auto* b = h->bin_member()) return b->rowCount_fn(b->ptr);
        if (const auto* c = h->csv_member()) return c->rowCount();
        return 0u;
    })())
}

const_bcsv_layout_t bcsv_writer_layout_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_layout_u", writer)) return nullptr;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_layout_u", static_cast<const_bcsv_layout_t>(nullptr),
        ([&]() {
            if (const auto* b = h->bin_member()) return reinterpret_cast<const_bcsv_layout_t>(b->layout_fn(b->ptr));
            if (const auto* c = h->csv_member()) return reinterpret_cast<const_bcsv_layout_t>(&c->layout());
            return static_cast<const_bcsv_layout_t>(&h->layout);   // container layout snapshot
        })())
}

bcsv_format_t bcsv_writer_format_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_format_u", writer)) return BCSV_FORMAT_AUTO;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_format_u", BCSV_FORMAT_AUTO, h->resolved)
}

#ifdef _WIN32
const wchar_t* bcsv_writer_filename_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename_u", writer)) return nullptr;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename_u", static_cast<const wchar_t*>(nullptr), ([&]() -> const wchar_t* {
        if (const auto* b = h->bin_member()) return b->filePath_fn(b->ptr);
        if (const auto* c = h->csv_member()) return c->filePath().c_str();
        return nullptr;
    })())
}
#else
const char* bcsv_writer_filename_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename_u", writer)) return nullptr;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename_u", static_cast<const char*>(nullptr), ([&]() -> const char* {
        if (const auto* b = h->bin_member()) return b->filePath_fn(b->ptr);
        if (const auto* c = h->csv_member()) return c->filePath().c_str();
        return nullptr;
    })())
}
#endif

const char* bcsv_writer_error_msg_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_error_msg_u", writer)) return "";
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_error_msg_u", "", ([&]() -> const char* {
        g_fmt_buf = writer_member_err(*h);
        return g_fmt_buf.c_str();
    })())
}

void* bcsv_writer_native_u(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_native_u", writer)) return nullptr;
    auto* h = static_cast<const UnifiedWriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_native_u", static_cast<void*>(nullptr),
        // The binary member is exposed as its WriterHandle (the shape the
        // legacy bcsv_writer_* functions take), the CSV member as the raw
        // CsvWriter. Borrowed: see bcsv_reader_native_u. Dispatch on RESOLVED,
        // like every other entry point — never on member existence.
        ([&]() -> void* {
            if (const auto* b = h->bin_member()) return const_cast<void*>(static_cast<const void*>(b));
            if (const auto* c = h->csv_member()) return const_cast<void*>(static_cast<const void*>(c));
            return nullptr;
        })())
}

} // extern "C" (unified file API, ADR-0009)
