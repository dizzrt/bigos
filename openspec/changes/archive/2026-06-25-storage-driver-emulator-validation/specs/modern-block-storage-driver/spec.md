## ADDED Requirements

### Requirement: 现代存储后端支持仿真器验证观测
BigOS SHALL expose enough kernel-internal validation state for the modern block-storage backend to prove backend publication, request submission, completion, and terminal success or failure without creating user-visible device ABI.

#### Scenario: 验证可观察后端发布
- **WHEN** the modern block-storage backend is probed during a validation run
- **THEN** BigOS MUST make the backend publication result observable to the default-off validation path through kernel-internal status or diagnostics
- **AND** it MUST NOT expose a new syscall, device node, mount name, or user-visible device identifier for this purpose

#### Scenario: 验证可观察请求终态
- **WHEN** validation submits I/O through the modern block-storage backend
- **THEN** BigOS MUST allow the validation path to distinguish terminal success, timeout, device error, issue failure, and rejected or late completion at the block request layer boundary
- **AND** it MUST NOT report success from driver-private state before the block request layer reaches terminal success

### Requirement: 现代存储验证覆盖块层缓存写回集成
BigOS SHALL require emulator validation of the modern block-storage backend to exercise integration through the block request layer, page/buffer cache, and writeback path rather than only driver-private device operations.

#### Scenario: 集成验证经过缓存与写回
- **WHEN** the modern storage emulator validation path runs with the backend available
- **THEN** it MUST perform at least one cache-mediated write/read round trip through the ordinary block path
- **AND** the validation MUST depend on writeback and readback observing request-layer terminal success

#### Scenario: 失败路径保持分层
- **WHEN** modern storage validation observes backend publication failure, request-layer failure, cache/writeback failure, or timeout
- **THEN** BigOS MUST preserve those failures as distinct validation results
- **AND** it MUST NOT collapse them into a generic driver failure when recording the validation outcome

### Requirement: 现代存储后端不改变默认启动依赖
BigOS SHALL keep the modern block-storage backend independent from the default boot path unless a future change explicitly changes that contract.

#### Scenario: 默认启动不要求现代后端
- **WHEN** the modern storage backend is absent, unsupported, or disabled outside validation
- **THEN** default boot MUST continue to rely on the existing boot/storage baseline
- **AND** modern backend absence MUST NOT prevent normal userland baseline validation from running

#### Scenario: 验证设备不替代启动设备
- **WHEN** the emulator attaches a modern storage device for validation
- **THEN** BigOS MUST treat it as an explicit validation target selected by kernel-internal logic
- **AND** it MUST NOT silently replace the boot disk, root filesystem source, or persistent writable filesystem backend
