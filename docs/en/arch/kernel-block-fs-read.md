# Kernel Block And exFAT Read Path

BigOS phase 7 adds a kernel-runtime read-only block and filesystem path. This
path is separate from the Legacy BIOS bootloader exFAT helpers: the bootloader
still uses fixed low-memory buffers and contiguous boot files to load
`/boot/boot.bin` and the kernel, while the kernel runtime exposes bounded APIs
that later stages can reuse after memory management is initialized.

## Scope

- Block reads use a synchronous, read-only `BlockDevice` contract over whole
  512-byte sectors with caller-owned buffers.
- The first backend is ATA PIO for the Legacy BIOS raw image used by Bochs and
  QEMU IDE: primary master, LBA48, synchronous polling, and bounded timeouts.
- The filesystem layer discovers the first valid MBR exFAT partition, validates
  the exFAT boot region, and mounts a single read-only volume.
- exFAT support covers root path lookup, regular file metadata, bounded
  reads, `NoFatChain` contiguous files, and bounded FAT-chain traversal. The
  VFS metadata bridge fully initializes atime, mtime, and ctime fields; the
  read-only exFAT backend currently reports documented zero timestamp defaults
  rather than complete exFAT timestamp semantics.
- This archived foundation API is ordinary-kernel-context only. It is not
  IRQ-handler-safe, not DMA based, and does not itself provide scheduler sleep,
  async request lifecycle, or cross-CPU semantics; newer block-layer documents
  describe the bounded interrupt-driven request path built above it.

## Non-Goals

- No write, delete, directory mutation, permissions, page cache, or full VFS.
- No AHCI, NVMe, USB storage, DMA, hotplug, or broad storage-device management.
- No full exFAT implementation beyond the controlled read-only subset.
- No changes to MBR/DBR/extended DBR layout, BootInfo handoff, linker addresses,
  or the existing first user-program smoke.

## Validation Smoke

`xmake f --fs_smoke=y` enables a default-off runtime smoke. The image generator
adds `/boot/fs_smoke.txt` with the payload `BIGOS_FS_SMOKE_PAYLOAD\n` while
preserving the existing `/boot/boot.bin` and `kernel` layout. When
`build/bin/user/init.elf` exists, the same image generator also packages it as
`/boot/user/init.elf` for the default-off ELF user-program smoke. During kernel
initialization the smoke reads the file through ATA PIO and exFAT, then emits:

- `BIGOS_FS_EXFAT_READ_PASSED` on success.
- `BIGOS_FS_EXFAT_READ_FAILED code=<code>` on bounded mount, lookup, read, or
  verification failure.

This stage is the API foundation used by the archived
`load-user-elf-program` change: the default-off user ELF smoke reads
`/boot/user/init.elf` by path, then the bounded ELF64 `ET_EXEC` loader maps the
image instead of depending on bootloader-only exFAT helpers or embedded flat
images.
