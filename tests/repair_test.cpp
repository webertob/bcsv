/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file repair_test.cpp
 * @brief Comprehensive tests for bcsvRepair tool
 *
 * Tests cover all 5 file codecs x 3 row codecs with realistic parameters:
 *   - 72-column schema (6 per type: bool, int/uint 8/16/32/64, float, double, string)
 *   - 8 MB packet size (library default = 8192 KB)
 *   - Enough rows to fill 3+ valid packets, plus 1-2 damaged
 *
 * Corruption scenarios: truncation, missing footer, corrupt checksums.
 * Repair modes: dry-run, copy, in-place.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <array>
#include <string>
#include <bcsv/bcsv.h>
#include <bcsv/vle.hpp>

#ifdef _WIN32
#  include <windows.h>
#  define REPAIR_POPEN  _popen
#  define REPAIR_PCLOSE _pclose
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  define REPAIR_POPEN  popen
#  define REPAIR_PCLOSE pclose
#  include <sys/wait.h>
#else
#  define REPAIR_POPEN  popen
#  define REPAIR_PCLOSE pclose
#  include <sys/wait.h>
#endif

using namespace bcsv;
namespace fs = std::filesystem;

// ============================================================================
// Codec enum for unified write helper
// ============================================================================

enum class RowCodec { Flat, ZoH, Delta };

// ============================================================================
// Test fixture
// ============================================================================

class RepairTest : public ::testing::Test {
protected:
    static fs::path getExecutableDir() {
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            throw std::runtime_error("GetModuleFileNameA failed");
        return fs::path(buf).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buf(size, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) != 0)
            throw std::runtime_error("_NSGetExecutablePath failed");
        return fs::canonical(buf).parent_path();
#else
        return fs::canonical("/proc/self/exe").parent_path();
#endif
    }

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (fs::temp_directory_path() / "bcsv_repair_test"
                     / (std::string(info->test_suite_name()) + "_" + info->name())).string();
        fs::create_directories(test_dir_);

        auto exe_path = getExecutableDir();
#ifdef _WIN32
        repair_binary_ = (exe_path / "bcsvRepair.exe").string();
#else
        repair_binary_ = (exe_path / "bcsvRepair").string();
