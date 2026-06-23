#include <bigos/boot_handoff.h>
#include <bigos/glyph_font.h>
#include <bigos/io.h>
#include <bigos/memory.h>

namespace {
    struct GlyphLookupView {
        bool available;
        bool invalid;
        const uint8_t *payload;
        uint32_t payload_size;
        const BootFontAssetHeader *header;
        const BootFontAssetRangeRecord *ranges;
        const BootFontAssetGlyphRecord *glyphs;
        const uint8_t *bitmaps;
    };

    GlyphLookupView g_view = {};
    constexpr uint64_t GLYPH_FONT_PAGE_SIZE = 4096;

    bool checked_table_end(
        uint32_t offset, uint32_t count, uint32_t record_size, uint32_t payload_size, uint32_t &end) {
        if (offset > payload_size || count > (payload_size - offset) / record_size)
            return false;
        end = offset + count * record_size;
        return true;
    }

    bool valid_width_class(uint16_t width_class) {
        return width_class == BIGOS_BOOT_FONT_WIDTH_CLASS_HALF || width_class == BIGOS_BOOT_FONT_WIDTH_CLASS_FULL;
    }

    bool font_asset_direct_mapped(uint64_t physical_base, uint64_t byte_size) {
        if (byte_size == 0 || physical_base + byte_size <= physical_base)
            return false;
        uint64_t page = physical_base & ~(GLYPH_FONT_PAGE_SIZE - 1);
        const uint64_t last = (physical_base + byte_size - 1) & ~(GLYPH_FONT_PAGE_SIZE - 1);
        while (page <= last) {
            if (!bigos::mm::is_direct_mapped_phys(page, 1))
                return false;
            if (last - page < GLYPH_FONT_PAGE_SIZE)
                break;
            page += GLYPH_FONT_PAGE_SIZE;
        }
        return true;
    }

    uint32_t expected_bitmap_size(const BootFontAssetGlyphRecord &glyph) {
        if (glyph.glyph_height != 16)
            return 0;
        if (glyph.width_class == BIGOS_BOOT_FONT_WIDTH_CLASS_HALF && glyph.glyph_width == 8)
            return 16;
        if (glyph.width_class == BIGOS_BOOT_FONT_WIDTH_CLASS_FULL && glyph.glyph_width == 16)
            return 32;
        return 0;
    }

    bool validate_header(const BootFontAssetMetadata &metadata, const BootFontAssetHeader &header) {
        uint32_t range_table_end = 0;
        uint32_t glyph_table_end = 0;
        if (header.magic != BIGOS_BOOT_FONT_ASSET_MAGIC || header.header_size != sizeof(BootFontAssetHeader))
            return false;
        if (header.format_version != BIGOS_BOOT_FONT_FORMAT_GLYPH_LOOKUP_V1 ||
            header.payload_size != metadata.byte_size)
            return false;
        if (header.format_version != metadata.format_version || header.glyph_count != metadata.glyph_count)
            return false;
        if (header.glyph_width == 0 || header.glyph_height == 0 || header.cell_width == 0 || header.cell_height == 0)
            return false;
        if (header.glyph_width != metadata.glyph_width || header.glyph_height != metadata.glyph_height ||
            header.cell_width != metadata.cell_width || header.cell_height != metadata.cell_height)
            return false;
        if (header.range_count == 0 || header.glyph_count == 0 || header.bitmap_data_size == 0)
            return false;
        if ((header.range_table_offset % alignof(BootFontAssetRangeRecord)) != 0 ||
            (header.glyph_record_offset % alignof(BootFontAssetGlyphRecord)) != 0 ||
            (header.bitmap_data_offset % alignof(uint32_t)) != 0)
            return false;
        if (header.range_table_offset != header.header_size)
            return false;
        if (!checked_table_end(header.range_table_offset, header.range_count, sizeof(BootFontAssetRangeRecord),
                header.payload_size, range_table_end) ||
            range_table_end != header.glyph_record_offset)
            return false;
        if (!checked_table_end(header.glyph_record_offset, header.glyph_count, sizeof(BootFontAssetGlyphRecord),
                header.payload_size, glyph_table_end) ||
            glyph_table_end != header.bitmap_data_offset)
            return false;
        if (header.bitmap_data_offset > header.payload_size ||
            header.bitmap_data_size > header.payload_size - header.bitmap_data_offset)
            return false;
        return header.bitmap_data_offset + header.bitmap_data_size == header.payload_size;
    }

