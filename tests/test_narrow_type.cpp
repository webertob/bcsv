/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <bcsv/bcsv.h>

namespace fs = std::filesystem;

// Find the bcsvNarrowType binary from common build locations
static std::string findNarrowBin() {
    fs::path cur = fs::current_path();

    std::string candidates[] = {
        (cur / "bin" / "bcsvNarrowType").string(),
        (cur / "../bin" / "bcsvNarrowType").string(),
        (cur / "bcsvNarrowType").string(),
        "build/ninja-debug/bin/bcsvNarrowType",
        "build/ninja-release/bin/bcsvNarrowType",
    };

    for (auto& c : candidates) {
        fs::path p(c);
        if (fs::exists(p) && fs::is_regular_file(p))
            return p.string();
    }

    return (cur / "bin" / "bcsvNarrowType").string();
}

class NarrowTypeTest : public ::testing::Test {
protected:
    std::string test_dir_;
    std::string narrow_bin_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_        = (fs::path("test_temp") /
                            (std::string(info->test_suite_name()) + "_" +
                             info->name()))
                               .string();
        fs::create_directories(test_dir_);
        narrow_bin_ = findNarrowBin();
    }

    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    int runNarrow(const std::vector<std::string>& args,
                  std::string&                    stdout_out, std::string& /*stderr*/) {
        std::string cmd = "\"" + narrow_bin_ + "\"";
        for (auto& a : args)
            cmd += " " + a;

        std::string merged = cmd + " 2>&1";
        FILE*       pipe   = popen(merged.c_str(), "r");
        if (!pipe)
            return -1;

        char buf[256];
        while (std::fgets(buf, sizeof(buf), pipe))
            stdout_out += buf;
        int rc = pclose(pipe);
        return WEXITSTATUS(rc);
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

TEST_F(NarrowTypeTest, Int64ToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 0, 1});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(NarrowTypeTest, Int64ToUint8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(NarrowTypeTest, Int64ToUint16) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 50000});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT16);
    reader.close();
}

TEST_F(NarrowTypeTest, Int64ToInt8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {-128, 0, 127});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT8);
    reader.close();
}

TEST_F(NarrowTypeTest, Int64ToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {INT32_MIN, 0, INT32_MAX});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT32);
    reader.close();
}

TEST_F(NarrowTypeTest, Int64Unchanged) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {INT64_MIN, INT64_MAX});

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

// CRITICAL: verify signed ladder checks both min and max (regression test)
TEST_F(NarrowTypeTest, DoubleWithNegativeToSigned) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // {-2000.0, 5.0} should NOT narrow to INT8 (would overflow -2000)
    writeDoubleFile(path.string(), {-2000.0, 5.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()}, sout, serr);
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

TEST_F(NarrowTypeTest, Uint64LargeValues) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeUint64File(path.string(), {UINT32_MAX + 1, UINT64_MAX});

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    // Verify analyze shows unchanged
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(NarrowTypeTest, Uint64ToUint32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeUint64File(path.string(), {0, 1000000, UINT32_MAX});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT32);
    reader.close();
}

// ── Double narrowing ──

TEST_F(NarrowTypeTest, DoubleToFloat) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1.0, 0.5, 3.14f, 100.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    auto t = reader.layout().columnType(0);
    EXPECT_TRUE(t == bcsv::ColumnType::FLOAT ||
                t == bcsv::ColumnType::UINT32);
    reader.close();
}

TEST_F(NarrowTypeTest, DoubleToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.0, 50.0, 100.0, -10.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
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

TEST_F(NarrowTypeTest, DoubleToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {0.0, 1.0, 0.0, 1.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(NarrowTypeTest, DoubleWithNaN) {
    auto path    = fs::path(test_dir_) / "data.bcsv";
    auto nan_val = std::numeric_limits<double>::quiet_NaN();
    writeDoubleFile(path.string(), {1.0, nan_val, 3.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
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

TEST_F(NarrowTypeTest, DoubleUnchanged) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {1e20, 1e30, 1.23456789123456});

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

// ── String narrowing ──

TEST_F(NarrowTypeTest, StringToInt32) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "42", "-5", "100", "-99"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", "-o", out.string(),
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

TEST_F(NarrowTypeTest, StringToBool) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"0", "1", "0", "1"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

TEST_F(NarrowTypeTest, StringMixedNoConversion) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "abc", "3"});

    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    // No narrowing possible — string column can't become numeric
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(NarrowTypeTest, StringWithoutFlag) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"1", "2", "3"});

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    // Without --stringsToValue, string columns are not probed
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(NarrowTypeTest, StringPartial) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"12abc", "34"});

    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
}