#endif
        ASSERT_TRUE(fs::exists(repair_binary_))
            << "bcsvRepair binary not found at: " << repair_binary_;
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }

    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    // -- 72-column layout (6 per type) ----------------------------------------

    Layout makeLayout() {
        Layout layout;
        const char* prefixes[] = {"b","u8","u16","u32","u64","i8","i16","i32","i64","f","d","s"};
        ColumnType types[] = {
            ColumnType::BOOL,
            ColumnType::UINT8, ColumnType::UINT16, ColumnType::UINT32, ColumnType::UINT64,
            ColumnType::INT8, ColumnType::INT16, ColumnType::INT32, ColumnType::INT64,
            ColumnType::FLOAT, ColumnType::DOUBLE, ColumnType::STRING
        };
        for (int t = 0; t < 12; ++t) {
            for (int n = 0; n < 6; ++n) {
                layout.addColumn({std::string(prefixes[t]) + std::to_string(n), types[t]});
            }
        }
        return layout;
    }

    // -- Unified file-write helper --------------------------------------------

    void writeFile(const std::string& path, size_t rows, RowCodec codec,
                   size_t compressionLevel, FileFlags flags,
                   size_t blockSizeKB = 8192) {
        auto layout = makeLayout();

        auto fillRow = [](auto& writer, size_t i) {
            size_t c = 0;
            for (int n = 0; n < 6; ++n) writer.row().set(c++, (i + n) % 2 != 0);
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<uint8_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<uint16_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<uint32_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<uint64_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<int8_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<int16_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<int32_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<int64_t>(i + n));
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<float>(i + n) * 0.5f);
            for (int n = 0; n < 6; ++n) writer.row().set(c++, static_cast<double>(i + n) * 0.25);
            for (int n = 0; n < 6; ++n) writer.row().set(c++, std::string("s") + std::to_string(i));
        };

        auto openAndWrite = [&](auto& writer, FileFlags f) {
            ASSERT_TRUE(writer.open(path, true, compressionLevel, blockSizeKB, f))
                << writer.getErrorMsg();
            for (size_t i = 0; i < rows; ++i) {
                if (codec == RowCodec::ZoH && i % 3 != 0) {
                    // ZoH: repeat previous row (only set values every 3rd)
                } else {
                    fillRow(writer, i);
                }
                writer.writeRow();
            }
            writer.close();
        };

        switch (codec) {
            case RowCodec::Flat: {
                WriterFlat<Layout> w(layout);
                openAndWrite(w, flags);
                break;
            }
            case RowCodec::ZoH: {
                WriterZoH<Layout> w(layout);
                openAndWrite(w, flags | FileFlags::ZERO_ORDER_HOLD);
                break;
            }
            case RowCodec::Delta: {
                WriterDelta<Layout> w(layout);
                openAndWrite(w, flags | FileFlags::DELTA_ENCODING);
                break;
            }
        }
    }

    // -- Corruption helpers ---------------------------------------------------

    static void truncateFile(const std::string& path, uintmax_t newSize) {
        fs::resize_file(path, newSize);
    }

    void stripFooter(const std::string& path) {
        FileFooter footer;
        {
            std::ifstream is(path, std::ios::binary);
            ASSERT_TRUE(is && footer.read(is)) << "Cannot read footer from: " << path;
        }
        auto file_size = fs::file_size(path);
        auto footer_size = footer.encodedSize();
        ASSERT_GT(file_size, footer_size);
        truncateFile(path, file_size - footer_size);
    }

    static void corruptByte(const std::string& path, uintmax_t offset) {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f) << "Cannot open: " << path;
        f.seekp(static_cast<std::streamoff>(offset));
        char byte;
        f.read(&byte, 1);
        byte = static_cast<char>(~byte);
        f.seekp(static_cast<std::streamoff>(offset));
        f.write(&byte, 1);
    }

    // -- Tool execution helpers -----------------------------------------------

    struct RepairOutput {
        int exit_code;
        std::string stdout_text;
        std::string stderr_text;
    };

    RepairOutput runRepair(const std::vector<std::string>& args) {
        std::string stderr_path = test_dir_ + "/repair_stderr.txt";
        std::string cmd = repair_binary_;
        for (const auto& a : args) cmd += " " + a;
        cmd += " 2>" + stderr_path;

        RepairOutput out;
        FILE* pipe = REPAIR_POPEN(cmd.c_str(), "r");
        EXPECT_NE(pipe, nullptr);
        if (!pipe) { out.exit_code = -1; return out; }

        std::array<char, 4096> buf;
        while (fgets(buf.data(), static_cast<int>(buf.size()), pipe))
            out.stdout_text += buf.data();
        int status = REPAIR_PCLOSE(pipe);
#ifdef _WIN32
        out.exit_code = status;  // _pclose returns the process exit code directly
#else
        out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

        std::ifstream err_file(stderr_path);
        if (err_file) {
            std::ostringstream ss;
            ss << err_file.rdbuf();
            out.stderr_text = ss.str();
        }
        return out;
    }

    RepairOutput dryRunJson(const std::string& input) {
        return runRepair({"-i", input, "--dry-run", "--json"});
    }

    RepairOutput repairCopy(const std::string& input, const std::string& output) {
        return runRepair({"-i", input, "-o", output});
    }

    // -- Verification helpers -------------------------------------------------

    size_t countRows(const std::string& path) {
        Reader<Layout> reader;
        if (!reader.open(path)) return 0;
        size_t count = 0;
        while (reader.readNext()) ++count;
        reader.close();
        return count;
    }

    void verifyRepairedFile(const std::string& path, size_t expectedRows) {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        size_t count = 0;
        while (reader.readNext()) ++count;
        EXPECT_EQ(count, expectedRows);
        reader.close();
    }

    static bool jsonContains(const std::string& json,
                              const std::string& key, const std::string& value) {
        return json.find("\"" + key + "\": " + value) != std::string::npos;
    }

    std::string test_dir_;
    std::string repair_binary_;
};