    bool validate_ranges_and_glyphs(const GlyphLookupView &view) {
        uint32_t previous_range_end = 0;
        uint32_t previous_bitmap_end = 0;
        bool have_previous_range = false;
        bool have_previous_glyph = false;
        uint32_t previous_codepoint = 0;
        for (uint32_t i = 0; i < view.header->range_count; i++) {
            const BootFontAssetRangeRecord &range = view.ranges[i];
            if (range.first_codepoint > range.last_codepoint || range.glyph_count == 0 ||
                range.last_codepoint > 0x10ffffu || !valid_width_class(range.width_class) || range.flags != 0)
                return false;
            if (have_previous_range && range.first_codepoint <= previous_range_end)
                return false;
            if (range.first_glyph_index > view.header->glyph_count ||
                range.glyph_count > view.header->glyph_count - range.first_glyph_index)
                return false;
            const uint32_t range_glyph_end = range.first_glyph_index + range.glyph_count;
            for (uint32_t j = range.first_glyph_index; j < range_glyph_end; j++) {
                const BootFontAssetGlyphRecord &glyph = view.glyphs[j];
                if (glyph.codepoint < range.first_codepoint || glyph.codepoint > range.last_codepoint)
                    return false;
                if (glyph.width_class != range.width_class || !valid_width_class(glyph.width_class) || glyph.flags != 0)
                    return false;
                if (glyph.glyph_height != 16 || glyph.cell_height != 16 || glyph.cell_width < glyph.glyph_width)
                    return false;
                if (glyph.bitmap_size != expected_bitmap_size(glyph))
                    return false;
                if (glyph.bitmap_offset > view.header->bitmap_data_size ||
                    glyph.bitmap_size > view.header->bitmap_data_size - glyph.bitmap_offset)
                    return false;
                if (have_previous_glyph && glyph.codepoint <= previous_codepoint)
                    return false;
                if (have_previous_glyph && glyph.bitmap_offset < previous_bitmap_end)
                    return false;
                previous_bitmap_end = glyph.bitmap_offset + glyph.bitmap_size;
                previous_codepoint = glyph.codepoint;
                have_previous_glyph = true;
            }
            previous_range_end = range.last_codepoint;
            have_previous_range = true;
        }
        return have_previous_glyph;
    }

    const BootFontAssetRangeRecord *find_range(uint32_t codepoint) {
        uint32_t left = 0;
        uint32_t right = g_view.header->range_count;
        while (left < right) {
            const uint32_t mid = left + (right - left) / 2;
            const BootFontAssetRangeRecord &range = g_view.ranges[mid];
            if (codepoint < range.first_codepoint) {
                right = mid;
            } else if (codepoint > range.last_codepoint) {
                left = mid + 1;
            } else {
                return &range;
            }
        }
        return nullptr;
    }

    const BootFontAssetGlyphRecord *find_glyph(const BootFontAssetRangeRecord &range, uint32_t codepoint) {
        uint32_t left = range.first_glyph_index;
        uint32_t right = range.first_glyph_index + range.glyph_count;
        while (left < right) {
            const uint32_t mid = left + (right - left) / 2;
            const BootFontAssetGlyphRecord &glyph = g_view.glyphs[mid];
            if (codepoint < glyph.codepoint) {
                right = mid;
            } else if (codepoint > glyph.codepoint) {
                left = mid + 1;
            } else {
                return &glyph;
            }
        }
        return nullptr;
    }
}   // namespace

namespace bigos::font {
    void init_kernel_glyph_lookup() noexcept {
        g_view = {};
        const auto &asset = bigos::boot::early_font_asset();
        if (asset.status != BootOptionalSectionStatus::Valid)
            return;
        if (!font_asset_direct_mapped(asset.metadata.physical_base, asset.metadata.byte_size)) {
            g_view.invalid = true;
            bigos::serial_puts("BIGOS_FONT_LOOKUP unavailable stage=direct_map\n");
            return;
        }
        auto *payload = (const uint8_t *)bigos::mm::phys_to_direct(asset.metadata.physical_base);
        if (payload == nullptr || asset.metadata.byte_size < sizeof(BootFontAssetHeader)) {
            g_view.invalid = true;
            bigos::serial_puts("BIGOS_FONT_LOOKUP unavailable stage=payload\n");
            return;
        }

        GlyphLookupView view = {};
        view.payload = payload;
        view.payload_size = (uint32_t)asset.metadata.byte_size;
        view.header = (const BootFontAssetHeader *)payload;
        if (!validate_header(asset.metadata, *view.header)) {
            g_view.invalid = true;
            bigos::serial_puts("BIGOS_FONT_LOOKUP unavailable stage=header\n");
            return;
        }
        view.ranges = (const BootFontAssetRangeRecord *)(payload + view.header->range_table_offset);
        view.glyphs = (const BootFontAssetGlyphRecord *)(payload + view.header->glyph_record_offset);
        view.bitmaps = payload + view.header->bitmap_data_offset;
        if (!validate_ranges_and_glyphs(view)) {
            g_view.invalid = true;
            bigos::serial_puts("BIGOS_FONT_LOOKUP unavailable stage=tables\n");
            return;
        }
        view.available = true;
        g_view = view;
        bigos::serial_puts("BIGOS_FONT_LOOKUP ready\n");
    }

    bool glyph_lookup_available() noexcept {
        return g_view.available;
    }

    GlyphLookupStatus lookup_glyph(uint32_t __codepoint, GlyphBitmap *__out) noexcept {
        if (!g_view.available)
            return g_view.invalid ? GlyphLookupStatus::InvalidAsset : GlyphLookupStatus::Unavailable;
        if (__codepoint > 0x10ffffu)
            return GlyphLookupStatus::NotFound;
        const BootFontAssetRangeRecord *range = find_range(__codepoint);
        if (range == nullptr)
            return GlyphLookupStatus::NotFound;
        const BootFontAssetGlyphRecord *glyph = find_glyph(*range, __codepoint);
        if (glyph == nullptr)
            return GlyphLookupStatus::NotFound;
        if (__out != nullptr) {
            *__out = {
                glyph->codepoint,
                g_view.bitmaps + glyph->bitmap_offset,
                glyph->bitmap_size,
                glyph->glyph_width,
                glyph->glyph_height,
                glyph->cell_width,
                glyph->cell_height,
                (GlyphWidthClass)glyph->width_class,
            };
        }
        return GlyphLookupStatus::Found;
    }
}   // namespace bigos::font
