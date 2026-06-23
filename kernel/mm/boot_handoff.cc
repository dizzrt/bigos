#include <bigos/boot_handoff.h>

namespace {
    bigos::boot::EarlyFramebufferView g_framebuffer = {BootOptionalSectionStatus::Absent, {}};
    bigos::boot::EarlyFontAssetView g_font_asset = {BootOptionalSectionStatus::Absent, {}};
}   // namespace

namespace bigos::boot {
    void init_early_handoff_views(const BootInfoHeader *__boot_info) noexcept {
        g_framebuffer = {BootOptionalSectionStatus::Absent, {}};
        g_font_asset = {BootOptionalSectionStatus::Absent, {}};

        BootHandoff handoff = bigos_boot_resolve_handoff(__boot_info);
        if (handoff.v2 == nullptr)
            return;

        auto framebuffer = bigos_boot_info_v2_framebuffer_metadata(handoff.v2);
        g_framebuffer.status = framebuffer.status;
        if (framebuffer.status == BootOptionalSectionStatus::Valid)
            g_framebuffer.metadata = *framebuffer.payload;

        auto font = bigos_boot_info_v2_font_asset_metadata(handoff.v2);
        g_font_asset.status = font.status;
        if (font.status == BootOptionalSectionStatus::Valid)
            g_font_asset.metadata = *font.payload;
    }

    const EarlyFramebufferView &early_framebuffer() noexcept {
        return g_framebuffer;
    }

    const EarlyFontAssetView &early_font_asset() noexcept {
        return g_font_asset;
    }
}   // namespace bigos::boot