// ============================================================================
// Parameterized test: packet codecs x row codecs
// ============================================================================

struct PacketRepairParam {
    std::string name;
    RowCodec    row_codec;
    size_t      compression;   // 0 = uncompressed, 1+ = LZ4
    FileFlags   flags;
};

class PacketRepairTest : public RepairTest,
                         public ::testing::WithParamInterface<PacketRepairParam> {};

// Enough rows for ~4 packets at 8 MB each with 72 columns (~350 bytes/row)
// 8 MB / 350 = ~24 000 rows per packet --> 100 000 rows = 4+ packets
static constexpr size_t PACKET_ROWS = 100'000;

TEST_P(PacketRepairTest, MissingFooter) {
    const auto& p = GetParam();
    auto path = testFile("missing_footer.bcsv");
    writeFile(path, PACKET_ROWS, p.row_codec, p.compression, p.flags);
    stripFooter(path);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "true"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_footer", "false"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "is_stream_mode", "false"));

    auto repaired = testFile("missing_footer_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    verifyRepairedFile(repaired, PACKET_ROWS);
}

TEST_P(PacketRepairTest, TruncatedLastPacket) {
    const auto& p = GetParam();
    auto path = testFile("trunc.bcsv");
    writeFile(path, PACKET_ROWS, p.row_codec, p.compression, p.flags);
    stripFooter(path);

    auto no_footer_size = fs::file_size(path);
    // Remove 200 KB from end (should damage the last packet)
    truncateFile(path, no_footer_size - 200 * 1024);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "true"));

    auto repaired = testFile("trunc_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);

    size_t recovered = countRows(repaired);
    EXPECT_GT(recovered, PACKET_ROWS / 2);  // most packets should survive
    EXPECT_LE(recovered, PACKET_ROWS);
}

TEST_P(PacketRepairTest, ValidFileNoDamage) {
    const auto& p = GetParam();
    auto path = testFile("valid.bcsv");
    writeFile(path, PACKET_ROWS, p.row_codec, p.compression, p.flags);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "false"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "success", "true"));
}

INSTANTIATE_TEST_SUITE_P(Codecs, PacketRepairTest, ::testing::Values(
    PacketRepairParam{"Packet_Flat",    RowCodec::Flat,  0, FileFlags::NONE},
    PacketRepairParam{"Packet_ZoH",     RowCodec::ZoH,   0, FileFlags::NONE},
    PacketRepairParam{"Packet_Delta",   RowCodec::Delta, 0, FileFlags::NONE},
    PacketRepairParam{"PacketLZ4_Flat", RowCodec::Flat,  1, FileFlags::NONE},
    PacketRepairParam{"PacketLZ4_ZoH",  RowCodec::ZoH,   1, FileFlags::NONE},
    PacketRepairParam{"PacketLZ4_Delta",RowCodec::Delta, 1, FileFlags::NONE}
), [](const auto& info) { return info.param.name; });

// ============================================================================
// Parameterized test: stream codecs x row codecs
// ============================================================================

struct StreamRepairParam {
    std::string name;
    RowCodec    row_codec;
    size_t      compression;
};

class StreamRepairTest : public RepairTest,
                         public ::testing::WithParamInterface<StreamRepairParam> {};

// Stream tests: 5000 rows (no packet boundaries, just row-by-row)
static constexpr size_t STREAM_ROWS = 5000;

