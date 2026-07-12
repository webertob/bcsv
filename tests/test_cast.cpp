/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <bcsv/bcsv.h>

#ifdef _WIN32
#  define CAST_POPEN  _popen
#  define CAST_PCLOSE _pclose
#  define CAST_EXE_SUFFIX ".exe"
#else
#  include <sys/wait.h>
#  define CAST_POPEN  popen
#  define CAST_PCLOSE pclose
#  define CAST_EXE_SUFFIX ""
#endif

namespace fs = std::filesystem;

// Find the bcsvCast binary from common build locations
static std::string findCastBin() {
    fs::path cur = fs::current_path();

    std::string candidates[] = {
        (cur / "bin" / "bcsvCast" CAST_EXE_SUFFIX).string(),
        (cur / "../bin" / "bcsvCast" CAST_EXE_SUFFIX).string(),
        (cur / "bcsvCast" CAST_EXE_SUFFIX).string(),
        "build/ninja-debug/bin/bcsvCast" CAST_EXE_SUFFIX,
        "build/ninja-release/bin/bcsvCast" CAST_EXE_SUFFIX,
    };

    for (auto& c : candidates) {
        fs::path p(c);
        if (fs::exists(p) && fs::is_regular_file(p))
            return p.string();
    }

    return (cur / "bin" / "bcsvCast" CAST_EXE_SUFFIX).string();
}

class CastTest : public ::testing::Test {
protected:
    std::string test_dir_;
    std::string cast_bin_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_        = (fs::path("tmp") /
                            (std::string(info->test_suite_name()) + "_" +
                             info->name()))
                               .string();
        fs::create_directories(test_dir_);
        cast_bin_ = findCastBin();
    }

    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    int runCast(const std::vector<std::string>& args,
                  std::string&                    stdout_out, std::string& /*stderr*/) {
        std::string cmd = "\"" + cast_bin_ + "\"";
        for (auto& a : args)
            cmd += " " + a;

        std::string merged = cmd + " 2>&1";
        FILE*       pipe   = CAST_POPEN(merged.c_str(), "r");
        if (!pipe)
            return -1;

        char buf[256];
        while (std::fgets(buf, sizeof(buf), pipe))
            stdout_out += buf;
        int rc = CAST_PCLOSE(pipe);
#ifdef _WIN32
        return rc;  // _pclose returns the process exit code directly
#else
        return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    }

    void writeInt64File(const std::string&          path,
                        const std::vector<int64_t>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::INT64});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (auto v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeDoubleFile(const std::string&         path,
                         const std::vector<double>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::DOUBLE});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (auto v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeFloatFile(const std::string&        path,
                        const std::vector<float>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::FLOAT});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (auto v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeBoolFile(const std::string&       path,
                       const std::vector<bool>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::BOOL});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (bool v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeUint64File(const std::string&           path,
                         const std::vector<uint64_t>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::UINT64});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (auto v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeStringFile(const std::string&              path,
                         const std::vector<std::string>& values) {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::STRING});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        for (auto& v : values) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    void writeMixedFile(const std::string&                   path,
                        const std::vector<std::string>&      names,
                        const std::vector<bcsv::ColumnType>& types,
                        const std::vector<bcsv::ValueType>&  values) {
        bcsv::Layout layout;
        for (size_t i = 0; i < names.size(); ++i) {
            layout.addColumn({names[i], types[i]});
        }
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path, true);
        auto& row = writer.row();
        for (size_t i = 0; i < names.size(); ++i) {
            std::visit([i, &row](const auto& v) { row.set(i, v); },
                       values[i]);
        }
        writer.writeRow();
        writer.close();
    }
};

// ── Integer narrowing ──

TEST_F(CastTest, Int64ToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 0, 1});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(CastTest, Int64ToUint8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(CastTest, Int64ToUint16) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 50000});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT16);
    reader.close();
}

TEST_F(CastTest, Int64ToInt8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {-128, 0, 127});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT8);
    reader.close();
}

TEST_F(CastTest, Int64ToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {INT32_MIN, 0, INT32_MAX});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    reader.close();
}

TEST_F(CastTest, Int64Unchanged) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {INT64_MIN, INT64_MAX});

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

