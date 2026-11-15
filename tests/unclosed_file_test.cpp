/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file unclosed_file_test.cpp
 * @brief Files whose writer never reached close(), and footers that disagree with the packets.
 *
 * A writer ended by a crash or SIGKILL leaves no footer and usually a cut last
 * packet. Up to 1.5.24:
 *   - ReaderDirectAccess::open(path, true) (Python count_rows(), C
 *     bcsv_reader_open_ex) re-found the same packet header forever and grew
 *     the index without bound;
 *   - bcsvRepair wrote a footer one batch packet short, and its --dry-run
 *     counted differently;
 *   - direct access served rows by an index that disagreed with the packets.
 *
 * Every packet file codec x row codec is written with several packets and cut
 * at many points: exactly at a packet boundary, inside a packet header, inside
 * a payload, at the last byte of a packet, at the footer start (no footer),
 * inside the footer and not at all. Asserted for each cut:
 *   - the footer rebuild terminates within a bound and indexes exactly the rows
 *     of the complete packets, and direct access reads them correctly;
 *   - the sequential Reader delivers at least those rows, all correct;
 *   - bcsvRepair (-o, --in-place, --dry-run): footer rows == rows the
 *     sequential reader delivers from the result == dry-run rows.
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <vector>

#include <bcsv/bcsv.h>

#ifdef _WIN32
#  include <windows.h>
#  define UNCLOSED_POPEN  _popen
#  define UNCLOSED_PCLOSE _pclose
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <sys/wait.h>
#  define UNCLOSED_POPEN  popen
#  define UNCLOSED_PCLOSE pclose
#else
#  include <sys/wait.h>
#  define UNCLOSED_POPEN  popen
#  define UNCLOSED_PCLOSE pclose
#endif

using namespace bcsv;
namespace fs = std::filesystem;