TEST_P(StreamRepairTest, Truncated) {
    const auto& p = GetParam();
    auto path = testFile("stream_trunc.bcsv");
    writeFile(path, STREAM_ROWS, p.row_codec, p.compression, FileFlags::STREAM_MODE);
    auto orig_size = fs::file_size(path);

    // Remove 10 KB from end
    truncateFile(path, orig_size - 10 * 1024);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "is_stream_mode", "true"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "true"));

    auto repaired = testFile("stream_trunc_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    EXPECT_TRUE(fs::exists(repaired));
    EXPECT_LT(fs::file_size(repaired), orig_size);

    // For non-LZ4 stream codecs, verify row count via Reader
    if (p.compression == 0) {
        size_t recovered = countRows(repaired);
        EXPECT_GT(recovered, STREAM_ROWS / 2);
        EXPECT_LT(recovered, STREAM_ROWS);
    }
}

TEST_P(StreamRepairTest, ValidFileNoDamage) {
    const auto& p = GetParam();
    auto path = testFile("stream_valid.bcsv");
    writeFile(path, STREAM_ROWS, p.row_codec, p.compression, FileFlags::STREAM_MODE);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "is_stream_mode", "true"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "false"));
}

INSTANTIATE_TEST_SUITE_P(Codecs, StreamRepairTest, ::testing::Values(
    StreamRepairParam{"Stream_Flat",      RowCodec::Flat,  0},
    StreamRepairParam{"Stream_ZoH",       RowCodec::ZoH,   0},
    StreamRepairParam{"Stream_Delta",     RowCodec::Delta, 0},
    StreamRepairParam{"StreamLZ4_Flat",   RowCodec::Flat,  1},
    StreamRepairParam{"StreamLZ4_ZoH",    RowCodec::ZoH,   1},
    StreamRepairParam{"StreamLZ4_Delta",  RowCodec::Delta, 1}
), [](const auto& info) { return info.param.name; });

// ============================================================================
// PacketLZ4 Batch: no partial recovery possible
// ============================================================================

#ifdef BCSV_HAS_BATCH_CODEC
TEST_F(RepairTest, PacketLZ4Batch_MissingFooter) {
    auto path = testFile("batch_no_footer.bcsv");
    writeFile(path, PACKET_ROWS, RowCodec::Flat, 1, FileFlags::BATCH_COMPRESS);
    stripFooter(path);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);

    auto repaired = testFile("batch_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    verifyRepairedFile(repaired, PACKET_ROWS);
}

TEST_F(RepairTest, PacketLZ4Batch_Truncated) {
    auto path = testFile("batch_trunc.bcsv");
    writeFile(path, PACKET_ROWS, RowCodec::Flat, 1, FileFlags::BATCH_COMPRESS);
    stripFooter(path);
    auto no_footer_size = fs::file_size(path);
    truncateFile(path, no_footer_size - 500 * 1024);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "true"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "rows_recovered_partial", "0"));

    auto repaired = testFile("batch_trunc_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    size_t recovered = countRows(repaired);
    EXPECT_LE(recovered, PACKET_ROWS);
}
#endif

// ============================================================================
// Damaged middle packet: corrupt packet 2 header, verify tool stops there
// ============================================================================

