## MODIFIED Requirements

### Requirement: 确定性的启动镜像安装

启动镜像 patch 工具 SHALL 以确定性方式安装或拒绝受支持 BIOS/MBR/exFAT 启动路径所需的每个 x86 boot artifact。受支持入口 SHALL 为 `uv run python -m tools.bigosdev image patch` 或等价 `python3 -m tools.bigosdev image patch` 调用。

#### Scenario: 扫描所有 MBR 分区表项

- **WHEN** 镜像 patch 工具在 MBR 中查找 exFAT 分区
- **THEN** 它 MUST 读取并检查全部四个 16 字节分区表项

#### Scenario: boot.bin 安装行为明确

- **GIVEN** 目标镜像包含已有、连续、容量足够的 `/boot/boot.bin`
- **WHEN** 使用 `image patch --with-boot` 调用 `tools.bigosdev`
- **THEN** 它 MUST 将生成的 `boot.bin` 覆盖写入该预分配文件的数据区
- **AND** 它 MUST NOT 新建文件、扩展文件、分配 cluster、更新 allocation bitmap，或生成新的 exFAT 目录项

#### Scenario: boot.bin 放置方式不受支持

- **WHEN** `/boot/boot.bin` 缺失、非连续、容量不足，或目录布局不受支持
- **THEN** 镜像 patch 工具 MUST 以明确的 unsupported-layout 或等价错误失败
- **AND** 它 MUST NOT 部分更新磁盘镜像

#### Scenario: 刷新 exFAT boot checksum

- **WHEN** 镜像 patch 工具修改 exFAT boot-region 扇区
- **THEN** 它 MUST 更新对应 main 和 backup boot-region checksum

## ADDED Requirements

### Requirement: 旧 install.py 入口不再受支持
BigOS SHALL remove `tools/install.py` as an active boot artifact installer entry after its bounded patch capability is migrated to `tools.bigosdev image patch`.

#### Scenario: Active documentation describes patching boot artifacts
- **WHEN** active documentation or active OpenSpec specs describe patching MBR, DBR, extended DBR, or `/boot/boot.bin`
- **THEN** they MUST point to `uv run python -m tools.bigosdev image patch`
- **AND** they MUST NOT point to `tools/install.py` as the active supported entry
