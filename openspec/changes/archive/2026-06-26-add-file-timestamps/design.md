## Context

当前 `bigos::Metadata` 和用户态 `struct stat` 预留了 `reserved[4]`，但没有 atime/mtime/ctime 字段；`file-metadata-contract` 也明确不承诺完整时间戳语义。`bigfs` inode 目前固定 64 字节、format version 为 1，记录 type/mode/uid/gid/size/direct blocks 等基础状态；时间来源已有 `time::current_unix_time()` 和用户态 `time()`/`date` 可消费。

本变更跨越内核 VFS、bigfs、syscall、用户 libc、用户工具和构建打包。实现必须保持 freestanding、安全用户缓冲拷贝、append-only syscall 编号，并避免改变 boot/disk 分区/IDT/页表/CR3 之类系统级 ABI。

## Goals / Non-Goals

**Goals:**

- 在内核和用户态 metadata ABI 中显式暴露 `atime`、`mtime`、`ctime`，使用 Unix epoch 秒。
- 在 `/rw` bigfs 中维护时间戳，使 create/read/write/truncate/rename/unlink/rmdir/stat/fstat/touch 可观察。
- 追加有界 `utimens` 类 syscall；可提供 `utime` 作为 libc 兼容 wrapper。
- 新增 `/bin/touch`，并让 `/bin/stat` 输出时间戳。
- 提供源码检查、用户程序构建和运行时 smoke 验证路径。

**Non-Goals:**

- 不提供纳秒精度、timezone/locale 格式化、完整 POSIX `utimensat`/`futimens`/`lutimes`、symlink timestamp、mount `noatime`、NFS/remote FS 语义或完整权限数据库。
- 不要求 exFAT 写入时间戳；只读 exFAT 可在可解析时填充 metadata 时间，否则用 0 或文档化默认值。
- 不把 `reserved[]` 作为未命名时间戳长期 ABI；字段应重命名并由源码契约检查固定布局。

## Decisions

1. 时间戳单位采用 Unix epoch 秒。

   BigOS 当前 wall-clock API 是秒级，阻塞 sleep 和 PIT tick 都是 coarse 模型；秒级字段能满足 `touch`、`stat` 和用户态 smoke。纳秒字段会引入虚假精度和更复杂 ABI，本阶段不做。

2. `Metadata`/`struct stat` 显式新增 `atime/mtime/ctime` 字段。

   将现有 reserved slots 迁移为命名字段可以保持结构大小接近当前 ABI，同时避免“reserved 被工具私用”的不清晰状态。需要增加 source contract 测试，确保内核/用户镜像结构字段顺序一致。

3. `utimens` syscall 使用路径、两个秒级时间值和 flags。

   建议接口为 BigOS-specific `SYS_UTIMENS`：`path, atime, mtime, flags`。flags 支持 `BIGOS_UTIME_NOW`、`BIGOS_UTIME_OMIT` 的有界子集，ctime 总是在成功 metadata mutation 时更新为当前时间。`utime(path, times[2])` 可作为 libc wrapper 降级到秒级。

4. bigfs format version 需要显式处理。

   因 inode 固定 64 字节，增加三个 64-bit 时间戳可能需要调整 inode 布局。实现应优先检查现有 inode 是否有足够保留空间；若没有， bump `FORMAT_VERSION` 并只支持重新 format 的 RAM-backed / persistent test disk。持久化兼容策略要明确：旧 persistent bigfs 可要求 `mkfs_bigfs` 重新格式化，不做 silent reinterpret。

5. atime 更新保持有界且可降级。

   严格每次 read 更新 atime 会增加写放大。实现可定义“read 成功后更新 inode atime 并标脏”，但验证只要求 atime 非递减并在受控读后可观察。后续如需 noatime/relatime 再单独扩展。

## Risks / Trade-offs

- [Risk] inode 布局膨胀破坏持久化 bigfs 兼容。→ 显式版本检查；旧格式拒绝挂载或要求重新 `mkfs_bigfs`，不做隐式迁移。
- [Risk] atime 造成 read 路径写入元数据。→ 保持 coarse 秒级和有界更新，验证只覆盖确定性变化，不承诺高精度。
- [Risk] exFAT 时间戳解析涉及 FAT 日期/时间和 timezone。→ 本阶段可先对 exFAT 返回 0 或只解析已可靠字段；文档明确只读后端不承诺完整时间戳。
- [Risk] syscall ABI 与 libc mirror 不一致。→ 添加 source-level syscall number mirror/struct layout 检查。
- [Risk] `touch` 名称接近 POSIX。→ help/spec 注释明确 BigOS bounded touch，不支持完整选项集和纳秒精度。