TEST_F(RepairTest, Packet_Flat_CorruptMiddlePacket) {
    auto path = testFile("middle_corrupt.bcsv");
    writeFile(path, PACKET_ROWS, RowCodec::Flat, 0, FileFlags::NONE);
    stripFooter(path);

    // Find the 2nd packet header by walking: header → packet 0 → packet 1 → packet 2
    Layout layout = makeLayout();
    size_t hdr_size = FileHeader::getBinarySize(layout);
    std::ifstream is(path, std::ios::binary);
    is.seekg(static_cast<std::streamoff>(hdr_size));

    // Skip packets 0 and 1 to find offset of packet 2
    uint64_t pkt2_offset = 0;
    for (int pkt = 0; pkt < 2; ++pkt) {
        PacketHeader ph;
        ASSERT_TRUE(ph.read(is)) << "Cannot read packet " << pkt;
        // Walk the payload to find end
        while (true) {
            uint64_t rowLen = 0;
            try {
                bcsv::vleDecode<uint64_t, true>(is, rowLen, nullptr);
            } catch (...) { FAIL() << "VLE decode failed in packet " << pkt; }
            if (rowLen == bcsv::PCKT_TERMINATOR) {
                uint64_t checksum;
                is.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
                break;
            }
            if (rowLen > 0) is.seekg(static_cast<std::streamoff>(rowLen), std::ios::cur);
        }
    }
    pkt2_offset = static_cast<uint64_t>(is.tellg());
    is.close();
    ASSERT_GT(pkt2_offset, hdr_size);

    // Corrupt the magic bytes of packet 2 header
    corruptByte(path, pkt2_offset);
    corruptByte(path, pkt2_offset + 1);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    // Tool should find packets 0 and 1 only; packet 2+ lost
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "true"));

    auto repaired = testFile("middle_corrupt_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    size_t recovered = countRows(repaired);
    // Should have rows from packets 0+1 but not 2+
    EXPECT_GT(recovered, PACKET_ROWS / 4);
    EXPECT_LT(recovered, PACKET_ROWS);
}

// ============================================================================
// Stream + Flat: corrupt last-row checksum -> loses exactly 1 row
// ============================================================================

TEST_F(RepairTest, Stream_Flat_CorruptChecksum) {
    auto path = testFile("stream_corrupt.bcsv");
    writeFile(path, 500, RowCodec::Flat, 0, FileFlags::STREAM_MODE);
    auto file_size = fs::file_size(path);
    corruptByte(path, file_size - 3);  // flip byte inside last row's XXH32

    auto repaired = testFile("stream_corrupt_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);
    EXPECT_EQ(countRows(repaired), 499u);
}

// ============================================================================
// In-place repair with backup
// ============================================================================

TEST_F(RepairTest, Packet_Flat_InPlaceBackup) {
    auto path = testFile("inplace.bcsv");
    writeFile(path, PACKET_ROWS, RowCodec::Flat, 0, FileFlags::NONE);
    stripFooter(path);
    auto stripped_size = fs::file_size(path);

    auto out = runRepair({"-i", path, "--in-place", "--backup"});
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(fs::exists(path + ".bak"));
    EXPECT_EQ(fs::file_size(path + ".bak"), stripped_size);
    verifyRepairedFile(path, PACKET_ROWS);
}

TEST_F(RepairTest, Stream_Flat_InPlace) {
    auto path = testFile("stream_inplace.bcsv");
    writeFile(path, 1000, RowCodec::Flat, 0, FileFlags::STREAM_MODE);
    auto orig_size = fs::file_size(path);
    truncateFile(path, orig_size - 500);

    auto out = runRepair({"-i", path, "--in-place", "--backup"});
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(fs::exists(path + ".bak"));
    size_t recovered = countRows(path);
    EXPECT_GT(recovered, 0u);
    EXPECT_LT(recovered, 1000u);
}

// ============================================================================
// Deep validation
// ============================================================================

TEST_F(RepairTest, Packet_Flat_DeepValid) {
    auto path = testFile("deep_valid.bcsv");
    writeFile(path, PACKET_ROWS, RowCodec::Flat, 0, FileFlags::NONE);
    stripFooter(path);

    auto out = runRepair({"-i", path, "--dry-run", "--json", "--deep"});
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "deep", "true"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "success", "true"));
}

// ============================================================================
// Edge cases (lightweight -- small files)
// ============================================================================

TEST_F(RepairTest, EmptyPacketFile) {
    auto path = testFile("empty.bcsv");
    writeFile(path, 0, RowCodec::Flat, 0, FileFlags::NONE);
    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "rows_recovered", "0"));
}

