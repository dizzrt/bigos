## MODIFIED Requirements

### Requirement: ELF64 program-header 加载

bootloader SHALL 按照 ELF64 的 `PT_LOAD` program header 加载 kernel，并 MUST 支持由 kernel 链接脚本生成的多个不同权限 loadable segment。bootloader MUST NOT 假设 kernel ELF 只有单个 `PT_LOAD`，也 MUST NOT 因 text/rodata/data 被拆分到不同 program header 而遗漏加载、遗漏 zero-fill 或错误计算 kernel memory extent。

#### Scenario: ELF header 无效

- **WHEN** kernel 文件不包含受支持的 ELF64 x86_64 header
- **THEN** bootloader MUST 报告 invalid-kernel 错误并 halt

#### Scenario: 存在多个 loadable segment

- **WHEN** ELF64 program header table 包含多个 `PT_LOAD` entry
- **THEN** bootloader MUST 将每个 loadable segment 加载到预期映射目标

#### Scenario: 权限拆分后的多个 loadable segment

- **WHEN** kernel ELF 将 text、rodata 和 data 拆分为多个不同权限的 `PT_LOAD` entry
- **THEN** bootloader MUST 遍历并加载所有 loadable segment
- **AND** bootloader MUST continue to compute kernel memory extent from the maximum end address of all loaded segments

#### Scenario: segment 内存大小大于文件数据

- **WHEN** 某个 `PT_LOAD` segment 的 `p_memsz` 大于 `p_filesz`
- **THEN** bootloader MUST 在进入 kernel 前 zero-fill 剩余 segment memory

#### Scenario: program header 跨多个扇区

- **WHEN** ELF64 program header table 跨越超过一个磁盘扇区
- **THEN** bootloader MUST 读取足够扇区以解析所有声明的 program header

#### Scenario: 使用 ELF entry point

- **WHEN** ELF64 header 提供 `e_entry`
- **THEN** bootloader MUST 校验 entry point 落在已加载的 `PT_LOAD` 虚拟地址范围内
- **AND** bootloader MUST 跳转到已校验的 ELF entry point，而不是固定 higher-half base
