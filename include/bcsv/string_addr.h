/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once
#include <cstdint>
#include <stdexcept>

namespace bcsv {
    /* StringAddress, helps with packing and unpacking string payload address and length into fixed column sections. */
    template<typename AddrType = uint32_t>
    class StringAddr {
        private:
            AddrType                   packed_;     //  (this is the only stored value in this class! --> you can directly serialize this)
            static constexpr AddrType  OFFSET_SHIFT = sizeof(AddrType) * 4;                           // upper half bits
            static constexpr AddrType  OFFSET_MASK  = (static_cast<AddrType>(1) << OFFSET_SHIFT) - 1; // lower half bits
            static constexpr AddrType  LENGTH_MASK  = (static_cast<AddrType>(1) << OFFSET_SHIFT) - 1; // lower half bits
        
        public:
                                        StringAddr  ()                              : packed_(0)                    {}
                                        StringAddr  (const AddrType& packed)        : packed_(packed)               {}
                                        StringAddr  (size_t offset, size_t length)  : packed_(pack(offset, length)) {}
            const AddrType&             packed      () const { return packed_; }
            size_t                      offset      () const { return static_cast<size_t>(packed_ >> OFFSET_SHIFT);}
            size_t                      length      () const { return static_cast<size_t>(packed_ & LENGTH_MASK);}
            std::tuple<size_t, size_t>  unpack      () const { return {offset(), length()}; }

            static AddrType             pack        (size_t offset, size_t length);
            static void                 unpack      (AddrType packed, size_t& offset, size_t& length);
    };

    // Implementation of pack and unpack methods
    template<typename AddrType>
    AddrType StringAddr<AddrType>::pack(size_t offset, size_t length) {
        if (offset > OFFSET_MASK) {
            throw std::overflow_error("StringAddr::pack() Offset too large ");
        }
        if (length > LENGTH_MASK) {
            throw std::overflow_error("StringAddr::pack() Length too large ");
        }
        return ((static_cast<AddrType>(offset) & OFFSET_MASK) << OFFSET_SHIFT) | (static_cast<AddrType>(length) & LENGTH_MASK);
    }
    
    template<typename AddrType>
    void StringAddr<AddrType>::unpack(AddrType packed, size_t& offset, size_t& length) {
        offset = static_cast<size_t>((packed >> OFFSET_SHIFT) & OFFSET_MASK);
        length = static_cast<size_t>(packed & LENGTH_MASK);
    }
} // namespace bcsv