// CRITICAL: verify signed ladder checks both min and max (regression test)
TEST_F(CastTest, DoubleWithNegativeToSigned) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // {-2000.0, 5.0} should NOT narrow to INT8 (would overflow -2000)
    writeDoubleFile(path.string(), {-2000.0, 5.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    auto t = reader.layout().columnType(0);
    // -2000 needs INT16 (range -32768..32767), not INT8 (-128..127)
    EXPECT_EQ(t, bcsv::ColumnType::INT16);

    // Verify values round-trip correctly
    int16_t expected[] = {-2000, 5};
    size_t  idx        = 0;
    while (reader.readNext()) {
        int16_t val;
        ASSERT_TRUE(reader.row().get<int16_t>(0, val));
        EXPECT_EQ(val, expected[idx]);
        ++idx;
    }
    EXPECT_EQ(idx, 2);
    reader.close();
}

TEST_F(CastTest, Uint64LargeValues) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeUint64File(path.string(), {UINT32_MAX + 1, UINT64_MAX});

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    // Verify analyze shows unchanged
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(CastTest, Uint64ToUint32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeUint64File(path.string(), {0, 1000000, UINT32_MAX});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT32);
    reader.close();
}

// ── Double narrowing ──

TEST_F(CastTest, DoubleToFloat) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1.0, 0.5, 3.14f, 100.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    auto t = reader.layout().columnType(0);
    EXPECT_TRUE(t == bcsv::ColumnType::FLOAT ||
                t == bcsv::ColumnType::UINT32);
    reader.close();
}

TEST_F(CastTest, DoubleToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.0, 50.0, 100.0, -10.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    auto t = reader.layout().columnType(0);
    EXPECT_TRUE(t == bcsv::ColumnType::INT8 ||
                t == bcsv::ColumnType::INT16 ||
                t == bcsv::ColumnType::INT32);
    reader.close();
}

TEST_F(CastTest, DoubleToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.0, 1.0, 0.0, 1.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(CastTest, DoubleWithNaN) {
    auto path    = fs::path(test_dir_) / "data.bcsv";
    auto nan_val = std::numeric_limits<double>::quiet_NaN();
    writeDoubleFile(path.string(), {1.0, nan_val, 3.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    // NaN excluded from integer narrowing; narrows to FLOAT or stays DOUBLE
    auto t = reader.layout().columnType(0);
    EXPECT_TRUE(t == bcsv::ColumnType::FLOAT ||
                t == bcsv::ColumnType::DOUBLE)
        << "got type " << static_cast<int>(t);
    reader.close();
}

TEST_F(CastTest, DoubleUnchanged) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1e20, 1e30, 1.23456789123456});

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

// ── String narrowing ──

TEST_F(CastTest, StringToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "42", "-5", "100", "-99"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"--string-to-value", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    auto t = reader.layout().columnType(0);
    EXPECT_TRUE(t == bcsv::ColumnType::INT8 ||
                t == bcsv::ColumnType::INT16 ||
                t == bcsv::ColumnType::INT32);
    reader.close();
}

TEST_F(CastTest, StringToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"0", "1", "0", "1"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"--string-to-value", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(CastTest, StringMixedNoConversion) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "abc", "3"});

    std::string sout, serr;
    int         rc = runCast({"--string-to-value", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    // No narrowing possible — string column can't become numeric
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(CastTest, StringWithoutFlag) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "2", "3"});

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    // Without --string-to-value, string columns are not probed
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(CastTest, StringPartial) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"12abc", "34"});

    std::string sout, serr;
    int         rc = runCast({"--string-to-value", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(CastTest, StringLeads) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"007", "001", "010"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"--string-to-value", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

// ── Regression: short output is fully captured (tests runCast harness) ──
// A 1-row BOOL file is already at its narrowest type. The "narrowest" message
// fits in a single 256-byte fgets call. Before the runCast fix, this output
// was silently discarded and stdout_out would be empty.
TEST_F(CastTest, ShortOutputIsCaptured) {
    auto         path = fs::path(test_dir_) / "bool_data.bcsv";
    bcsv::Layout layout;
    layout.addColumn({"flag", bcsv::ColumnType::BOOL});
    bcsv::Writer<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path.string(), true));
    writer.row().set(0, true);
    writer.writeRow();
    writer.close();

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(sout.empty()) << "stdout was not captured at all";
    EXPECT_NE(sout.find("narrowest"), std::string::npos)
        << "Expected 'narrowest' in output; got: " << sout;
}