namespace {

enum class RowKind { Flat, ZoH, Delta };

struct CodecCase {
    const char* name;
    RowKind     row;
    size_t      level;   // 0 = packet, > 0 = packet_lz4 (or batch with the flag)
    FileFlags   flags;
};

std::string caseName(const ::testing::TestParamInfo<CodecCase>& info) {
    return info.param.name;
}

constexpr size_t ROWS = 24000;        // Delta/ZoH: ~5 packets of 64 KB, Flat: ~25
constexpr size_t PACKET_KB = 64;      // MIN_PACKET_SIZE
constexpr size_t HASH_COLS = 4;

// Values change every third row, so ZoH and Delta have whole-row repeats to
// encode; the hash columns keep the changing rows incompressible.
int64_t     valueT(size_t i) { return static_cast<int64_t>(i / 3); }
double      valueX(size_t i) { return static_cast<double>(i / 3) * 0.5; }
std::string valueS(size_t i) { return "s" + std::to_string(i / 7); }
bool        valueB(size_t i) { return (i / 5) % 2 != 0; }
uint64_t    valueH(size_t i, size_t k) {   // splitmix64
    uint64_t z = (i / 3) * HASH_COLS + k + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

Layout makeLayout() {
    Layout layout;
    layout.addColumn({"t", ColumnType::INT64});
    layout.addColumn({"x", ColumnType::DOUBLE});
    layout.addColumn({"s", ColumnType::STRING});
    layout.addColumn({"b", ColumnType::BOOL});
    for (size_t k = 0; k < HASH_COLS; ++k)
        layout.addColumn({"h" + std::to_string(k), ColumnType::UINT64});
    return layout;
}

template<typename WriterT>
void writeRows(const std::string& path, const CodecCase& cc, FileFlags extra) {
    WriterT w(makeLayout());
    ASSERT_TRUE(w.open(path, true, cc.level, PACKET_KB, cc.flags | extra)) << w.getErrorMsg();
    for (size_t i = 0; i < ROWS; ++i) {
        w.row().set(0, valueT(i));
        w.row().set(1, valueX(i));
        w.row().set(2, valueS(i));
        w.row().set(3, valueB(i));
        for (size_t k = 0; k < HASH_COLS; ++k) w.row().set(4 + k, valueH(i, k));
        w.writeRow();
    }
    w.close();
}

void writeFile(const std::string& path, const CodecCase& cc) {
    switch (cc.row) {
        case RowKind::Flat:  writeRows<WriterFlat<Layout>>(path, cc, FileFlags::NONE); break;
        case RowKind::ZoH:   writeRows<WriterZoH<Layout>>(path, cc, FileFlags::ZERO_ORDER_HOLD); break;
        case RowKind::Delta: writeRows<WriterDelta<Layout>>(path, cc, FileFlags::DELTA_ENCODING); break;
    }
}

template<typename RowT>
bool rowMatches(const RowT& row, size_t i) {
    for (size_t k = 0; k < HASH_COLS; ++k)
        if (row.template get<uint64_t>(4 + k) != valueH(i, k)) return false;
    return row.template get<int64_t>(0) == valueT(i) &&
           row.template get<double>(1) == valueX(i) &&
           row.template get<std::string>(2) == valueS(i) &&
           row.template get<bool>(3) == valueB(i);
}

void copyPrefix(const std::string& src, const std::string& dst, uint64_t bytes) {
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    fs::resize_file(dst, bytes);
}

/// Rows the sequential Reader delivers (stops at EOF or at the first error),
/// checking every value. Returns SIZE_MAX if a value is wrong.
size_t sequentialRows(const std::string& path, bool* threw = nullptr) {
    Reader<Layout> r;
    if (!r.open(path)) return 0;
    size_t n = 0;
    try {
        while (r.readNext()) {
            if (!rowMatches(r.row(), n)) return SIZE_MAX;
            ++n;
        }
        if (threw) *threw = false;
    } catch (const std::exception&) {
        if (threw) *threw = true;
    }
    return n;
}

/// ReaderDirectAccess::open(path, rebuildFooter=true) with a hard time bound:
/// before 1.5.25 it never returned on a footer-less file (and grew without
/// bound), so a regression must fail the test, not hang the suite.
struct RebuildResult { bool opened = false; uint64_t rows = 0; std::string err; };

RebuildResult rebuildWithin(const std::string& path, std::chrono::seconds limit) {
    auto fut = std::async(std::launch::async, [path] {
        RebuildResult res;
        ReaderDirectAccess<Layout> da;
        res.opened = da.open(path, true);
        res.rows   = res.opened ? da.rowCount() : 0;
        res.err    = da.getErrorMsg();
        return res;
    });
    if (fut.wait_for(limit) != std::future_status::ready) {
        ADD_FAILURE() << "footer rebuild did not finish within " << limit.count() << " s: " << path;
        std::fflush(nullptr);
        std::_Exit(1);   // the stuck thread cannot be joined; end the process
    }
    return fut.get();
}

fs::path executableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(std::string(buf, len)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    _NSGetExecutablePath(buf.data(), &size);
    return fs::canonical(buf.c_str()).parent_path();
#else
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}

struct ToolRun { int exit_code = -1; std::string out; };

ToolRun runTool(const fs::path& tool, const std::string& args) {
    ToolRun run;
    std::string cmd = "\"" + tool.string() + "\" " + args;
#ifdef _WIN32
    cmd += " 2>NUL";
#else
    cmd += " 2>/dev/null";
#endif
    FILE* pipe = UNCLOSED_POPEN(cmd.c_str(), "r");
    if (!pipe) return run;
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) run.out += buf.data();
    const int status = UNCLOSED_PCLOSE(pipe);
#ifdef _WIN32
    run.exit_code = status;
#else
    run.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return run;
}

/// "rows_recovered": N from bcsvRepair --json; UINT64_MAX if absent.
uint64_t jsonRowsRecovered(const std::string& json) {
    const std::string key = "\"rows_recovered\": ";
    const auto pos = json.find(key);
    if (pos == std::string::npos) return UINT64_MAX;
    return std::strtoull(json.c_str() + pos + key.size(), nullptr, 10);
}

uint64_t footerRows(const std::string& path, bool* ok) {
    ReaderDirectAccess<Layout> da;
    *ok = da.open(path);   // no rebuild: the footer must be there and valid
    return *ok ? da.rowCount() : 0;
}

} // namespace

class UnclosedFileTest : public ::testing::TestWithParam<CodecCase> {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = std::string(info->test_suite_name()) + "_" + info->name();
        for (auto& ch : name) if (ch == '/') ch = '_';
        dir_ = fs::temp_directory_path() / "bcsv_unclosed_test" / name;
        fs::create_directories(dir_);
        full_ = (dir_ / "full.bcsv").string();
        writeFile(full_, GetParam());

