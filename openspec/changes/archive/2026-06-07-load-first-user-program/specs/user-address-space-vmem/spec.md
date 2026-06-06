## MODIFIED Requirements

### Requirement: 用户地址空间页表根派生共享内核高半区

BigOS SHALL provide a minimal user address-space root derivation capability where the kernel higher half is shared in the derived root and the user lower half is isolated. Derivation by itself SHALL NOT switch CR3 or enter ring3; a later process runtime path MAY explicitly activate the derived root only under its own controlled requirements.

#### Scenario: 派生根复制内核高半区条目

- **WHEN** 内核代码基于当前内核 PML4 派生一个新的用户地址空间页表根
- **THEN** 派生根 MUST 复制内核 PML4 高半区顶层条目（覆盖 kernel higher-half、self-mapping、direct map、KVMEM 所在区域）
- **AND** 派生根的低半区（用户区）顶层条目 MUST 被清零以保证用户地址空间相互独立

#### Scenario: 派生不隐式切换地址空间

- **WHEN** 用户地址空间页表根被派生
- **THEN** BigOS MUST NOT implicitly write CR3 as part of the derivation helper
- **AND** BigOS MUST NOT implicitly enter ring3, load user code, or implement demand paging as a side effect of deriving the root
- **AND** `#PF` handler MUST remain diagnostic-only for kernel faults

#### Scenario: 进程运行路径可显式激活派生根

- **WHEN** a dedicated first-user-program or process runtime path starts a user process using a derived user address-space root
- **THEN** that runtime path MAY explicitly activate the derived root by switching CR3 or equivalent address-space state
- **AND** the activated root MUST preserve kernel higher-half mappings needed for syscall, exception, IRQ, direct-map, KVMEM, and diagnostic paths
- **AND** this activation MUST be covered by that runtime capability's validation rather than by the derivation helper alone

### Requirement: 用户地址空间页表准备的验证可复现

BigOS SHALL use source-level checks and default-off emulator smoke to validate page attribute primitives, user root derivation semantics, and the boundary between passive derivation and explicit runtime activation.

#### Scenario: 源码级检查覆盖属性与派生不变量

- **WHEN** this change is implemented
- **THEN** source-level checks MUST cover: primitive accepts explicit attributes, kernel default attributes are equivalent to supervisor `present+writable`, user mappings set user bit, user data pages are NX / code pages are non-NX, and derived roots copy the high half while clearing the low half
- **AND** source-level checks MUST confirm the derivation helper itself does not write CR3 or enter ring3

#### Scenario: 构建与 emulator 验证被记录

- **WHEN** implementation completes
- **THEN** validation MUST record the narrowest available `xmake` / cross-toolchain build, relevant `uv run pytest` source-level checks, and `openspec validate prepare-user-address-space-vmem --strict` or the current change's strict validation command when this requirement is modified
- **AND** if Bochs runtime smoke cannot observe markers due to emulator, ROM, serial oracle, image lock, or interaction limits, validation MUST record the missing dependency and remaining bootability risk
