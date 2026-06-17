## ADDED Requirements

### Requirement: 运行期目录树状态跨接口一致
BigOS SHALL expose successful `/rw` directory tree mutations consistently across path lookup, fd I/O, metadata queries, bounded directory enumeration, cwd-relative resolution, libc wrappers, shell commands, and packaged path tools within the same boot session. This requirement applies to the active writable backend's current-runtime state and MUST NOT imply cross-reboot persistence unless the persistent clean-sync backend separately commits the state.

#### Scenario: shell 和用户程序观察同一目录树
- **WHEN** a user program creates `/rw/tree/sub`, creates multiple files under it, and a shell command or packaged path tool later lists or stats the same paths in the same boot session
- **THEN** BigOS MUST expose the same live directory entries, object types, and bounded metadata through both programmatic and shell-visible paths
- **AND** relative paths from cwd inside the directory tree MUST resolve according to the existing bounded path resolution contract

#### Scenario: 删除后所有运行期接口收敛
- **WHEN** a file is unlinked or an empty directory is removed successfully under `/rw`
- **THEN** subsequent open, stat, directory enumeration, and shell/path-tool observations of the parent directory MUST agree that the removed path is no longer present
- **AND** unrelated sibling entries MUST remain observable

### Requirement: 目录树失败路径保持运行期状态可解释
BigOS SHALL keep runtime filesystem state explainable when directory tree operations fail. Failed creation, deletion, lookup, enumeration, or metadata operations caused by missing paths, existing targets, invalid object types, read-only backends, permission denial, capacity exhaustion, invalid descriptors, illegal user buffers, backend I/O errors, allocation failure, or nonblocking-context rejection MUST return stable negative errno values and MUST NOT publish partial objects or corrupt unrelated state.

#### Scenario: 非法用户缓冲不改变目录树
- **WHEN** a syscall that copies path, metadata, or directory entries to or from user memory fails user-buffer validation
- **THEN** BigOS MUST return a deterministic fault or invalid-argument error
- **AND** it MUST NOT create, remove, rename, or partially enumerate directory tree entries as a side effect

#### Scenario: 不可阻塞上下文拒绝目录树副作用
- **WHEN** directory tree code would allocate, block, issue synchronous block I/O, flush dirty cache state, or mutate directory entries from IRQ context, scheduler critical sections, preemption-disabled regions, or another nonblocking path
- **THEN** BigOS MUST fail deterministically or enter a documented diagnostic path
- **AND** it MUST NOT publish fd entries, dirty cache blocks, directory entries, inode metadata, cwd changes, or partial user output from that context