        std::ifstream is(full_, std::ios::binary);
        ASSERT_TRUE(footer_.read(is)) << "the complete file must have a footer";
        fileSize_    = fs::file_size(full_);
        footerStart_ = fileSize_ - footer_.encodedSize();
        ASSERT_GE(footer_.packetIndex().size(), 4u) << "test needs several packets";
        ASSERT_EQ(footer_.rowCount(), ROWS);
    }
    void TearDown() override { fs::remove_all(dir_); }

    /// Offset just past packet k.
    uint64_t packetEnd(size_t k) const {
        const auto& idx = footer_.packetIndex();
        return k + 1 < idx.size() ? idx[k + 1].byte_offset : footerStart_;
    }
    uint64_t rowsBefore(size_t k) const {
        const auto& idx = footer_.packetIndex();
        return k < idx.size() ? idx[k].first_row : ROWS;
    }

    struct Cut { uint64_t bytes; uint64_t completeRows; std::string where; };

    std::vector<Cut> cuts() const {
        std::vector<Cut> c;
        const auto& idx = footer_.packetIndex();
        const size_t n = idx.size();
        // The first two, a middle one and the last two packets.
        for (size_t k : {size_t{0}, size_t{1}, n / 2, n - 2, n - 1}) {
            const uint64_t off = idx[k].byte_offset;
            const uint64_t end = packetEnd(k);
            const std::string p = "packet " + std::to_string(k);
            c.push_back({off,                 rowsBefore(k), "at the start of " + p});
            c.push_back({off + 7,             rowsBefore(k), "inside the header of " + p});
            c.push_back({off + 16 + 3,        rowsBefore(k), "at the payload start of " + p});
            c.push_back({off + (end - off) / 2, rowsBefore(k), "in the middle of " + p});
            c.push_back({end - 1,             rowsBefore(k), "at the last byte of " + p});
        }
        c.push_back({footerStart_,      ROWS, "at the footer start (no footer)"});
        c.push_back({footerStart_ + 10, ROWS, "inside the footer"});
        c.push_back({fileSize_ - 1,     ROWS, "at the last footer byte"});
        c.push_back({fileSize_,         ROWS, "not cut"});
        return c;
    }

    bool isBatch() const {
        return (GetParam().flags & FileFlags::BATCH_COMPRESS) != FileFlags::NONE && GetParam().level > 0;
    }

    fs::path dir_;
    std::string full_;
    FileFooter footer_;
    uint64_t fileSize_ = 0;
    uint64_t footerStart_ = 0;
};

TEST_P(UnclosedFileTest, RebuildIndexesTheCompletePackets) {
    const std::string cut = (dir_ / "cut.bcsv").string();
    for (const auto& c : cuts()) {
        SCOPED_TRACE("cut " + c.where + " (" + std::to_string(c.bytes) + " bytes)");
        copyPrefix(full_, cut, c.bytes);

        const auto t0 = std::chrono::steady_clock::now();
        const RebuildResult rb = rebuildWithin(cut, std::chrono::seconds(20));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        EXPECT_LT(ms, 5000) << "rebuild of a " << c.bytes << "-byte file";
        if (!rb.opened && isBatch() && c.completeRows == 0) {
            // The batch codec decodes the first packet when the file is
            // opened: with that packet cut, open() fails - a clear error, the
            // same one the sequential Reader gives.
            EXPECT_FALSE(rb.err.empty());
            Reader<Layout> seq;
            EXPECT_FALSE(seq.open(cut));
            continue;
        }
        ASSERT_TRUE(rb.opened) << rb.err;
        EXPECT_EQ(rb.rows, c.completeRows);

        if (c.bytes < fileSize_) {
            // No valid footer: a plain open refuses and leaves nothing open,
            // so the retry with a rebuild works (1.5.24: "already open").
            ReaderDirectAccess<Layout> retry;
            EXPECT_FALSE(retry.open(cut));
            EXPECT_FALSE(retry.isOpen());
            EXPECT_TRUE(retry.open(cut, true)) << retry.getErrorMsg();
        }

        // Direct access serves exactly those rows, correctly.
        ReaderDirectAccess<Layout> da;
        ASSERT_TRUE(da.open(cut, true)) << da.getErrorMsg();
        for (size_t i = 0; i < da.rowCount(); ++i) {
            ASSERT_TRUE(da.read(i)) << "row " << i << ": " << da.getErrorMsg();
            ASSERT_TRUE(rowMatches(da.row(), i)) << "row " << i;
        }
        EXPECT_FALSE(da.read(da.rowCount()));

        // The sequential Reader delivers at least the rows of the complete
        // packets (row-framed codecs also deliver the complete rows of a cut
        // packet before they fail; a cut batch packet delivers nothing).
        bool threw = false;
        const size_t seq = sequentialRows(cut, &threw);
        ASSERT_NE(seq, SIZE_MAX) << "the sequential reader returned a wrong value";
        EXPECT_GE(seq, c.completeRows);
        if (isBatch()) {
            EXPECT_EQ(seq, c.completeRows);
        }
    }
}