// ── Regression: output == input must be rejected (suggest --in-place) ──
// Without the same-path guard, bcsvCast opens the input for reading
// while simultaneously truncating and writing to the same path, silently
// corrupting the file. The error should point the user at --in-place.
TEST_F(CastTest, ConvertSameInputOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});

    std::string sout, serr;
    int         rc = runCast({"-o", path.string(), path.string()},
                               sout, serr);
    EXPECT_NE(rc, 0) << "Must reject output == input to prevent corruption";
    EXPECT_NE(sout.find("--in-place"), std::string::npos)
        << "Error should suggest --in-place; got: " << sout;

    // Verify the original file was not corrupted
    bcsv::Reader<bcsv::Layout> reader;
    EXPECT_TRUE(reader.open(path.string())) << "Input file was corrupted";
    if (reader.open(path.string()))
        reader.close();
}

// ── Empty file ──
// An empty BCSV file (0 rows) can be opened by bcsv::Reader; the tool should
// complete gracefully with rc=0 and report 0 rows / all columns at narrowest.
TEST_F(CastTest, EmptyFileGracefulError) {
    auto         path = fs::path(test_dir_) / "data.bcsv";
    bcsv::Layout layout;
    layout.addColumn({"value", bcsv::ColumnType::INT64});
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open(path.string(), true);
    writer.close();

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    // Empty files are opened successfully; 0 rows means no narrowing
    EXPECT_EQ(rc, 0);
}

// ── Mixed file ──

TEST_F(CastTest, MixedColumns) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeMixedFile(path.string(),
                   {"id", "val", "flag"},
                   {bcsv::ColumnType::INT64, bcsv::ColumnType::DOUBLE,
                    bcsv::ColumnType::INT64},
                   {bcsv::ValueType(int64_t(50)),
                    bcsv::ValueType(double(0.0)),
                    bcsv::ValueType(int64_t(1))});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    EXPECT_TRUE(reader.layout().columnType(1) == bcsv::ColumnType::BOOL ||
                reader.layout().columnType(1) == bcsv::ColumnType::UINT8);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::BOOL);
    reader.close();
}

// ── Signed zero ──

TEST_F(CastTest, SignedZero) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {-0.0, 0.0, 1.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

// ── Value correctness after conversion ──

TEST_F(CastTest, RoundTripIntToUint8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 50, 128, 254, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);

    std::vector<uint8_t> expected = {0, 50, 128, 254, 255};
    size_t               idx      = 0;
    while (reader.readNext()) {
        uint8_t val;
        ASSERT_TRUE(reader.row().get<uint8_t>(0, val));
        EXPECT_EQ(val, expected[idx]);
        ++idx;
    }
    EXPECT_EQ(idx, 5);
    reader.close();
}

TEST_F(CastTest, RoundTripStringToInt) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"-5", "0", "10"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"--string-to-value", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));

    int8_t expected[] = {-5, 0, 10};
    size_t idx        = 0;
    while (reader.readNext()) {
        int8_t val;
        ASSERT_TRUE(reader.row().get<int8_t>(0, val));
        EXPECT_EQ(val, expected[idx]);
        ++idx;
    }
    EXPECT_EQ(idx, 3);
    reader.close();
}

// ── CLI behavior ──

TEST_F(CastTest, HelpFlag) {
    std::string sout, serr;
    int         rc = runCast({"--help"}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(sout.find("bcsvCast"), std::string::npos);
}

TEST_F(CastTest, NoArgs) {
    std::string sout, serr;
    int         rc = runCast({}, sout, serr);
    EXPECT_NE(rc, 0);
}

// Removed flags (--convert / --analyze / -f) must be rejected as unknown options.
TEST_F(CastTest, RemovedConvertFlagRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});

    std::string sout, serr;
    EXPECT_NE(runCast({"--convert", path.string()}, sout, serr), 0);
    sout.clear();
    EXPECT_NE(runCast({"--analyze", path.string()}, sout, serr), 0);
    sout.clear();
    EXPECT_NE(runCast({"-f", path.string()}, sout, serr), 0);
}

TEST_F(CastTest, NonexistentFile) {
    std::string sout, serr;
    int         rc = runCast({"/nonexistent/path/data.bcsv"}, sout, serr);
    EXPECT_NE(rc, 0);
}

// ── Regression: INT64 → UINT64 no-narrow (zero savings, pure risk) ──

