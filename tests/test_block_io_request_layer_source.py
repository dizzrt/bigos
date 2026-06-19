from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_source(relative: str) -> str:
    if relative == "xmake.lua":
        parts = [
            ROOT / "xmake.lua",
            ROOT / "xmake/options.lua",
            ROOT / "xmake/kernel.lua",
        ]
        return "\n".join(path.read_text(encoding="utf-8") for path in parts)
    return (ROOT / relative).read_text(encoding="utf-8")


def test_request_layer_defines_bounded_sync_contract() -> None:
    header = read_source("include/bigos/block_io.h")
    source = read_source("kernel/core/block_io.cc")

    assert "QUEUE_CAPACITY_PER_DEVICE = 8" in header
    assert "MAX_DEVICE_QUEUES = 8" in header
    assert "enum class Operation" in header
    assert "enum class Status" in header
    assert "InvalidRequest" in header
    assert "QueueFull" in header
    assert "DeviceNotReady" in header
    assert "struct Request" in header
    assert "Status submit_sync(Request *__request) noexcept;" in header

    assert "g_queues" in source
    assert "validate_request" in source
    assert "queue->depth >= QUEUE_CAPACITY_PER_DEVICE" in source
    assert "Status::QueueFull" in source
    assert "driver::block::read_sectors(" in source
    assert "driver::block::write_sectors(" in source
    assert "Status::WouldBlock" in source


def test_normal_storage_consumers_use_request_layer() -> None:
    bcache = read_source("kernel/core/fs/bcache.cc")
    exfat = read_source("kernel/core/fs/exfat.cc")
    bigfs = read_source("kernel/core/fs/bigfs.cc")

    assert "bigos::block_io::read_sync" in bcache
    assert "bigos::block_io::write_sync" in bcache
    assert "driver::block::read_sectors" not in bcache
    assert "driver::block::write_sectors" not in bcache

    assert "bigos::block_io::read_sync" in exfat
    assert "driver::block::read_sectors" not in exfat

    assert "bigos::block_io::write_sync" in bigfs
    assert "driver::block::write_sectors(&g_device" not in bigfs


def test_request_layer_smoke_is_default_off_and_in_matrix() -> None:
    xmake = read_source("xmake.lua")
    kernel = read_source("kernel/core/kernel.cc")
    boot_debug = read_source("tools/boot_debug.py")

    assert 'option("block_io_request_smoke")' in xmake
    option_index = xmake.index('option("block_io_request_smoke")')
    assert "set_default(false)" in xmake[option_index : option_index + 220]
    assert 'add_defines("BIGOS_BLOCK_IO_REQUEST_SMOKE")' in xmake

    assert "#ifdef BIGOS_BLOCK_IO_REQUEST_SMOKE" in kernel
    assert "BIGOS_BLOCK_IO_REQUEST_PASSED" in kernel
    assert "BIGOS_BLOCK_IO_REQUEST_FAILED" in kernel
    assert "Status::QueueFull" in kernel
    assert "Status::Unsupported" in kernel
    assert "Status::DeviceError" in kernel

    assert "'block_io_request_smoke'" in boot_debug
    assert "case_id='block-io-request-layer'" in boot_debug
    assert "BIGOS_BLOCK_IO_REQUEST_PASSED" in boot_debug
    assert "no async I/O" in boot_debug