TEST_F(NarrowTypeTest, StringLeads) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"007", "001", "010"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", "-o", out.string(),
                                path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

// ── Regression: short output is fully captured (tests runNarrow harness) ──
// A 1-row BOOL file is already at its narrowest type. The "narrowest" message
// fits in a single 256-byte fgets call. Before the runNarrow fix, this output
// was silently discarded and stdout_out would be empty.
TEST_F(NarrowTypeTest, ShortOutputIsCaptured) {
    auto         path = fs::path(test_dir_) / "bool_data.bcsv";
    bcsv::Layout layout;
    layout.addColumn({"flag", bcsv::ColumnType::BOOL});
    bcsv::Writer<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path.string(), true));
    writer.row().set(0, true);
    writer.writeRow();
    writer.close();

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(sout.empty()) << "stdout was not captured at all";
    EXPECT_NE(sout.find("narrowest"), std::string::npos)
        << "Expected 'narrowest' in output; got: " << sout;
}

// ── Regression: output == input must be rejected (suggest --in-place) ──
// Without the same-path guard, bcsvNarrowType opens the input for reading
// while simultaneously truncating and writing to the same path, silently
// corrupting the file. The error should point the user at --in-place.
TEST_F(NarrowTypeTest, ConvertSameInputOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});

    std::string sout, serr;
    int         rc = runNarrow({"-o", path.string(), path.string()},
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
TEST_F(NarrowTypeTest, EmptyFileGracefulError) {
    auto         path = fs::path(test_dir_) / "data.bcsv";
    bcsv::Layout layout;
    layout.addColumn({"value", bcsv::ColumnType::INT64});
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open(path.string(), true);
    writer.close();

    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    // Empty files are opened successfully; 0 rows means no narrowing
    EXPECT_EQ(rc, 0);
}

// ── Mixed file ──

TEST_F(NarrowTypeTest, MixedColumns) {
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
    int         rc = runNarrow({"-o", out.string(), path.string()},
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

TEST_F(NarrowTypeTest, SignedZero) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeDoubleFile(path.string(), {-0.0, 0.0, 1.0});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
                               sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    reader.close();
}

// ── Value correctness after conversion ──

TEST_F(NarrowTypeTest, RoundTripIntToUint8) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 50, 128, 254, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()},
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

TEST_F(NarrowTypeTest, RoundTripStringToInt) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeStringFile(path.string(), {"-5", "0", "10"});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"--stringsToValue", "-o", out.string(),
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

TEST_F(NarrowTypeTest, HelpFlag) {
    std::string sout, serr;
    int         rc = runNarrow({"--help"}, sout, serr);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(sout.find("bcsvNarrowType"), std::string::npos);
}

TEST_F(NarrowTypeTest, NoArgs) {
    std::string sout, serr;
    int         rc = runNarrow({}, sout, serr);
    EXPECT_NE(rc, 0);
}

// Removed flags (--convert / --analyze / -f) must be rejected as unknown options.
TEST_F(NarrowTypeTest, RemovedConvertFlagRejected) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 1, 2});

    std::string sout, serr;
    EXPECT_NE(runNarrow({"--convert", path.string()}, sout, serr), 0);
    sout.clear();
    EXPECT_NE(runNarrow({"--analyze", path.string()}, sout, serr), 0);
    sout.clear();
    EXPECT_NE(runNarrow({"-f", path.string()}, sout, serr), 0);
}

TEST_F(NarrowTypeTest, NonexistentFile) {
    std::string sout, serr;
    int         rc = runNarrow({"/nonexistent/path/data.bcsv"}, sout, serr);
    EXPECT_NE(rc, 0);
}

// ── Regression: INT64 → UINT64 no-narrow (zero savings, pure risk) ──

