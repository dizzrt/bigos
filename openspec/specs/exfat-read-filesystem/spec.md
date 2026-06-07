## Purpose

Define the first read-only filesystem capability for BigOS: discovering an exFAT
partition from an MBR, mounting a bounded exFAT volume, resolving absolute paths,
and reading regular files through the kernel block device layer. This capability
is scoped to read-only smoke-friendly access and does not introduce writes,
mutation, VFS, file descriptors, caching, userspace filesystem syscalls, or
general storage hotplug support.

## Requirements

### Requirement: MBR exFAT partition discovery

BigOS SHALL discover a supported exFAT partition from the MBR partition table before mounting the filesystem. The discovery operation MUST read the MBR through the block device layer, validate partition bounds, and select an exFAT partition whose boot region also passes exFAT validation.

#### Scenario: exFAT partition is discovered

- **WHEN** kernel code asks the filesystem layer to discover an exFAT partition on the project raw disk image
- **THEN** the filesystem layer reads the MBR, finds a partition candidate, validates its exFAT boot region, and returns the partition base LBA and sector count.

#### Scenario: No valid exFAT partition exists

- **WHEN** the MBR has no partition whose bounds and exFAT boot region validate successfully
- **THEN** the filesystem layer returns a no-supported-partition error and does not attempt to mount an unchecked LBA.

#### Scenario: Partition bounds are unsafe

- **WHEN** an MBR partition entry has zero length, overflows LBA arithmetic, or points outside the block device's supported address range when known
- **THEN** the filesystem layer rejects that partition entry before reading filesystem metadata from it.

### Requirement: exFAT volume mount

BigOS SHALL provide a read-only exFAT mount operation over a block device and an automatically discovered partition. The mount operation MUST validate the exFAT boot region fields required for safe reads before exposing the volume for path lookup.

#### Scenario: Valid exFAT volume mounts

- **WHEN** kernel code mounts a supported exFAT volume from a discovered exFAT partition on the project raw disk image
- **THEN** the mount operation records the sector size, sectors per cluster, FAT location, cluster heap location, root directory cluster, and volume bounds needed for later reads.

#### Scenario: Invalid boot region is rejected

- **WHEN** the boot region does not contain the expected exFAT identity or contains unsupported/unsafe geometry values
- **THEN** the mount operation returns an explicit error and does not expose a usable mount handle.

### Requirement: Read-only path lookup

BigOS SHALL support read-only absolute path lookup for regular files on the mounted exFAT volume. The lookup MUST parse directory entry sets with bounds checks and MUST reject unsupported or malformed entries instead of reading beyond the directory buffer.

#### Scenario: Lookup finds a file below root

- **WHEN** kernel code looks up a supported absolute path such as `/boot/fs_smoke.txt`
- **THEN** the filesystem layer resolves each path component through exFAT directory entries and returns file metadata including first cluster, data length, and allocation mode.

#### Scenario: Lookup misses a file

- **WHEN** kernel code looks up a path that is not present in the mounted exFAT directory tree
- **THEN** the filesystem layer returns a not-found error without issuing reads outside the volume bounds.

#### Scenario: Malformed directory entry is rejected

- **WHEN** a directory entry set has an invalid secondary count, missing stream extension, incomplete filename entries, or a length that would exceed the directory buffer
- **THEN** the filesystem layer returns a malformed-filesystem error and does not continue with unchecked metadata.

### Requirement: Bounded file read

BigOS SHALL provide a bounded read API for regular files that copies at most the requested bytes from a file offset into a caller-provided kernel buffer. The read API MUST clamp reads at file size, validate destination capacity, and avoid accessing sectors or clusters outside the mounted volume.

#### Scenario: File content read succeeds

- **WHEN** kernel code reads a byte range from a supported exFAT regular file into a sufficiently large kernel buffer
- **THEN** the filesystem layer reads the required sectors through the block device and copies the requested file bytes into the destination buffer.

#### Scenario: Read past end of file is clamped

- **WHEN** kernel code reads from a valid offset with a length that extends past the file size
- **THEN** the filesystem layer copies only bytes available before end of file and reports the actual byte count read.

#### Scenario: Destination buffer is too small

- **WHEN** kernel code provides a destination capacity smaller than the requested read length
- **THEN** the filesystem layer rejects the request before copying and returns a bounded error status.

### Requirement: exFAT allocation mode support

BigOS SHALL support exFAT files marked as contiguous `NoFatChain` and files that require FAT-chain traversal. FAT-chain traversal MUST validate cluster bounds, EOF markers, bad cluster markers, reserved values, loops, and maximum traversal length before using each cluster for file reads.

#### Scenario: Contiguous file is read by cluster math

- **WHEN** a file is marked as `NoFatChain` and its first cluster plus data length maps inside the cluster heap
- **THEN** the filesystem layer computes the backing LBA range directly and reads the requested data.

#### Scenario: FAT-chain file is read by following the chain

- **WHEN** a regular file requires FAT-chain traversal and every visited FAT entry is valid until EOF
- **THEN** the filesystem layer reads file content through the visited cluster sequence and returns the requested bytes.

#### Scenario: Invalid FAT chain is rejected

- **WHEN** FAT-chain traversal encounters an out-of-range cluster, bad cluster, reserved value, cycle, or traversal longer than the file can require
- **THEN** the filesystem layer returns a malformed-filesystem error and does not treat the file as contiguous.

### Requirement: Filesystem smoke marker

BigOS SHALL provide a default-off filesystem smoke that reads `/boot/fs_smoke.txt` from the exFAT test image through the kernel block and filesystem layers. The smoke MUST emit deterministic COM1 markers for pass and fail outcomes.

#### Scenario: Smoke reads expected payload

- **WHEN** the filesystem smoke build option is enabled and the Bochs raw image contains `/boot/fs_smoke.txt` with the expected payload
- **THEN** kernel boot reads the file through the new runtime block/FS stack and emits `BIGOS_FS_EXFAT_READ_PASSED`.

#### Scenario: Smoke fails deterministically

- **WHEN** the filesystem smoke build option is enabled but mount, lookup, read, or payload verification fails
- **THEN** kernel boot emits `BIGOS_FS_EXFAT_READ_FAILED` with a bounded error code or enters the unified panic path with a deterministic marker.

#### Scenario: Smoke disabled leaves boot unchanged

- **WHEN** the filesystem smoke build option is disabled
- **THEN** the normal boot path does not require runtime exFAT mounting or test file reads.
