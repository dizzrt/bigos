from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/common.lua',
            ROOT / 'xmake/boot_artifacts.lua',
            ROOT / 'xmake/user_package.lua',
            ROOT / 'xmake/runtime.lua',
            ROOT / 'xmake/kernel.lua',
            ROOT / 'xmake/run_targets.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def test_device_registry_is_bounded_and_freestanding_safe() -> None:
    header = read_source('include/bigos/device.h')
    source = read_source('kernel/core/device.cc')
    xmake = read_source('xmake.lua')

    assert 'MAX_DEVICES = 16' in header
    assert 'MAX_DRIVERS = 16' in header
    assert 'DeviceClass' in header
    assert 'DeviceRole' in header
    assert 'DeviceState' in header
    assert 'Status' in header
    assert 'class_interface' in header
    assert 'Registry g_registry = {};' in source
    assert 'Device devices[bigos::device::MAX_DEVICES];' in source
    assert 'DriverDescriptor drivers[bigos::device::MAX_DRIVERS];' in source
    assert 'return Status::NoSpace;' in source
    assert 'return Status::Exists;' in source
    assert '-fno-rtti' in xmake and '-fno-exceptions' in xmake and '-ffreestanding' in xmake
    for token in ('throw ', 'new ', 'dynamic_cast', 'typeid', 'std::'):
        assert token not in source


def test_duplicate_capacity_probe_and_lookup_statuses_are_deterministic() -> None:
    header = read_source('include/bigos/device.h')
    source = read_source('kernel/core/device.cc')

    for token in (
        'InvalidArgument',
        'Exists',
        'NoSpace',
        'NotFound',
        'NotReady',
        'ProbeFailed',
        'UnsupportedContext',
    ):
        assert token in header

    assert 'same_device_identity' in source
    assert 'same_driver_identity' in source
    assert 'device->state = DeviceState::Probing;' in source
    assert 'device->state = DeviceState::ProbeFailed;' in source
    assert 'device->class_interface = nullptr;' in source
    assert 'device->state == DeviceState::ProbeFailed ? Status::ProbeFailed : Status::NotReady' in source
    assert 'context_allows_probe' in source
    assert 'bigos::sched::can_block()' in source


def test_builtin_devices_publish_existing_class_interfaces() -> None:
    source = read_source('kernel/core/device.cc')

    for token in (
        'DeviceRole::BootBlock',
        'DeviceRole::PersistentWritableBlock',
        'DeviceRole::PitTimer',
        'DeviceRole::VgaText',
        'DeviceRole::CmosRtc',
    ):
        assert token in source

    assert 'driver::block::ata_pio_primary_master_init(ata);' in source
    assert 'driver::block::ata_pio_persistent_test_init(ata);' in source
    assert 'return bigos::device::publish(__device, &ata->block);' in source
    assert 'const bigos::device::TimerInterface g_pit_interface' in source
    assert 'const bigos::device::VideoTextInterface g_vga_interface' in source
    assert 'const bigos::device::RtcInterface g_rtc_interface' in source


def test_block_consumers_lookup_framework_devices_without_direct_ata_init() -> None:
    vfs = read_source('kernel/core/fs/vfs.cc')
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'bigos::device::block(bigos::device::DeviceRole::BootBlock)' in vfs
    assert 'find_exfat_partition(boot_device, &partition)' in vfs
    assert 'mount_exfat(boot_device, &partition, &mount)' in vfs
    assert 'mount.device = boot_device;' in vfs
    assert 'ata_pio_primary_master_init' not in vfs

    assert 'bigos::device::block(bigos::device::DeviceRole::PersistentWritableBlock)' in bigfs
    assert 'publish_ram_formatted()' in bigfs
    assert 'return false;' in bigfs[bigfs.index('publish_persistent_if_valid') : bigfs.index('publish_ram_formatted')]
    assert 'g_persistent = false;' in bigfs
    assert 'ata_pio_persistent_test_init' not in bigfs


def test_timer_video_and_rtc_normal_paths_use_published_wrappers() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    isr = read_source('kernel/core/irq/isr.cc')
    io = read_source('kernel/core/bigos/io.cc')
    console = read_source('kernel/core/terminal/console.cc')
    time = read_source('kernel/core/time/time.cc')

    mem_index = kernel.index('bigos::init_mem(boot_info);')
    device_init_index = kernel.index('bigos::device::init();')
    probe_index = kernel.index('bigos::device::probe_all(bigos::device::ProbeContext::KernelInit);')
    tty_index = kernel.index('bigos::terminal::init_tty();')
    irq_index = kernel.index('bigos::irq::initIRQ();')

    assert mem_index < device_init_index < probe_index < tty_index < irq_index
    assert 'void clear_video_text() noexcept' in read_source('kernel/core/device.cc')
    assert 'bigos::device::init_pit_timer();' in isr
    assert 'driver::timer::pit::init_channel0();' not in isr
    assert 'bigos::device::write_video_text(c);' in io
    assert 'bigos::device::write_video_text(ch);' in console
    assert 'bigos::device::read_rtc_time(&dt)' in time


def test_internal_device_roles_do_not_escape_user_abi() -> None:
    syscall_h = read_source('include/bigos/syscall.h')
    user_sources = '\n'.join(
        path.read_text(encoding='utf-8')
        for path in (ROOT / 'user').rglob('*')
        if path.is_file() and path.suffix in {'.c', '.h', '.s', '.S'}
    )

    for token in ('BootBlock', 'PersistentWritableBlock', 'PitTimer', 'VgaText', 'CmosRtc', 'DeviceRole'):
        assert token not in syscall_h
        assert token not in user_sources