TEST_P(UnclosedFileTest, RepairFooterMatchesSequentialAndDryRun) {
#ifdef _WIN32
    const fs::path tool = executableDir() / "bcsvRepair.exe";
#else
    const fs::path tool = executableDir() / "bcsvRepair";
#endif
    ASSERT_TRUE(fs::exists(tool)) << tool;

    const std::string cut      = (dir_ / "cut.bcsv").string();
    const std::string repaired = (dir_ / "repaired.bcsv").string();
    const std::string inplace  = (dir_ / "inplace.bcsv").string();
    for (const auto& c : cuts()) {
        SCOPED_TRACE("cut " + c.where + " (" + std::to_string(c.bytes) + " bytes)");
        copyPrefix(full_, cut, c.bytes);
        copyPrefix(full_, inplace, c.bytes);

        const ToolRun dry = runTool(tool, "-i \"" + cut + "\" --dry-run --json");
        const ToolRun cp  = runTool(tool, "-i \"" + cut + "\" -o \"" + repaired + "\" --json");
        const ToolRun ip  = runTool(tool, "-i \"" + inplace + "\" --in-place --json");
        ASSERT_EQ(dry.exit_code, 0) << dry.out;
        ASSERT_EQ(cp.exit_code, 0) << cp.out;
        ASSERT_EQ(ip.exit_code, 0) << ip.out;
        const uint64_t dryRows = jsonRowsRecovered(dry.out);
        EXPECT_EQ(dryRows, jsonRowsRecovered(cp.out));
        EXPECT_EQ(dryRows, jsonRowsRecovered(ip.out));

        for (const std::string& out : {repaired, inplace}) {
            bool ok = false;
            const uint64_t fRows = footerRows(out, &ok);
            ASSERT_TRUE(ok) << "repaired file has no valid footer: " << out;
            bool threw = true;
            const size_t seq = sequentialRows(out, &threw);
            ASSERT_NE(seq, SIZE_MAX) << "wrong value in " << out;
            EXPECT_FALSE(threw) << "the sequential reader fails on the repaired file " << out;
            EXPECT_EQ(fRows, seq) << out;
            EXPECT_EQ(fRows, dryRows) << out;
            EXPECT_GE(fRows, c.completeRows) << out;
            if (isBatch()) {
                EXPECT_EQ(fRows, c.completeRows) << out;
            }

            ReaderDirectAccess<Layout> da;
            ASSERT_TRUE(da.open(out)) << da.getErrorMsg();
            for (size_t i = 0; i < da.rowCount(); ++i) {
                ASSERT_TRUE(da.read(i)) << out << " row " << i << ": " << da.getErrorMsg();
                ASSERT_TRUE(rowMatches(da.row(), i)) << out << " row " << i;
            }
        }
    }
}

