#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <type_traits>

#include "definitions.h"
#include "column_layout.h"
#include "file_header.h"
#include "row.h"

namespace bcsv {

    /**
     * @brief Template class for reading BCSV files
     */
    template<typename StreamType = std::ifstream>
    class Reader {
    public:
        Reader() = default;
        explicit Reader(const std::string& filename);
        explicit Reader(StreamType&& stream);
        
        bool open(const std::string& filename);
        void close();
        
        bool readFileHeader();
        bool readColumnLayout();
        bool readRow(Row& row);
        
        const FileHeader& getFileHeader() const;
        const ColumnLayout& getColumnLayout() const;
        
        bool hasMoreRows() const;
        size_t getCurrentRowIndex() const;
        
        // Iterator support for range-based loops
        class iterator {
        public:
            iterator(Reader* reader, bool end = false);
            Row operator*();
            iterator& operator++();
            bool operator!=(const iterator& other) const;
            
        private:
            Reader* reader_;
            bool end_;
            Row currentRow_;
        };
        
        iterator begin();
        iterator end();
        
    private:
        StreamType stream_;
        FileHeader fileHeader_;
        ColumnLayout columnLayout_;
        size_t currentRowIndex_ = 0;
        bool fileHeaderRead_ = false;
        bool headerRead_ = false;
        
        bool decompressData(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed);
    };

} // namespace bcsv
