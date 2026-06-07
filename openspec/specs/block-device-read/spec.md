## Purpose

Define the first kernel-only block device read capability for BigOS: a bounded
whole-sector read API, a synchronous ATA PIO read-only backend for the current
Bochs raw disk setup, explicit non-IRQ context limits, and deterministic smoke
diagnostics. This capability is intentionally read-only and does not introduce
async IO, request queues, write support, caching, partition management, or
filesystem semantics.

## Requirements

### Requirement: Kernel block device read API

BigOS SHALL provide a kernel-only block device read interface that reads whole sectors from an LBA address into a caller-provided kernel buffer. The API MUST validate sector count, destination buffer length, and arithmetic overflow before issuing device I/O.

#### Scenario: Aligned sector read succeeds

- **WHEN** kernel code requests one or more sectors from a registered read-only block device with a destination buffer large enough for `sector_count * sector_size`
- **THEN** the block device API reads the requested sectors into the destination buffer and returns success.

#### Scenario: Buffer is too small

- **WHEN** kernel code requests sectors with a destination buffer smaller than the required byte count
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

#### Scenario: LBA arithmetic overflows

- **WHEN** kernel code requests a sector range whose end LBA calculation overflows the address type
- **THEN** the block device API rejects the request before issuing device I/O and returns a bounded error status.

### Requirement: ATA PIO read backend

BigOS SHALL provide an ATA PIO read-only block backend for the current Bochs raw disk setup. The backend MUST document its hardware limits, including synchronous polling, 512-byte sectors, primary-master addressing, and the supported LBA mode.

#### Scenario: ATA PIO backend reads a known sector

- **WHEN** the FS smoke opens the ATA PIO block backend against the Bochs raw image
- **THEN** the backend can read a known sector used by the test image and report success to the caller.

#### Scenario: ATA device is not ready

- **WHEN** the ATA PIO status polling does not reach the expected ready/data state within the bounded timeout
- **THEN** the backend stops polling and returns a device timeout or device error status without hanging the kernel indefinitely.

### Requirement: Block IO context boundary

Block device reads SHALL be callable only from ordinary kernel context after port I/O and memory management are initialized. The API MUST NOT be advertised as IRQ-handler-safe, preemption-safe, asynchronous, or sleepable.

#### Scenario: Non-IRQ context read

- **WHEN** kernel initialization or a smoke test calls the block read API after memory initialization
- **THEN** the call is within the supported context boundary and may perform synchronous polling I/O.

#### Scenario: IRQ context is out of scope

- **WHEN** code attempts to treat the block read API as IRQ-handler-safe behavior
- **THEN** the specification does not guarantee correctness, and implementation documentation MUST mark this usage unsupported.

### Requirement: Block read diagnostics

Block read failures SHALL be reported through explicit status values and deterministic diagnostic markers in smoke paths. The driver MUST NOT silently ignore short reads, device errors, buffer validation failures, or polling timeouts.

#### Scenario: Smoke observes a read failure

- **WHEN** the block backend fails during the filesystem smoke path
- **THEN** the smoke emits a deterministic failure marker containing a bounded error code before returning or panicking according to the smoke contract.

#### Scenario: Normal boot keeps block smoke disabled

- **WHEN** the block/filesystem smoke build option is disabled
- **THEN** normal kernel boot does not require ATA PIO probing or disk reads from the new block device layer.
