# 004 物理页元数据上限 1 GiB

状态：已修复（本地验收，非板端验收）　优先级：P0　关联约束：01#9（容量留真机余量）

## 现象

以下是原始缺陷，2026-11-18的实现修复见末尾。

`kernel/main.c:52` 启动检查：`hardware.ram[i].size / PAGE_SIZE > (1UL << 18) - count` 即 `panic("RAM metadata capacity")`，上限 2^18 页 = 1 GiB。

## 影响

- NanoPi NEO3（1 GB）正好压线。
- VisionFive 2 最低配 2 GB，**当前上电即 panic**；8 GB 变体更甚。

## 建议修复

1. 上限提到 `1UL << 19`（2 GiB，元数据约 16 MiB）；8 GB 需 `1UL << 21`（约 64 MiB），或启动期只登记前若干 GiB 并打印裁剪说明。
2. 元数据预留沿用"从可用 RAM 取最大块"逻辑；容量不足必须 panic，不静默截断到未知状态。
3. 各板实际上限与裁剪策略记入 status。

## 验收

host 测试覆盖 1/2/8 GiB 容量判定；QEMU 用既有 `RAM=512` 与新增大内存配置各跑 `pages` 与 `normal`。

## 修复落点（2026-11-18）

kernel/page.c的只读pgplan按完整RAM计数并规划连续元数据，上限2^21页/8 GiB，最大物理跨度8 GiB；选最大可用区，不静默裁剪。直接映射、DTB/包候选读取窗口同步扩容，设备/栈窗口移开以免4 GiB以上RAM覆盖KIO。布局字面值统一include/layout.inc；详见[02末尾](../design/02-memory.md#完整ram元数据与8-gib直接映射2026-11-18issue-004)。

最终完整make test-all通过653项host、格式和158次两架构QEMU；make test-memory的两架构512 MiB/2 GiB/8 GiB pages与normal共12次矩阵全部通过。8 GiB完整页数/64 MiB元数据、真实高PA读写/耗尽/回收及生产PID1的完整PTE/Acct核对通过；全部结果核对当前源码/共享头哈希。证据build/host/stage96-memory-capacity/evidence.json与test-all.log；真实板仍待验。
