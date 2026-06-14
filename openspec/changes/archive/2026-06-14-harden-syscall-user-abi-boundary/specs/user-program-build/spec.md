## ADDED Requirements

### Requirement: 用户程序构建只依赖 freestanding 用户 ABI
BigOS user program builds SHALL consume only freestanding userland headers, crt0, user libc, and explicitly supported ABI constants. User program sources MUST NOT require kernel-private headers, backend implementation headers, host libc headers, dynamic linker inputs, or unspecified syscall numbers.

#### Scenario: 用户程序 include 边界
- **WHEN** a bundled user program is compiled
- **THEN** its includes MUST resolve through userland public headers and freestanding support headers
- **AND** it MUST NOT include kernel-private process, VFS, interrupt, memory-management, scheduler, or architecture backend implementation headers

#### Scenario: syscall number 不在程序内重复定义
- **WHEN** a user program invokes kernel services
- **THEN** it MUST use libc wrappers or shared stable ABI constants
- **AND** it MUST NOT maintain an ad hoc syscall number table that can drift from the kernel dispatcher

### Requirement: 用户程序构建不扩大 runtime 承诺
BigOS user program build and packaging SHALL preserve the current static ELF64 `ET_EXEC` and bounded image-install contract while syscall/user ABI boundaries are hardened. The build MUST NOT imply support for dynamic linking, shared libraries, hosted libc, complete POSIX process behavior, or additional runtime backends.

#### Scenario: 静态 ELF64 约束保持不变
- **WHEN** user programs are built after ABI boundary cleanup
- **THEN** they MUST continue to link as bounded static ELF64 `ET_EXEC` artifacts with crt0 and user libc
- **AND** the build MUST NOT require a dynamic loader, shared libraries, or host runtime services

#### Scenario: 打包路径保持现有镜像契约
- **WHEN** user programs are packaged into the boot image
- **THEN** packaging MUST preserve existing boot, MBR, partition, exFAT, and kernel image layout assumptions
- **AND** ABI boundary cleanup MUST NOT require new storage drivers or a new image format
