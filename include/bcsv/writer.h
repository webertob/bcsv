#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <type_traits>

#include "definitions.h"
#include "column_layout.h"
#include "compression.h"
#include "file_header.h"
#include "row.h"

namespace bcsv {

    /**
     * @brief Template class for writing BCSV files
     */
    template<typename StreamType = std::ofstream>
    class Writer {
    public:
        Writer() = default;
        explicit Writer(const std::string& filename);
        explicit Writer(StreamType&& stream);
        
        bool open(const std::string& filename);
        void close();
        
        void setCompression(bool enabled);
        bool isCompressionEnabled() const;
        
        bool writeFileHeader(const FileHeader& fileHeader);
        bool writeColumnLayout(const ColumnLayout& columnLayout);
        bool writeRow(const Row& row);
        
        void flush();

    private:
        bool compressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& compressed);
        
        Compressor compressor_;
        std::vector<char> buffer_raw_;
        std::vector<char> buffer_zip_;

        StreamType stream_;
        ColumnLayout columnLayout_;
        bool compressionEnabled_ = false;
        bool fileHeaderWritten_ = false;
        bool headerWritten_ = false;
        size_t rowCount_ = 0;

    };

} // namespace bcsv
