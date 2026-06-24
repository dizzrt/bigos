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
    block_header = read_source('include/drivers/block/block_device.h')
    source = read_source('kernel/core/block_io.cc')

    assert 'QUEUE_CAPACITY_PER_DEVICE = 8' in header
    assert 'MAX_DEVICE_QUEUES = 8' in header
    assert 'enum class Operation' in header
    assert 'enum class Status' in header
    assert 'InvalidRequest' in header
    assert 'QueueFull' in header
    assert 'DeviceNotReady' in header
    assert 'PendingTimeout' in header
    assert 'Cancelled' in header
    assert 'CompletionRejected' in header
    assert 'enum class RequestState' in header
    assert 'enum class TerminalReason' in header
    assert 'enum class CompletionRejectionReason' in header
    assert 'struct DiagnosticsSnapshot' in header
    assert 'struct CompletionToken' in header
    assert 'struct Request' in header
    assert 'TerminalReason terminal_reason;' in header
    assert 'CompletionRejectionReason rejection_reason;' in header
    assert 'Status submit_sync(Request *__request) noexcept;' in header
    assert 'Status arm_pending(Request *__request, CompletionToken *__out_token) noexcept;' in header
    assert 'Status wait_pending(Request *__request, timer::tick_t __timeout_ticks = 0) noexcept;' in header
    assert 'Status complete_from_irq(const CompletionToken *__token, Status __final_status) noexcept;' in header
    assert 'void reset_diagnostics() noexcept;' in header
    assert 'void diagnostics_snapshot(DiagnosticsSnapshot *__out) noexcept;' in header
    assert 'Status read_role_sync(device::DeviceRole __role' in header
    assert 'Status write_role_sync(device::DeviceRole __role' in header
    assert 'using IssueRequestFn' in block_header
    assert 'bigos::block_io::CompletionToken' in block_header
    assert 'IssueRequestFn issue_impl;' in block_header

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
    assert 'DEFAULT_SUBMIT_TIMEOUT_TICKS' in source
    assert 'record_terminal' in source
    assert 'record_rejection' in source
    assert 'TerminalReason::IssueFailure' in source
    assert 'CompletionRejectionReason::LateCompletion' in source
    assert 'CompletionRejectionReason::DuplicateCompletion' in source
    assert 'CompletionRejectionReason::SlotReuseProtected' in source
    assert 'issue_request(' in source
    assert 'enqueue_request(queue, __request, &token, RequestState::Pending)' in source
    assert '__request->device->issue_impl' in source
    assert 'complete_from_irq(__token, final_status)' in source
    assert 'return wait_pending(__request, DEFAULT_SUBMIT_TIMEOUT_TICKS);' in source
    assert 'queue_lookup(__token->device)' in source
    assert '__device->issue_impl != nullptr && __sector_count > 1' in source
    assert 'dst + (size_t)sector * __device->sector_size' in source


def test_ata_pio_uses_irq_completion_boundary() -> None:
    header = read_source('include/drivers/block/ata_pio.h')
    source = read_source('kernel/drivers/block/ata_pio.cc')
    isr = read_source('kernel/core/irq/isr.cc')
    interrupt = read_source('include/irq/interrupt.h')

    for token in (
        'enum class AtaPioPhase',
        'irq_completion_enabled',
        'active_token',
        'ata_pio_primary_irq',
    ):
        assert token in header

    assert 'ata_issue_irq' in source
    assert '__device->block.issue_impl = __irq_completion ? ata_issue_irq : nullptr;' in source
    assert '__request->sector_count != 1' in source
    assert 'ATA_CMD_READ_SECTORS_EXT' in source
    assert 'ATA_CMD_WRITE_SECTORS_EXT' in source
    assert 'ATA_CMD_FLUSH_CACHE_EXT' in source
    assert 'wait_for_data(__device)' in source
    assert 'ata_read_sector(ata, ata->next_sector)' in source
    assert 'ata_write_sector(ata, ata->next_sector)' in source
    assert 'complete_from_irq(&token, final_status)' in source
    assert 'send_eoi' not in source[source.index('void ata_pio_primary_irq') :]
    assert 'kmalloc' not in source[source.index('void ata_pio_primary_irq') :]
    assert 'free(' not in source[source.index('void ata_pio_primary_irq') :]

    assert 'VECTOR_PRIMARY_IDE' in interrupt
    assert 'I8259_MASTER_VECTOR_BASE + IRQ_LINE_PRIMARY_IDE' in interrupt
    assert 'driver::block::ata_pio_primary_irq();' in isr
    assert 'register_isr(VECTOR_PRIMARY_IDE, &isr_primary_ide, VectorOwner::Pic)' in isr
    assert 'driver::irqchip::i8259::enable_irq(IRQ_LINE_SLAVE)' in isr
    assert 'driver::irqchip::i8259::enable_irq(IRQ_LINE_PRIMARY_IDE)' in isr


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
        'terminal_reason_name',
        'completion_rejection_reason_name',
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
        'request->rejection_reason = rejection',
        'g_diagnostics.device_error_count',
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
    assert 'block_io_smoke_issue_failure' in kernel
    assert 'diagnostics_snapshot(&diagnostics)' in kernel
    assert 'TerminalReason::IssueFailure' in kernel
    assert 'TerminalReason::Cancelled' in kernel
    assert 'CompletionRejectionReason::DuplicateCompletion' in kernel
    assert 'CompletionRejectionReason::LateCompletion' in kernel
    assert 'slot_reuse_protection_count' in kernel
    assert 'producer_status == bigos::block_io::Status::Success' not in kernel
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
        'Cancelled',
        'IssueFailure',
        'DuplicateCompletion',
        'LateCompletion',
        'SlotReuseProtected',
    ):
        assert token in kernel or token in read_source('include/bigos/block_io.h')

    assert 'peer_accepted_under_pressure' in kernel
    assert 'bigos::bcache::invalidate_device(__ram)' in kernel
    assert 'block->dirty' in kernel
    assert 'bigos::block_io::complete_from_irq(&ctx->token' in kernel
    assert 'Status::PendingTimeout' in kernel
    assert 'Status::CompletionRejected' in kernel
    assert 'internal RAM block role' in kernel
    assert 'user-visible device node' in boot_debug
