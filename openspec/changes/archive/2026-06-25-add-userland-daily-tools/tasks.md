## 1. Shell 内建

- [x] 1.1 在 `user/sh/sh.c` 中实现 `pwd`、`help`、`env`、`clear`、`true`、`false` 内建，并保持重定向/status 行为有界。
- [x] 1.2 从默认外部程序清单中移除 `/bin/pwd`，保留 shell 内建 `pwd` 作为默认入口。

## 2. 外部日常工具

- [x] 2.1 新增文件复制、移动、写入和追加工具：`cp`、`mv`、`tee`、`write`、`append`。
- [x] 2.2 新增内容查看和筛选工具：`head`、`tail`、`wc`、plain-substring `grep`、`hexdump`。
- [x] 2.3 新增系统观察和控制工具：`date`、`kill`、`sleep`。
- [x] 2.4 新增路径名、分页和树观察工具：`basename`、`dirname`、`more`、`find`、`du`。

## 3. 构建与打包

- [x] 3.1 更新 `xmake/user_package.lua`，把新增工具编译为默认 `/bin` 用户程序并删除 `pwd` 外部构建项。
- [x] 3.2 更新 `tools/bigosdev/core.py` 的默认用户程序打包清单，确保镜像校验与安装路径一致。

## 4. 验证

- [x] 4.1 运行 OpenSpec 校验，确认 proposal/design/spec/tasks 可解析。
- [x] 4.2 运行最窄可用用户程序构建检查，确认新增 freestanding C 工具能通过 cross toolchain 构建并满足 ELF 大小上限。
- [x] 4.3 环境允许时运行默认 QEMU headless 或用户态相关 smoke；不可用时记录缺失依赖与剩余风险。
