from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (PROJECT_ROOT / path).read_text()


def test_bootinfo_framebuffer_and_font_sections_are_versioned_and_checked() -> None:
    boot_info = read_source('include/arch/x86/boot/boot_info.h')

    assert '#define BIGOS_BOOT_SECTION_TYPE_FRAMEBUFFER_METADATA 5u' in boot_info
    assert '#define BIGOS_BOOT_SECTION_TYPE_FONT_ASSET_METADATA  6u' in boot_info
    assert 'struct BootFramebufferMetadata' in boot_info
    assert 'struct BootFontAssetMetadata' in boot_info
    assert 'enum class BootOptionalSectionStatus' in boot_info
    assert 'bigos_boot_info_v2_framebuffer_metadata' in boot_info
    assert 'bigos_boot_info_v2_font_asset_metadata' in boot_info
    assert 'static_assert(sizeof(BootFramebufferMetadata) == 40)' in boot_info
    assert 'static_assert(sizeof(BootFontAssetMetadata) == 40)' in boot_info
    assert 'BIGOS_BOOT_INFO_V2_MAGIC' in boot_info
    assert 'static_assert(sizeof(BootInfoHeader) == 24)' in boot_info
    assert 'static_assert(sizeof(BootInfo) == BIGOS_BOOT_INFO_SIZE)' in boot_info


def test_uefi_loader_collects_gop_and_font_without_claiming_console_ready() -> None:
    loader = read_source('kernel/arch/x86/uefi/loader.cc')
    uefi = read_source('kernel/arch/x86/uefi/uefi.h')
    boot_debug = read_source('tools/boot_debug.py')

    assert 'EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID' in uefi
    assert 'struct EFI_GRAPHICS_OUTPUT_PROTOCOL' in uefi
    assert 'prepare_framebuffer_handoff()' in loader
    assert 'locate_protocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID' in loader
    assert 'normalize_gop_pixel_format' in loader
    assert 'BIGOS_UEFI_FRAMEBUFFER' in loader
    assert 'BIGOS_UEFI_FONT' in loader
    assert 'u"\\\\boot\\\\fonts\\\\unifont.bin"' in loader
    assert 'BIGOS_BOOT_SECTION_TYPE_FRAMEBUFFER_METADATA' in loader
    assert 'BIGOS_BOOT_SECTION_TYPE_FONT_ASSET_METADATA' in loader
    assert "FONT_SOURCE = PROJECT_ROOT / 'assets' / 'fonts' / 'unifont_all-17.0.04.hex'" in boot_debug
    assert "FONT_ASSET = BUILD_DIR / 'assets' / 'fonts' / 'unifont.bin'" in boot_debug
    assert "FONT_ASSET_PATH = '/boot/fonts/unifont.bin'" in boot_debug
    assert 'generate_boot_font_asset(FONT_SOURCE, FONT_ASSET)' in boot_debug
    assert 'mtools_copy_in(image_path, artifacts.font_asset, FONT_ASSET_PATH)' in boot_debug


def test_kernel_consumes_optional_metadata_without_framebuffer_direct_map_alias() -> None:
    handoff = read_source('kernel/mm/boot_handoff.cc')
    buddy = read_source('kernel/mm/buddy.cc')
    vmem = read_source('kernel/mm/vmem.cc')
    memory = read_source('include/bigos/memory.h')

    assert 'init_early_handoff_views' in handoff
    assert 'BootOptionalSectionStatus::Valid' in handoff
    assert 'consume_region_excluding_framebuffer' in buddy
    assert 'bigos_boot_info_v2_framebuffer_metadata(header)' in buddy
    assert 'init_direct_map_from_region_excluding_framebuffer' in vmem
    assert 'bigos_boot_info_v2_framebuffer_metadata(__header)' in vmem
    assert 'DeviceMmioMapping map_device_mmio' in memory
    assert 'KDEVICE_MMIO_BASE' in vmem
    assert 'map_page(virt, phys, page_attr::PRESENT | page_attr::WRITABLE | page_attr::NO_EXECUTE)' in vmem
    assert 'return (void *)(KDIRECT_BASE + __phys);' in vmem