TEST_F(CastTest, Int64NoSignednessFlipToUint64) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // All-positive int64 values that exceed UINT32_MAX but need INT64
    writeInt64File(path.string(), {5000000000LL, 9000000000000000001LL, 9000000000000000003LL});

    // Analyze mode: column should remain INT64 (no flip to UINT64)
    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    // Should report "narrowest" since INT64→UINT64 is same-width and blocked
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
    // Should NOT show UINT64 in optimal column
    EXPECT_EQ(sout.find("UINT64"), std::string::npos);
}

// ── Regression: int64 values above 2^53 via coerce value ──

TEST_F(CastTest, Int64ToUint32BigValues) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // Values that legitimately fit in UINT32 (none above 2^32-1)
    // Use boundary values to ensure precision is preserved
    writeInt64File(path.string(), {0, 1, 65535, UINT32_MAX, 1000000000LL});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT32);

    uint32_t expected[] = {0, 1, 65535, UINT32_MAX, 1000000000U};
    size_t   idx        = 0;
    while (reader.readNext()) {
        uint32_t val;
        ASSERT_TRUE(reader.row().get<uint32_t>(0, val));
        EXPECT_EQ(val, expected[idx]) << "row " << idx;
        ++idx;
    }
    EXPECT_EQ(idx, 5);
    reader.close();
}

// ── New CLI: positional output, --in-place, --overwrite ──

// Write a 3-column INT64 file: col0 in {0,1}, col1 in {0,50000}, col2 wide.
static void writeThreeColFile(const std::string& path) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT64});
    layout.addColumn({"b", bcsv::ColumnType::INT64});
    layout.addColumn({"c", bcsv::ColumnType::INT64});
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open(path, true);
    const int64_t a[]  = {0, 1};
    const int64_t b[]  = {0, 50000};
    const int64_t c[]  = {INT64_MIN, INT64_MAX};
    for (int r = 0; r < 2; ++r) {
        writer.row().set(0, a[r]);
        writer.row().set(1, b[r]);
        writer.row().set(2, c[r]);
        writer.writeRow();
    }
    writer.close();
}

TEST_F(CastTest, PositionalConvert) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Two positionals → convert, no flags required.
    int rc = runCast({path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(CastTest, OutputAliasStillWorks) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(out));
}

TEST_F(CastTest, InPlaceConvert) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    std::string sout, serr;
    int         rc = runCast({"--in-place", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(CastTest, InPlaceRejectsOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});
    auto out = path.parent_path() / "out.bcsv";

    std::string sout, serr;
    int         rc = runCast({"--in-place", path.string(), out.string()}, sout, serr);
    EXPECT_EQ(rc, 2) << "Expected arg error when --in-place is combined with an output";
}

TEST_F(CastTest, OverwriteRequiredForExistingOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto out = path.parent_path() / "out.bcsv";
    // Pre-create the output file.
    writeInt64File(out.string(), {7});

    std::string sout, serr;
    int         rc = runCast({path.string(), out.string()}, sout, serr);
    EXPECT_NE(rc, 0) << "Existing output without --overwrite must fail";
    EXPECT_NE(sout.find("--overwrite"), std::string::npos)
        << "Error should suggest --overwrite; got: " << sout;

    // With --overwrite it succeeds.
    sout.clear();
    rc = runCast({"--overwrite", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

// ── New CLI: --cols column selection ──

TEST_F(CastTest, ColsRestrictConvertSingle) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Only narrow column 0; columns 1 and 2 stay at original INT64.
    int rc = runCast({"--cols", "0", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::INT64);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(CastTest, ColsRestrictConvertList) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Narrow columns 0 and 1; column 2 stays INT64.
    int rc = runCast({"--cols", "0,1", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::UINT16);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(CastTest, ColsNegativeIndex) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // -2 → index 1 (the narrowable middle column); columns 0 and 2 untouched.
    int rc = runCast({"--cols", "-2", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT64);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::UINT16);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(CastTest, ColsAnalyzeRestricts) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    std::string sout, serr;
    int         rc = runCast({"--cols", "0", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    // Only one column is in scope.
    EXPECT_NE(sout.find("Columns: 1"), std::string::npos)
        << "Expected 'Columns: 1' with --cols 0; got: " << sout;
}

TEST_F(CastTest, ColsOutOfRange) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    std::string sout, serr;
    int         rc = runCast({"--cols", "99", path.string()}, sout, serr);
    EXPECT_EQ(rc, 2) << "Out-of-range column selection must be an arg error";
}

// ── Regression: string decimals that don't survive float32 must stay DOUBLE ──
// (Bug: the string path skipped the double→float round-trip check and narrowed
//  "0.1" to FLOAT, corrupting it to 0.10000000149...)
TEST_F(CastTest, StringDecimalKeepsDoubleNotFloat) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"0.1", "0.2", "3.14159265358979"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"--string-to-value", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::DOUBLE);

    double expected[] = {0.1, 0.2, 3.14159265358979};
    size_t idx        = 0;
    while (reader.readNext()) {
        double val;
        ASSERT_TRUE(reader.row().get<double>(0, val));
        EXPECT_DOUBLE_EQ(val, expected[idx]);  // lossless vs the parsed double
        ++idx;
    }
    EXPECT_EQ(idx, 3u);
    reader.close();
}

// ── Regression: a signed column must NOT flip to same-width unsigned (0 bytes) ──
TEST_F(CastTest, Int32NonNegativeNoUnsignedFlip) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::INT32});
        bcsv::Writer<bcsv::Layout> writer(layout);
        writer.open(path.string(), true);
        for (int32_t v : {0, 1000000, 500, 42}) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    std::string sout, serr;
    int         rc = runCast({path.string()}, sout, serr);  // analyze only
    ASSERT_EQ(rc, 0) << sout;
    // INT32 -> UINT32 is a 0-byte lateral flip and must be suppressed.
    EXPECT_NE(sout.find("narrowest"), std::string::npos) << sout;
    EXPECT_EQ(sout.find("uint32"), std::string::npos) << sout;
}

