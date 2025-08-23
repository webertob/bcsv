#pragma once

#include <vector>
#include <cstdint>

#include "definitions.h"
#include "column_layout.h"

namespace bcsv {

    /**
     * @brief Represents a data packet that can contain column layouts or rows
     */
    class Packet {
    public:
        enum class Type {
            HEADER,
            ROW,
            METADATA
        };

        Packet() = default;
        explicit Packet(Type type) : type_(type) {}
        
        void setType(Type type) { type_ = type; }
        Type getType() const { return type_; }
        
        void setData(const std::vector<uint8_t>& data) { data_ = data; }
        const std::vector<uint8_t>& getData() const { return data_; }
        
        void setCompressed(bool compressed) { compressed_ = compressed; }
        bool isCompressed() const { return compressed_; }

    private:
        Type type_ = Type::ROW;
        std::vector<uint8_t> data_;
        bool compressed_ = false;
    };

} // namespace bcsv
