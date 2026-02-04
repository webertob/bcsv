#pragma once
/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace bcsv {
    
    /* StringAddress template, helps with packing and unpacking string payload address and length into fixed column sections. */
    template<typename AddrType = uint32_t>
    class StrAddrT {
        private:
            AddrType                    packed_;     //  (this is the only stored value in this class! --> you can directly serialize this)
            static constexpr AddrType   OFFSET_SHIFT = sizeof(AddrType) * 4;                           // upper half bits
            static constexpr AddrType   OFFSET_MASK  = (static_cast<AddrType>(1) << OFFSET_SHIFT) - 1; // lower half bits
            static constexpr AddrType   LENGTH_MASK  = (static_cast<AddrType>(1) << OFFSET_SHIFT) - 1; // lower half bits
            
                    
        public:
            static constexpr size_t     MAX_STRING_LENGTH = LENGTH_MASK;                               // Maximum string length that can be represented (depends on AddrType size)

                                        StrAddrT    ()                              : packed_(0)                    {}
                                        StrAddrT    (const AddrType& packed)        : packed_(packed)               {}
                                        StrAddrT    (size_t offset, size_t length_will_truncate) : packed_(pack(offset, length_will_truncate)) {}
            const AddrType&             packed      () const { return packed_; }
            size_t                      offset      () const { return static_cast<size_t>(packed_ >> OFFSET_SHIFT);}
            size_t                      length      () const { return static_cast<size_t>(packed_ & LENGTH_MASK);}
            std::pair<size_t, size_t>   unpack      () const { return {offset(), length()}; }

            static AddrType             pack        (size_t offset, size_t length_will_truncate);
            static void                 unpack      (AddrType packed, size_t& offset, size_t& length);
    };

    // Implementation of pack and unpack methods
    template<typename AddrType>
    AddrType StrAddrT<AddrType>::pack(size_t offset, size_t length_will_truncate) {
        if (offset > OFFSET_MASK) {
            throw std::overflow_error("StrAddrT::pack() Offset too large");
        }
        return ((static_cast<AddrType>(offset) & OFFSET_MASK) << OFFSET_SHIFT) | (static_cast<AddrType>(length_will_truncate) & LENGTH_MASK);
    }
    
    template<typename AddrType>
    void StrAddrT<AddrType>::unpack(AddrType packed, size_t& offset, size_t& length) {
        offset = static_cast<size_t>((packed >> OFFSET_SHIFT) & OFFSET_MASK);
        length = static_cast<size_t>(packed & LENGTH_MASK);
    }

    using StrAddr16_t = StrAddrT<uint16_t>; // 2 bytes address type
    using StrAddr32_t = StrAddrT<uint32_t>; // 4 bytes address type
    using StrAddr64_t = StrAddrT<uint64_t>; // 8 bytes
} // namespace bcsv