// ── Regression: convert must preserve the source packet/block size ──
TEST_F(CastTest, PreservesPacketSize) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    {
        bcsv::Layout layout;
        layout.addColumn({"value", bcsv::ColumnType::INT64});
        bcsv::Writer<bcsv::Layout> writer(layout);
        // Non-default 256 KB packet size; values narrow INT64 -> INT8.
        writer.open(path.string(), true, 1, 256);
        for (int64_t v : {-128, 0, 127}) {
            writer.row().set(0, v);
            writer.writeRow();
        }
        writer.close();
    }

    uint32_t in_packet = 0;
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path.string()));
        in_packet = reader.fileHeader().getPacketSize();
        reader.close();
    }
    ASSERT_EQ(in_packet, 256u * 1024u);

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runCast({"-o", out.string(), path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT8);
    // Preserved, not reset to DEFAULT_PACKET_SIZE_KB.
    EXPECT_EQ(reader.fileHeader().getPacketSize(), in_packet);
    reader.close();
}

// ── Mode / argument conflict matrix (spec §3.2) ──

TEST_F(CastTest, TwoModesRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    std::string sout, serr;
    EXPECT_EQ(runCast({"--scan", "--optimize", path.string()}, sout, serr), 2);
}

TEST_F(CastTest, ScanWithOutputRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    EXPECT_EQ(runCast({"--scan", path.string(), out.string()}, sout, serr), 2);
    sout.clear();
    EXPECT_EQ(runCast({"--scan", "-o", out.string(), path.string()}, sout, serr), 2);
}

TEST_F(CastTest, ColsWithStaticRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    EXPECT_EQ(runCast({"--cols", "0", "--static", "0=int8",
                         path.string(), out.string()},
                        sout, serr),
              2);
}

TEST_F(CastTest, ModeMissingSpecRejected) {
    std::string sout, serr;
    EXPECT_EQ(runCast({"--static"}, sout, serr), 2);
    sout.clear();
    EXPECT_EQ(runCast({"--dynamic"}, sout, serr), 2);
}

TEST_F(CastTest, PositionalWithEqualsRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    std::string sout, serr;
    // A brace-expanded SPEC leaking into a positional must be caught (exit 2).
    EXPECT_EQ(runCast({path.string(), "0=int8"}, sout, serr), 2);
}

TEST_F(CastTest, ToleranceInvalidRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    std::string sout, serr;
    EXPECT_EQ(runCast({"--tolerance", "abc", path.string()}, sout, serr), 2);
    sout.clear();
    EXPECT_EQ(runCast({"--tolerance", "-1", path.string()}, sout, serr), 2);
}

