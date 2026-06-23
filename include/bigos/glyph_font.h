#ifndef _BIGOS_GLYPH_FONT_H
#define _BIGOS_GLYPH_FONT_H

#include <bigos/types.h>

namespace bigos::font {
    enum class GlyphLookupStatus : uint8_t {
        Unavailable,
        InvalidAsset,
        NotFound,
        Found,
    };

    enum class GlyphWidthClass : uint8_t {
        Unknown = 0,
        Half = 1,
        Full = 2,
    };

    struct GlyphBitmap {
        uint32_t codepoint;
        const uint8_t *bitmap;
        uint32_t bitmap_size;
        uint16_t glyph_width;
        uint16_t glyph_height;
        uint16_t cell_width;
        uint16_t cell_height;
        GlyphWidthClass width_class;
    };

    void init_kernel_glyph_lookup() noexcept;
    bool glyph_lookup_available() noexcept;
    GlyphLookupStatus lookup_glyph(uint32_t __codepoint, GlyphBitmap *__out) noexcept;
}   // namespace bigos::font

#endif   // _BIGOS_GLYPH_FONT_H