TEST_F(RepairTest, EmptyStreamFile) {
    auto path = testFile("empty_stream.bcsv");
    writeFile(path, 0, RowCodec::Flat, 0, FileFlags::STREAM_MODE);
    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "rows_recovered", "0"));
    EXPECT_TRUE(jsonContains(out.stdout_text, "had_damage", "false"));
}

TEST_F(RepairTest, HeaderOnlyTruncated) {
    auto path = testFile("header_only.bcsv");
    writeFile(path, 1000, RowCodec::Flat, 0, FileFlags::NONE);
    FileHeader fh;
    Layout layout;
    {
        std::ifstream is(path, std::ios::binary);
        fh.readFromBinary(is, layout);
    }
    truncateFile(path, FileHeader::getBinarySize(layout));
    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    EXPECT_TRUE(jsonContains(out.stdout_text, "rows_recovered", "0"));
}

TEST_F(RepairTest, NonExistentFile) {
    auto out = runRepair({"-i", "/tmp/bcsv_nonexistent_42.bcsv", "--dry-run", "--json"});
    EXPECT_EQ(out.exit_code, 1);
    EXPECT_TRUE(out.stdout_text.find("\"success\": false") != std::string::npos);
}

// ============================================================================
// Argument validation
// ============================================================================

TEST_F(RepairTest, ArgError_MissingInput) {
    EXPECT_EQ(runRepair({"--dry-run"}).exit_code, 2);
}

TEST_F(RepairTest, ArgError_ConflictingModes) {
    auto path = testFile("dummy.bcsv");
    writeFile(path, 10, RowCodec::Flat, 0, FileFlags::NONE, 64);
    EXPECT_EQ(runRepair({"-i", path, "-o", "out.bcsv", "--in-place"}).exit_code, 2);
}

TEST_F(RepairTest, ArgError_NoOutputMode) {
    auto path = testFile("dummy2.bcsv");
    writeFile(path, 10, RowCodec::Flat, 0, FileFlags::NONE, 64);
    EXPECT_EQ(runRepair({"-i", path}).exit_code, 2);
}

// ============================================================================
// JSON output completeness
// ============================================================================

TEST_F(RepairTest, JsonOutputCompleteness) {
    auto path = testFile("json_check.bcsv");
    writeFile(path, 500, RowCodec::Flat, 0, FileFlags::NONE, 64);
    stripFooter(path);

    auto out = dryRunJson(path);
    EXPECT_EQ(out.exit_code, 0);
    for (const auto& key : {"input", "file_size", "is_stream_mode", "had_footer",
                             "packets_found", "packets_discarded", "rows_recovered",
                             "rows_recovered_partial", "rows_discarded", "recovery_pct",
                             "had_damage",
                             "bytes_trimmed", "footer_size", "dry_run", "deep",
                             "warnings", "success"}) {
        EXPECT_TRUE(out.stdout_text.find(std::string("\"") + key + "\"") != std::string::npos)
            << "Missing JSON key: " << key;
    }
}

// ============================================================================
// Data integrity after repair (uses small file for fast row-by-row check)
// ============================================================================

TEST_F(RepairTest, Packet_Flat_DataIntegrity) {
    auto path = testFile("data_verify.bcsv");
    writeFile(path, 200, RowCodec::Flat, 0, FileFlags::NONE, 64);
    stripFooter(path);

    auto repaired = testFile("data_verify_repaired.bcsv");
    auto rep = repairCopy(path, repaired);
    EXPECT_EQ(rep.exit_code, 0);

    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(repaired)) << reader.getErrorMsg();
    for (size_t i = 0; i < 200; ++i) {
        ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
        // Spot-check a few columns: INT32[0] (column 42) and FLOAT[0] (column 54)
        EXPECT_EQ(reader.row().get<int32_t>(42), static_cast<int32_t>(i));
        EXPECT_FLOAT_EQ(reader.row().get<float>(54), static_cast<float>(i) * 0.5f);
    }
    EXPECT_FALSE(reader.readNext());
    reader.close();
}