TEST_F(CastTest, ExplicitNarrowDryRunReports) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});  // narrowable to uint8
    std::string sout, serr;
    // --narrow with no output → dry-run report (exit 0), no file written.
    int rc = runCast({"--optimize", path.string()}, sout, serr);
    EXPECT_EQ(rc, 0) << sout;
    EXPECT_NE(sout.find("uint8"), std::string::npos) << sout;
}

// ══ Explicit modes: --static / --dynamic (spec §4.3–4.4, §9–§10) ══

TEST_F(CastTest, StaticWidenInt) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {5, 10, 20});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    int32_t exp[] = {5, 10, 20};
    size_t  i = 0;
    while (reader.readNext()) { int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, DynamicSkipsFractional) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1.5, 2.5});  // not whole → lossy to int
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--dynamic", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::DOUBLE);  // kept
    reader.close();
}

TEST_F(CastTest, DynamicAppliesWholeDoubles) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {2.0, 3.0, 4.0});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--dynamic", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    reader.close();
}

TEST_F(CastTest, StaticClampHighAndRound) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {300.0, 3.7, -5.0});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int8", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT8);
    int8_t exp[] = {127, 4, -5};   // 300→clamp 127, 3.7→round 4, -5 fits
    size_t i = 0;
    while (reader.readNext()) { int8_t v; ASSERT_TRUE(reader.row().get<int8_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticNegToUnsignedClampsZero) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {-5, 7});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=uint8", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    uint8_t exp[] = {0, 7};
    size_t  i = 0;
    while (reader.readNext()) { uint8_t v; ASSERT_TRUE(reader.row().get<uint8_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticNaNToZeroNoUB) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {std::numeric_limits<double>::quiet_NaN(), 5.0});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    int32_t exp[] = {0, 5};
    size_t  i = 0;
    while (reader.readNext()) { int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticHugeDoubleClampsInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1e300});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    reader.readNext();
    int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v));
    EXPECT_EQ(v, INT32_MAX);
    reader.close();
}

TEST_F(CastTest, Static2Pow63ToInt64NoUB) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // 2^63 exactly: must clamp to INT64_MAX, not overflow (UB).
    writeDoubleFile(path.string(), {9223372036854775808.0});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int64", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    reader.readNext();
    int64_t v; ASSERT_TRUE(reader.row().get<int64_t>(0, v));
    EXPECT_EQ(v, INT64_MAX);
    reader.close();
}

TEST_F(CastTest, NumericToStringRoundTripFidelity) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {3.141592653589793, 0.1});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=string", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::STRING);
    std::vector<std::string> got;
    while (reader.readNext()) { std::string s; ASSERT_TRUE(reader.row().get<std::string>(0, s)); got.push_back(s); }
    reader.close();
    ASSERT_EQ(got.size(), 2u);
    // shortest round-trip: parsing back must reproduce the exact double.
    EXPECT_EQ(std::strtod(got[0].c_str(), nullptr), 3.141592653589793);
    EXPECT_EQ(std::strtod(got[1].c_str(), nullptr), 0.1);
}

TEST_F(CastTest, DynamicFloatToBoolSkipsNonBinary) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {2.0, 0.0});  // 2.0 not in {0,1} → lossy
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--dynamic", "0=bool", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::DOUBLE);  // kept
    reader.close();
}

TEST_F(CastTest, StaticStringToIntParses) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"5", "-10", "42"});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    int32_t exp[] = {5, -10, 42};
    size_t  i = 0;
    while (reader.readNext()) { int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticStringUnparseableHardError) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"abc"});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    EXPECT_EQ(rc, 1) << "forcing a non-numeric string must be a hard error";
}

// --optimize honors --tolerance: values within tol of an integer narrow to int
// (and rounding applies); with the default tol=0 the same column stays double.
TEST_F(CastTest, OptimizeToleranceNarrowsFloatToInt) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {2.0000001, 3.0000001, 250.0000001});

    // Control: tol=0 (default) → not whole → stays double.
    {
        auto        c_out = path.parent_path() / "ctrl.bcsv";
        std::string s, e;
        int         rc = runCast({"--optimize", path.string(), c_out.string()}, s, e);
        ASSERT_EQ(rc, 0) << s;
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(c_out.string()));
        EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::DOUBLE);
        reader.close();
    }

    // With tol=1e-5 → whole-within-tol → narrows to uint8, rounding each value.
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--optimize", "--tolerance", "1e-5",
                        path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    uint8_t exp[] = {2, 3, 250};
    size_t  i = 0;
    while (reader.readNext()) { uint8_t v; ASSERT_TRUE(reader.row().get<uint8_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, ToleranceRoundsWithinEpsilon) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {5.3, 9.8});  // within 0.4 of an integer
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--dynamic", "0=int32", "--tolerance", "0.4",
                        path.string(), out.string()},
                       sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);  // applied within tol
    int32_t exp[] = {5, 10};  // round-to-nearest
    size_t  i = 0;
    while (reader.readNext()) { int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, PositionalListFull) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeMixedFile(path.string(), {"a", "b"},
                   {bcsv::ColumnType::INT64, bcsv::ColumnType::INT64},
                   {bcsv::ValueType(int64_t(5)), bcsv::ValueType(int64_t(9))});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "int32,int16", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::INT16);
    reader.close();
}