TEST_F(NarrowTypeTest, Int64NoSignednessFlipToUint64) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // All-positive int64 values that exceed UINT32_MAX but need INT64
    writeInt64File(path.string(), {5000000000LL, 9000000000000000001LL, 9000000000000000003LL});

    // Analyze mode: column should remain INT64 (no flip to UINT64)
    std::string sout, serr;
    int         rc = runNarrow({path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    // Should report "narrowest" since INT64→UINT64 is same-width and blocked
    EXPECT_NE(sout.find("narrowest"), std::string::npos);
    // Should NOT show UINT64 in optimal column
    EXPECT_EQ(sout.find("UINT64"), std::string::npos);
}

// ── Regression: int64 values above 2^53 via coerce value ──

TEST_F(NarrowTypeTest, Int64ToUint32BigValues) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    // Values that legitimately fit in UINT32 (none above 2^32-1)
    // Use boundary values to ensure precision is preserved
    writeInt64File(path.string(), {0, 1, 65535, UINT32_MAX, 1000000000LL});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()}, sout, serr);
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

TEST_F(NarrowTypeTest, PositionalConvert) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Two positionals → convert, no flags required.
    int rc = runNarrow({path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(NarrowTypeTest, OutputAliasStillWorks) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    int         rc = runNarrow({"-o", out.string(), path.string()}, sout, serr);
    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(out));
}

TEST_F(NarrowTypeTest, InPlaceConvert) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    std::string sout, serr;
    int         rc = runNarrow({"--in-place", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

TEST_F(NarrowTypeTest, InPlaceRejectsOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});
    auto out = path.parent_path() / "out.bcsv";

    std::string sout, serr;
    int         rc = runNarrow({"--in-place", path.string(), out.string()}, sout, serr);
    EXPECT_EQ(rc, 2) << "Expected arg error when --in-place is combined with an output";
}

TEST_F(NarrowTypeTest, OverwriteRequiredForExistingOutput) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeInt64File(path.string(), {0, 100, 255});

    auto out = path.parent_path() / "out.bcsv";
    // Pre-create the output file.
    writeInt64File(out.string(), {7});

    std::string sout, serr;
    int         rc = runNarrow({path.string(), out.string()}, sout, serr);
    EXPECT_NE(rc, 0) << "Existing output without --overwrite must fail";
    EXPECT_NE(sout.find("--overwrite"), std::string::npos)
        << "Error should suggest --overwrite; got: " << sout;

    // With --overwrite it succeeds.
    sout.clear();
    rc = runNarrow({"--overwrite", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::UINT8);
    reader.close();
}

// ── New CLI: --cols column selection ──

TEST_F(NarrowTypeTest, ColsRestrictConvertSingle) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Only narrow column 0; columns 1 and 2 stay at original INT64.
    int rc = runNarrow({"--cols", "0", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::INT64);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(NarrowTypeTest, ColsRestrictConvertList) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // Narrow columns 0 and 1; column 2 stays INT64.
    int rc = runNarrow({"--cols", "0,1", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::BOOL);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::UINT16);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(NarrowTypeTest, ColsNegativeIndex) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    auto        out = path.parent_path() / "out.bcsv";
    std::string sout, serr;
    // -2 → index 1 (the narrowable middle column); columns 0 and 2 untouched.
    int rc = runNarrow({"--cols", "-2", path.string(), out.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(out.string()));
    EXPECT_EQ(reader.layout().columnType(0), bcsv::ColumnType::INT64);
    EXPECT_EQ(reader.layout().columnType(1), bcsv::ColumnType::UINT16);
    EXPECT_EQ(reader.layout().columnType(2), bcsv::ColumnType::INT64);
    reader.close();
}

TEST_F(NarrowTypeTest, ColsAnalyzeRestricts) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    std::string sout, serr;
    int         rc = runNarrow({"--cols", "0", path.string()}, sout, serr);
    ASSERT_EQ(rc, 0) << sout;
    // Only one column is in scope.
    EXPECT_NE(sout.find("Columns: 1"), std::string::npos)
        << "Expected 'Columns: 1' with --cols 0; got: " << sout;
}

TEST_F(NarrowTypeTest, ColsOutOfRange) {
    auto path = fs::path(test_dir_) / "data.bcsv";
    writeThreeColFile(path.string());

    std::string sout, serr;
    int         rc = runNarrow({"--cols", "99", path.string()}, sout, serr);
    EXPECT_EQ(rc, 2) << "Out-of-range column selection must be an arg error";
}

