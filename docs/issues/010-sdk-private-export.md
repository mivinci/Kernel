# 010 SDK 私有头导出靠手工名单，无一致性检查

状态：已修复（两架构SDK与722项host/格式验收通过）　优先级：P2　关联约束：无（构建与维护性）

## 现象

- 公共头用 glob 自动导出（`tools/user.py:44-47` 的 `glob`/`rglob`），而**私有头是逐个 `copyfile` 手写**（`tools/user.py:54-60`）；libvirtio 部分又硬编码了一个名字元组（`tools/user.py:62-63`）。
- `tools/user.py:64` 有 `(sdk / 'private/virtqueue.h').unlink(missing_ok=True)`，用于清理改名后残留的旧私有头。
- 未发现"源侧私有头集合 == SDK 导出集合"的任何检查；`tests/` 中出现的 `private/` 都是主机测试自己 include，不校验导出。

## 影响

- 新增或重命名私有头而忘记改 `user.py`：SDK 静默缺文件，错误要到第三方 libc 适配的编译期才出现，且报在 include 链上，不指向导出步骤。
- 旧名字副本可能残留在已生成的 `sdk/private/` 中，形成同一头两份不同版本；现有 `unlink` 只覆盖已知的一个旧名。
- SDK 与源码可长期不一致而无人发现，`make sdk` 仍然成功。

## 建议修复

1. 私有头导出改为按目录枚举加显式白名单，与公共头的做法对齐。
2. 增加 SDK 一致性检查：构建后比较 `lib/*/private/**` 与 `sdk/private/**`（含 `virtio/` 子目录），集合不一致即失败并列出差异。
3. 用"先清空 `sdk/private` 再导出"替代针对单个旧名的 `unlink`，从根上消除残留。
4. 文档补一句：新增私有头只需改源目录，不需要改构建脚本。

## 验收

- 临时新增一个私有头且不改脚本，`make sdk` 后它出现在 `sdk/private/` 下；
- 删除一个私有头后重建，SDK 中不再存在该文件；
- 人为在 `sdk/private/` 放一个多余头，一致性检查失败并指出该文件。

## 修复落点（2026-11-18）

tools/sdkheaders.py统一白名单源目录及目标命名空间：libsys/private、libc/private映到SDK private根，libvirtio/private映到private/virtio；每个目录递归枚举.h，不再维护单文件名单。完整映射先检查碰撞，再清除生成的private目录并重新导出；检查源/目标符号链接，避免跟随目录链接删除或导出外部文件。

构建前后均比较精确文件集合和内容，manifest记录每个私有导出路径、来源及SHA256；构建期间源集合/内容变化也拒绝成功。独立命令`python3 tools/user.py --arch riscv --check-private-headers`（ARM64同理）只检查，不复制、编译或修复额外文件，错误列出missing/extra/changed。

7项专项覆盖新增/嵌套头、删除/改名/旧副本、缺失/额外/内容不符诊断、白名单、碰撞先拒绝及符号链接边界。两架构实际make sdk已验证临时源头自动导出、额外头拒绝、删除后重建清除；临时文件均已移除。实际libsys/libc/libvirtio归档、csu和init/runtime/serviceauth/irqdriver ELF哈希与改动前完全相同。记录在build/host/stage107-sdk-headers/。

最终722项host、fmt-check及四次两架构SDK构建通过；独立checker的成功/失败均不修改导出及last-run构建状态，错误明确列出人为额外文件。evidence.json核对最终十份导出/manifest逐字节内容、源码哈希及全部代表运行产物不变。本点无内核运行改动，未重跑QEMU，无CI/板端验收。
