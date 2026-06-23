from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == 'xmake.lua':
        parts = [
            ROOT / 'xmake.lua',
            ROOT / 'xmake/options.lua',
            ROOT / 'xmake/kernel.lua',
        ]
        return '\n'.join(path.read_text(encoding='utf-8') for path in parts)
    return (ROOT / relative).read_text(encoding='utf-8')


def test_request_layer_defines_bounded_sync_contract() -> None:
    header = read_source('include/bigos/block_io.h')
    source = read_source('kernel/core/block_io.cc')

    assert 'QUEUE_CAPACITY_PER_DEVICE = 8' in header
    assert 'MAX_DEVICE_QUEUES = 8' in header
    assert 'enum class Operation' in header
    assert 'enum class Status' in header
    assert 'InvalidRequest' in header
    assert 'QueueFull' in header
    assert 'DeviceNotReady' in header
    assert 'PendingTimeout' in header
    assert 'CompletionRejected' in header
    assert 'enum class RequestState' in header
    assert 'struct CompletionToken' in header
    assert 'struct Request' in header
    assert 'Status submit_sync(Request *__request) noexcept;' in header
    assert 'Status arm_pending(Request *__request, CompletionToken *__out_token) noexcept;' in header
    assert 'Status wait_pending(Request *__request, timer::tick_t __timeout_ticks = 0) noexcept;' in header
    assert 'Status complete_from_irq(const CompletionToken *__token, Status __final_status) noexcept;' in header
    assert 'Status read_role_sync(device::DeviceRole __role' in header
    assert 'Status write_role_sync(device::DeviceRole __role' in header

    assert 'g_queues' in source
    assert 'validate_request' in source
    assert 'map_device_status' in source
    assert '__queue->active_count >= bigos::block_io::QUEUE_CAPACITY_PER_DEVICE' in source
    assert 'Status::QueueFull' in source
    assert 'driver::block::read_sectors(' in source
    assert 'driver::block::write_sectors(' in source
    assert 'bigos::device::find_interface(bigos::device::DeviceClass::Block, __role, &iface)' in source
    assert 'Status::WouldBlock' in source
    assert 'bigos::sched::can_block()' in source
    assert 'bigos::sched::wake_all(&request->completion_wait)' in source
    assert 'Status::CompletionRejected' in source


def test_interrupt_completion_contract_is_bounded_and_named() -> None:
    header = read_source('include/bigos/block_io.h')
    source = read_source('kernel/core/block_io.cc')

    for token in (
        'Queued',
        'Pending',
        'CompletedSuccess',
        'CompletedError',
        'TimeoutOrCancelled',
        'request_state_name',
    ):
        assert token in header

    for token in (
        'completion_status_allowed',
        'completion_done_predicate',
        'set_terminal_state',
        'arm_pending',
        'wait_pending',
        'cancel_pending',
        'complete_from_irq',
        'request->state != RequestState::Pending',
        'queue->generations[slot] != __token->generation',
        'request->completion_generation != __token->generation',
    ):
        assert token in source

    forbidden = source[source.index('Status complete_from_irq') : source.index('Status read_sync')]
    assert 'driver::block::read_sectors(' not in forbidden
    assert 'driver::block::write_sectors(' not in forbidden
    assert 'send_eoi' not in forbidden
    assert 'kmalloc' not in forbidden
    assert 'free(' not in forbidden


def test_normal_storage_consumers_use_request_layer() -> None:
    bcache = read_source('kernel/core/fs/bcache.cc')
    exfat = read_source('kernel/core/fs/exfat.cc')
    bigfs = read_source('kernel/core/fs/bigfs.cc')

    assert 'bigos::block_io::read_sync' in bcache
    assert 'bigos::block_io::write_sync' in bcache
    assert 'driver::block::read_sectors' not in bcache
    assert 'driver::block::write_sectors' not in bcache

    assert 'bigos::block_io::read_sync' in exfat
    assert 'driver::block::read_sectors' not in exfat

    assert 'bigos::block_io::write_sync' in bigfs
    assert 'driver::block::write_sectors(&g_device' not in bigfs


def test_request_layer_smoke_is_default_off_and_in_matrix() -> None:
    xmake = read_source('xmake.lua')
    kernel = read_source('kernel/core/kernel.cc')
    boot_debug = read_source('tools/boot_debug.py')

    assert 'option("block_io_request_smoke")' in xmake
    option_index = xmake.index('option("block_io_request_smoke")')
    assert 'set_default(false)' in xmake[option_index : option_index + 220]
    assert 'add_defines("BIGOS_BLOCK_IO_REQUEST_SMOKE")' in xmake

    assert '#ifdef BIGOS_BLOCK_IO_REQUEST_SMOKE' in kernel
    assert 'BIGOS_BLOCK_IO_REQUEST_PASSED' in kernel
    assert 'BIGOS_BLOCK_IO_REQUEST_FAILED' in kernel
    assert 'DeviceRole::RamValidationBlock' in kernel
    assert 'bigos::block_io::write_role_sync' in kernel
    assert 'bigos::block_io::read_role_sync' in kernel
    assert 'block_io_smoke_bcache_round_trip' in kernel
    assert 'block_io_smoke_bcache_dirty_failure' in kernel
    assert 'ram_block_set_write_fault' in kernel
    assert 'Status::QueueFull' in kernel
    assert 'Status::Unsupported' in kernel
    assert 'Status::DeviceError' in kernel
    assert 'block_io_smoke_completion_wait' in kernel
    assert 'block_io_smoke_completion_edges' in kernel
    assert 'create_kernel_thread(&block_io_request_smoke_entry' in kernel

    assert "'block_io_request_smoke'" in boot_debug
    assert "case_id='block-io-request-layer'" in boot_debug
    assert 'BIGOS_BLOCK_IO_REQUEST_PASSED' in boot_debug
    assert 'bounded kernel-internal' in boot_debug
    assert 'no complete async I/O' in boot_debug


def test_ram_backend_smoke_covers_framework_request_cache_and_boundaries() -> None:
    kernel = read_source('kernel/core/kernel.cc')
    boot_debug = read_source('tools/boot_debug.py')

    for token in (
        'ram-publish',
        'ram-write',
        'ram-read',
        'ram-buffer',
        'ram-unchanged',
        'ram-range',
        'role-not-ready',
        'ram-cache',
        'ram-cache-dirty',
        'completion-wait',
        'completion-edges',
    ):
        assert token in kernel

    assert 'peer_accepted_under_pressure' in kernel
    assert 'bigos::bcache::invalidate_device(__ram)' in kernel
    assert 'block->dirty' in kernel
    assert 'bigos::block_io::complete_from_irq(&ctx->token' in kernel
    assert 'Status::PendingTimeout' in kernel
    assert 'Status::CompletionRejected' in kernel
    assert 'internal RAM block role' in kernel
    assert 'user-visible device node' in boot_debug
