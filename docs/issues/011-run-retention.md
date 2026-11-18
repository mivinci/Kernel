# 011 测试证据目录无保留策略

状态：待处理　优先级：P2　关联约束：无（本地磁盘与工具效率）

## 现象（2026-11-18 实测）

`build/` 共约 **21 GB**，组成如下：

| 目录 | 体积 | 内容 |
| --- | --- | --- |
| `build/arm64/smoke` | 9.8 GB | QEMU 场景运行目录 |
| `build/riscv/smoke` | 6.1 GB | 同上 |
| `build/riscv/runs` | 2.1 GB | 逐命令日志 |
| `build/arm64/runs` | 611 MB | 逐命令日志 |
| `build/arm64/packageboot` | 404 MB | 启动包产物 |
| `build/riscv/sdk` | 398 MB | SDK 产物 |
| `build/host` | 39 MB | 阶段证据 |

关键点：

- smoke 运行目录共 **10,551 个**（arm64 5,283 + riscv 5,268），每个约 **185 个文件、1.5–4 MB**，其中 `tables/` 是主机逐页审计留下的 **133 个 pmemsave 页文件**；另有 `runtime.dtb`、`init-page-metadata.bin`、`result.json`、`symbols.log`、串口与 QEMU 输出、构建快照。
- 合计约 **158 万个文件**，`du`/`find`/`git status` 明显变慢。
- `tools/smoke.py`、`tools/run.py`、`tools/build.py` 中**没有任何 prune 或保留策略**；从项目开始至今的每次运行都还在。

## 影响

- 每次跑全量回归都会再增加数百 MB 到数 GB，单调增长；
- 大量小文件影响 APFS、备份与并行开发（新工作树若复制 `build/` 会多出 20 GB，只能重建）；
- 磁盘压力可能在无关时刻打断验证（例如 CI 或本地跑全量时空间不足）。

## 建议修复

1. **保留策略**：保留所有 `build/host/stage*/evidence.json` 引用到的运行目录，加每架构最近 N 个（N 可配，默认如 20），其余可删。
2. **工具支持**：`tools/smoke.py` 增加 `--keep N`；另加 `make clean-runs`，**默认 dry-run**，打印将删除的目录、保留理由与可回收空间，需显式 `CONFIRM=1` 才真正删除。
3. **runs/ 轮转**：保留最近 N 次构建，或压缩归档；不要无限累积编译日志。
4. 在 003（板端验收）补一句：板端证据同样需要保留策略，且优先级更高（板端跑一次代价大）。

## 验收

- `make clean-runs` 在 dry-run 下列出待删目录与回收量，不删任何文件；
- 真正清理后，所有 `build/host/stage*/evidence.json` 引用到的目录仍然存在且内容完整；
- 清理后 `make unit` 与任一 `CASE` 场景能正常重跑并生成新证据；
- 记录清理前后的 `build/` 体积与文件数对比。
