## MODIFIED Requirements

### Requirement: portable libc subset programs use existing static build path

BigOS user program builds SHALL validate portable libc subset consumers through the existing freestanding static ELF64 build path. Representative portable programs MUST link with BigOS crt0 and user libc using the current bounded `ET_EXEC` model, including consumers of the bounded buffered `FILE` stream subset and the expanded `string.h`/`stdlib.h`/`ctype.h` helpers, and MUST NOT require host libc, dynamic linking, shared libraries, a dynamic loader, TLS runtime, or a new image format.

#### Scenario: representative program statically links

- **WHEN** the build compiles a representative portable libc subset user program
- **THEN** it MUST link through the existing user crt0 and user libc path as a bounded static ELF64 `ET_EXEC`
- **AND** the build MUST NOT require host libc, dynamic loader, shared libraries, or hosted runtime services

#### Scenario: buffered FILE stream consumer links statically

- **WHEN** the build compiles a representative program that uses the bounded buffered `FILE` stream subset (`fopen`/`fread`/`fwrite`/`fgets`/`fseek` and related interfaces)
- **THEN** it MUST link through the existing static ELF64 `ET_EXEC` user libc path
- **AND** the build MUST NOT require host libc, hosted stdio runtime, dynamic linking, or shared libraries

#### Scenario: build failure is deterministic

- **WHEN** a representative portable program includes an unsupported header, calls an unavailable symbol, fails to link, or exceeds the bounded artifact size
- **THEN** the build MUST fail deterministically and identify the affected user program or artifact
- **AND** the failure MUST NOT silently fall back to host libc or dynamic linking

### Requirement: representative program coverage for portable subset

BigOS SHALL include representative small user programs or smoke consumers that cover portable libc subset behavior. The coverage MUST include public header inclusion, static linking, argument/environment access, stdout/stderr, ctype, bounded `time.h`, bounded `assert.h`, numeric conversion, formatter behavior, errno/error text, file or directory wrapper use, bounded buffered `FILE` stream behavior (open, buffered read/write, stream state, and bounded positioning), and at least one failure path. Programs SHOULD remain small and purpose-bounded, but the set as a whole MUST cover these behavior categories.

#### Scenario: representative programs cover libc behavior

- **WHEN** portable libc subset validation builds or runs representative user programs
- **THEN** the program set MUST cover the documented portable libc behavior categories, including the bounded buffered `FILE` stream subset
- **AND** each observed behavior MUST be attributable to BigOS user libc rather than host runtime behavior

#### Scenario: packaging preserves boot image contract

- **WHEN** representative portable programs are packaged into the boot image
- **THEN** packaging MUST use bounded deterministic install paths
- **AND** MUST preserve existing boot, MBR, partition, exFAT, kernel image, and user ELF loader layout assumptions