TEST_F(CastTest, ApplyAlwaysWritesOnNoOp) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {5, 9});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Requesting the current type is a no-op, but an output must still be written.
    int rc = runCast({"--static", "0=int64", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    EXPECT_TRUE(fs::exists(out)) << "apply-always-writes: no-op must still produce output";
}

TEST_F(CastTest, ScanJsonShape) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});
    std::string sout, serr;
    int rc = runCast({"--scan", "--json", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    EXPECT_NE(sout.find("\"tool\": \"bcsvCast\""), std::string::npos) << sout;
    EXPECT_NE(sout.find("\"num_columns\": 1"), std::string::npos) << sout;
    EXPECT_NE(sout.find("\"suggested_spec\": \"0=uint8\""), std::string::npos) << sout;
    EXPECT_NE(sout.find("\"columns\""), std::string::npos) << sout;
}

TEST_F(CastTest, StaticJsonReportsClamp) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {300, 7});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int8", "--json", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    EXPECT_NE(sout.find("\"applied\": true"), std::string::npos) << sout;
    EXPECT_NE(sout.find("\"clamped_cells\": 1"), std::string::npos) << sout;  // 300→127
}

// ══ Review-round regression tests ══

// Regression for the zeroOf(BOOL) bug: static double→bool with a NaN must not crash;
// NaN≠0 → true (spec §10).
TEST_F(CastTest, StaticNaNToBoolNoCrash) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, 2.0});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=bool", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    bool   exp[] = {true, false, true, true};  // NaN→true, 0→false, 1→true, 2→true
    size_t i = 0;
    while (reader.readNext()) { bool v; ASSERT_TRUE(reader.row().get<bool>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, ToleranceNonFiniteRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    EXPECT_EQ(runCast({"--optimize", "--tolerance", "inf", path.string(), out.string()}, sout, serr), 2);
    sout.clear();
    EXPECT_EQ(runCast({"--optimize", "--tolerance", "nan", path.string(), out.string()}, sout, serr), 2);
}

TEST_F(CastTest, StaticFloatToDoubleLossless) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeFloatFile(path.string(), {1.5f, 2.25f, -3.75f});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=double", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::DOUBLE);
    double exp[] = {1.5, 2.25, -3.75};
    size_t i = 0;
    while (reader.readNext()) { double v; ASSERT_TRUE(reader.row().get<double>(0, v)); EXPECT_DOUBLE_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticFloatToInt8Clamps) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeFloatFile(path.string(), {2.0f, 3.7f, 300.0f});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int8", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    int8_t exp[] = {2, 4, 127};   // 3.7→round 4, 300→clamp 127
    size_t i = 0;
    while (reader.readNext()) { int8_t v; ASSERT_TRUE(reader.row().get<int8_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticBoolToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeBoolFile(path.string(), {true, false, true});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    int32_t exp[] = {1, 0, 1};
    size_t  i = 0;
    while (reader.readNext()) { int32_t v; ASSERT_TRUE(reader.row().get<int32_t>(0, v)); EXPECT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, StaticInt64ToFloatPrecisionLoss) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {16777217});   // 2^24 + 1, not exact in float32
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=float", "--json", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    EXPECT_NE(sout.find("\"clamped_cells\": 1"), std::string::npos) << sout;
}

TEST_F(CastTest, StaticUint64ToInt64Clamps) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeUint64File(path.string(), {static_cast<uint64_t>(INT64_MAX) + 100ULL});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int64", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    reader.readNext();
    int64_t v; ASSERT_TRUE(reader.row().get<int64_t>(0, v));
    EXPECT_EQ(v, INT64_MAX);
    reader.close();
}

