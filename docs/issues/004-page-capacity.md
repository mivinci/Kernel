# 004 物理页元数据上限 1 GiB

状态：待处理　优先级：P0　关联约束：01#9（容量留真机余量）

## 现象

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