// A footer that disagrees with the packets (1.5.24's bcsvRepair wrote one a
// batch packet short): direct access must refuse the rows it cannot place
// instead of serving the wrong ones, and the sequential reader is unaffected.
TEST_P(UnclosedFileTest, InconsistentFooterIsReportedNotServed) {
    const auto& idx = footer_.packetIndex();
    const uint64_t lastFirst = idx.back().first_row;
    struct Variant { const char* what; PacketIndex index; uint64_t rows; };
    const PacketIndex withoutLast(idx.begin(), idx.end() - 1);
    const std::vector<Variant> variants = {
        {"row count one packet short",            idx,         lastFirst},
        {"row count a few rows short",            idx,         ROWS - 5},
        {"row count a few rows long",             idx,         ROWS + 5},
        {"last packet missing from the index",    withoutLast, lastFirst},
        {"last packet missing, row count intact", withoutLast, ROWS},
    };
    const std::string path = (dir_ / "inconsistent.bcsv").string();
    for (const auto& v : variants) {
        SCOPED_TRACE(v.what);
        copyPrefix(full_, path, footerStart_);
        {
            std::ofstream os(path, std::ios::binary | std::ios::app);
            FileFooter bad(v.index, v.rows);
            ASSERT_TRUE(bad.write(os));
        }
        ReaderDirectAccess<Layout> da;
        ASSERT_TRUE(da.open(path)) << da.getErrorMsg();
        ASSERT_EQ(da.rowCount(), v.rows);
        // The index is right up to its last entry; that packet is assigned
        // [affectedFrom, v.rows) but holds [affectedFrom, actualEnd).
        const uint64_t affectedFrom = v.index.back().first_row;
        const uint64_t actualEnd = v.index.size() < idx.size() ? idx[v.index.size()].first_row : ROWS;
        for (size_t i = 0; i < affectedFrom; ++i) {
            ASSERT_TRUE(da.read(i)) << "row " << i << ": " << da.getErrorMsg();
            ASSERT_TRUE(rowMatches(da.row(), i)) << "row " << i;
        }
        if (v.rows == actualEnd) {
            for (size_t i = affectedFrom; i < v.rows; ++i) {
                ASSERT_TRUE(da.read(i)) << "row " << i << ": " << da.getErrorMsg();
                ASSERT_TRUE(rowMatches(da.row(), i)) << "row " << i;
            }
        } else {
            // A row the index places in a packet that disagrees with it is
            // refused, not served from the wrong place.
            for (size_t i = affectedFrom; i < v.rows; i += 97) {
                EXPECT_FALSE(da.read(i)) << "row " << i << " served from a packet the index misplaces";
                EXPECT_NE(da.getErrorMsg().find("footer inconsistent"), std::string::npos)
                    << da.getErrorMsg();
            }
        }
        EXPECT_FALSE(da.read(v.rows));
        // The sequential reader follows the packets: every row, all correct.
        bool threw = true;
        EXPECT_EQ(sequentialRows(path, &threw), ROWS);
        EXPECT_FALSE(threw);
    }
}

const CodecCase kCodecCases[] = {
    CodecCase{"Packet_Flat",      RowKind::Flat,  0, FileFlags::NONE},
    CodecCase{"Packet_ZoH",       RowKind::ZoH,   0, FileFlags::NONE},
    CodecCase{"Packet_Delta",     RowKind::Delta, 0, FileFlags::NONE},
    CodecCase{"PacketLZ4_Flat",   RowKind::Flat,  1, FileFlags::NONE},
    CodecCase{"PacketLZ4_ZoH",    RowKind::ZoH,   1, FileFlags::NONE},
    CodecCase{"PacketLZ4_Delta",  RowKind::Delta, 6, FileFlags::NONE},
#ifdef BCSV_HAS_BATCH_CODEC
    CodecCase{"Batch_Flat",       RowKind::Flat,  1, FileFlags::BATCH_COMPRESS},
    CodecCase{"Batch_ZoH",        RowKind::ZoH,   1, FileFlags::BATCH_COMPRESS},
    CodecCase{"Batch_Delta",      RowKind::Delta, 6, FileFlags::BATCH_COMPRESS},
#endif
};

INSTANTIATE_TEST_SUITE_P(PacketCodecs, UnclosedFileTest, ::testing::ValuesIn(kCodecCases), caseName);

// Stream-mode files have no packets and no footer by design: the rebuild
// finds nothing to index (and ends), the sequential reader reads every row.
TEST(UnclosedStreamFileTest, RebuildEndsWithoutPackets) {
    const fs::path dir = fs::temp_directory_path() / "bcsv_unclosed_test" / "stream";
    fs::create_directories(dir);
    for (size_t level : {size_t{0}, size_t{1}}) {
        SCOPED_TRACE("level " + std::to_string(level));
        const std::string path = (dir / "stream.bcsv").string();
        writeFile(path, CodecCase{"Stream", RowKind::Delta, level, FileFlags::STREAM_MODE});
        const std::string cut = (dir / "stream_cut.bcsv").string();
        copyPrefix(path, cut, fs::file_size(path) * 2 / 3);
        for (const std::string& p : {path, cut}) {
            const RebuildResult rb = rebuildWithin(p, std::chrono::seconds(20));
            ASSERT_TRUE(rb.opened) << rb.err;
            EXPECT_EQ(rb.rows, 0u);
        }
        EXPECT_EQ(sequentialRows(path), ROWS);
    }
    fs::remove_all(dir);
}