TEST_F(CastTest, StaticStringToFloatFidelity) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"0.5", "1.25", "-2.75"});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=float", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::FLOAT);
    float  exp[] = {0.5f, 1.25f, -2.75f};
    size_t i = 0;
    while (reader.readNext()) { float v; ASSERT_TRUE(reader.row().get<float>(0, v)); EXPECT_FLOAT_EQ(v, exp[i++]); }
    reader.close();
}

TEST_F(CastTest, OptimizeToleranceNarrowsDoubleToFloat) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.1, 0.2, 0.3});  // survive float32 within tol, not exactly

    // Control: tol=0 → 0.1 doesn't round-trip through float32 → stays double.
    {
        auto        c = path.parent_path() / "ctrl.bcsv";
        std::string s, e;
        ASSERT_EQ(runCast({"--optimize", path.string(), c.string()}, s, e), 0);
        bcsv::Reader<bcsv::Layout> r; ASSERT_TRUE(r.open(c.string()));
        EXPECT_EQ(r.layout().columnType(0), bcsv::ColumnType::DOUBLE);
        r.close();
    }
    // With tol=1e-6 → double→float round-trip is within tol → narrows to FLOAT.
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    ASSERT_EQ(runCast({"--optimize", "--tolerance", "1e-6", path.string(), out.string()}, sout, serr), 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::FLOAT);
    reader.close();
}

// Bool narrowing is EXACT (ignores tolerance) so it stays consistent with the
// exact coerce→bool at convert time: near-bool doubles must NOT become BOOL.
TEST_F(CastTest, ToleranceBoolCheckStaysExact) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.001, 0.999});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    ASSERT_EQ(runCast({"--optimize", "--tolerance", "0.01", path.string(), out.string()}, sout, serr), 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_NE(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(CastTest, StaticAppliesToZeroRowFile) {
    auto         path = fs::path(test_dir_) / "data.bcsv";
    bcsv::Layout layout;
    layout.addColumn({"v", bcsv::ColumnType::INT64});
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open(path.string(), true);
    writer.close();  // 0 rows
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);  // applied despite 0 rows
    reader.close();
}

TEST_F(CastTest, SuggestedSpecReusableViaStatic) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});
    std::string sout, serr;
    ASSERT_EQ(runCast({"--scan", "--json", path.string()}, sout, serr), 0);
    EXPECT_NE(sout.find("\"suggested_spec\": \"0=uint8\""), std::string::npos) << sout;
    auto        out = path.parent_path() / "out.bcsv";
    std::string s2, e2;
    ASSERT_EQ(runCast({"--static", "0=uint8", path.string(), out.string()}, s2, e2), 0) << s2;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

// --static on a non-numeric string fails fast (dry-run too), before writing anything.
TEST_F(CastTest, StaticUnparseableStringFailsFastDryRun) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"5", "abc", "7"});
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string()}, sout, serr);  // no output → dry-run
    EXPECT_EQ(rc, 1) << sout;
    EXPECT_NE(sout.find("non-numeric"), std::string::npos) << sout;
}

// A failed forced convert must not destroy the pre-existing --overwrite target
// (fail-fast + temp-then-rename).
TEST_F(CastTest, ForceErrorPreservesExistingOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "xyz"});   // "xyz" cannot cast to int
    auto out = path.parent_path() / "out.bcsv";
    writeInt64File(out.string(), {42, 43});          // pre-existing valid output
    std::string sout, serr;
    int rc = runCast({"--static", "0=int32", path.string(), out.string(), "--overwrite"}, sout, serr);
    EXPECT_EQ(rc, 1) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string())) << "existing output was destroyed";
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT64);
    ASSERT_TRUE(reader.readNext());
    int64_t v; ASSERT_TRUE(reader.row().get<int64_t>(0, v));
    EXPECT_EQ(v, 42);
    reader.close();
}

// Inert flag combinations warn instead of silently no-op (runCast merges stderr).
TEST_F(CastTest, InertFlagCombosWarn) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});
    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    runCast({"--in-place", "--overwrite", path.string()}, sout, serr);
    EXPECT_NE(sout.find("--overwrite is ignored"), std::string::npos) << sout;
    sout.clear();
    runCast({"--static", "0=int8", "--string-to-value", path.string(), out.string()}, sout, serr);
    EXPECT_NE(sout.find("--string-to-value is ignored"), std::string::npos) << sout;
}

