## ADDED Requirements

### Requirement: 有界文件时间戳 syscall
BigOS SHALL extend the bounded syscall surface with append-only file timestamp update operations. The operations MUST use the existing `int 0x80` register ABI, existing user pointer validation, existing path copy/cwd resolution rules, and negative errno convention. They MUST NOT change existing syscall numbers, `VECTOR_SYSCALL = 0x80`, syscall gate privilege, exception/IRQ EOI rules, boot layout, page-table layout, CR3 switching, or disk image layout.

#### Scenario: utimens syscall 追加而不改号
- **WHEN** file timestamp syscalls are added
- **THEN** BigOS MUST append new syscall numbers after the current syscall surface or use documented unused entries
- **AND** all existing syscall numbers, argument register order, and return behavior MUST remain unchanged

#### Scenario: utimens 成功更新时间戳
- **WHEN** a user process invokes the supported timestamp syscall with a valid path, valid timestamp arguments, and sufficient permission
- **THEN** BigOS MUST update the target object's atime and mtime according to the request
- **AND** it MUST update ctime to the current bounded timestamp value

#### Scenario: syscall 参数失败不修改状态
- **WHEN** the timestamp syscall receives an invalid path pointer, invalid timestamp pointer, unsupported flags, missing target, read-only backend target, unsupported object, or insufficient permission
- **THEN** BigOS MUST fail with deterministic negative errno
- **AND** it MUST NOT mutate file timestamps, file data, directory entries, fd table state, or process identity

### Requirement: libc 时间戳 wrapper
BigOS userland libc SHALL expose bounded wrappers for explicit timestamp updates. The wrappers MUST translate kernel negative errno returns into user `errno`, MUST remain freestanding-safe, and MUST NOT declare unsupported POSIX variants such as full `utimensat`, `futimens`, `lutimes`, symlink timestamp updates, timezone conversion, or nanosecond precision unless those capabilities are explicitly implemented.

#### Scenario: libc wrapper 成功
- **WHEN** a simple static user program calls the supported libc timestamp wrapper for a writable path
- **THEN** libc MUST invoke the correct timestamp syscall and return success
- **AND** subsequent `stat` MUST observe the requested bounded timestamp values

#### Scenario: libc wrapper 失败设置 errno
- **WHEN** the kernel returns a timestamp syscall error
- **THEN** libc MUST set user `errno` to the positive error value and return the documented failure sentinel
- **AND** callers MUST NOT need to parse kernel negative errno values
