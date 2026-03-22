#pragma once
#include "fixed_types.h"

namespace ParametricDramDirectoryMSI {

    /*--------------------------------------------------------*
     *  Section header equality                               *
     *--------------------------------------------------------*/
    struct CwtSectionHeader
    {
        uint8_t has_4kb_page;
        uint8_t has_2mb_page;
        uint8_t way;

        constexpr CwtSectionHeader(uint8_t _has_4kb_page = 0,
                                   uint8_t _has_2mb_page = 0,
                                   uint8_t _way          = static_cast<uint8_t>(-1))
            : has_4kb_page(_has_4kb_page),
              has_2mb_page(_has_2mb_page),
              way(_way) {}

        /* value-based comparison */
        friend constexpr bool operator==(const CwtSectionHeader& a,
                                         const CwtSectionHeader& b) noexcept
        {
            return a.has_4kb_page == b.has_4kb_page  &&
                   a.has_2mb_page == b.has_2mb_page  &&
                   a.way          == b.way;
        }

        friend constexpr bool operator!=(const CwtSectionHeader& a,
                                         const CwtSectionHeader& b) noexcept
        {
            return !(a == b);
        }
    };

    static constexpr CwtSectionHeader EMPTY_CWT_SECTION_HEADER = CwtSectionHeader(); // Default entry with all fields set to empty

    struct PmdCwtEntry;

    struct CWCRow;  // Forward-declare to break cycles

    struct PmdCwtEntry
    {
        IntPtr tag;
        CwtSectionHeader section_header[64];

        PmdCwtEntry() // Default constructor initializes tag to -1 and section headers to zero
            : tag(static_cast<IntPtr>(-1)) {

            for (int i = 0; i < 64; ++i) {
                section_header[i] = CwtSectionHeader(); // Default constructor initializes to empty
            } 
        }

        PmdCwtEntry(IntPtr _tag) // Constructor that initializes tag and sets section headers to zero
            : tag(_tag)
        {
            for (int i = 0; i < 64; ++i) {
                section_header[i].has_4kb_page = 0;
                section_header[i].has_2mb_page = 0;
                section_header[i].way = static_cast<uint8_t>(-1);
            } 
        }

        CWCRow toCWCRow();

    /*--------------------------------------------------------*
     *  PMD-CWT entry equality                                *
     *--------------------------------------------------------*/        
        friend bool operator==(const PmdCwtEntry& lhs,
                               const PmdCwtEntry& rhs) noexcept
        {
            if (lhs.tag != rhs.tag)
                return false;

            return std::equal(std::begin(lhs.section_header),
                              std::end  (lhs.section_header),
                              std::begin(rhs.section_header));
            // relies on CwtSectionHeader::operator==
        }

        friend bool operator!=(const PmdCwtEntry& lhs,
                               const PmdCwtEntry& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

}
