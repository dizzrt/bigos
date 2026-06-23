#ifndef _BIGOS_BOOT_HANDOFF_H
#define _BIGOS_BOOT_HANDOFF_H

#include <arch/x86/boot/boot_info.h>

struct BootInfoHeader;

namespace bigos::boot {
    struct EarlyFramebufferView {
        BootOptionalSectionStatus status;
        BootFramebufferMetadata metadata;
    };

    struct EarlyFontAssetView {
        BootOptionalSectionStatus status;
        BootFontAssetMetadata metadata;
    };

    void init_early_handoff_views(const BootInfoHeader *__boot_info) noexcept;
    const EarlyFramebufferView &early_framebuffer() noexcept;
    const EarlyFontAssetView &early_font_asset() noexcept;
}   // namespace bigos::boot

#endif   // _BIGOS_BOOT_HANDOFF_H
