# 005 DTB 解析容量与 compatible 匹配

状态：待处理　优先级：P0　关联约束：01#2、01#9

## 现象

- `kernel/dtb.c:3-5`：`NODES=192`、`PROPS=1536`、`DEPTH=16`；`include/dtb.h`：`DT_DEVICES=64`、`DT_REGS=2`、`DT_IRQS=4`。
- QEMU virt 的 DTB 仅 9,283 字节；真机 DTB 约 40–90 KiB、300–800 节点。
- compatible 匹配按 QEMU 字符串写（`ns16550a`、`arm,pl011`、`virtio,mmio`），真机为 `snps,dw-apb-uart`、`arm,gic-400`、`sifive,plic-1.0.0`、`snps,dw-mshc`、`snps,dwmac-*`。

## 影响

真机 DTB 会中途被拒或静默截断；`DT_REGS=2` 会丢多 reg 设备的窗口；compatible 不匹配会被误判为"设备不存在"。

## 建议修复

1. 上调 `NODES/PROPS/DEPTH/DT_DEVICES/DT_REGS`，取值与依据记入 status。
2. 增加上述 compatible 别名；`snps,dw-apb-uart` 按 16550 处理。
3. 把真实板 DTB 纳入主机测试：以 NEO3、VisionFive 2 的 DTB 为输入，断言 UART/GIC/PLIC/内存/reserved 解析结果。
4. 解析失败必须显式报错并停在该阶段，不静默继续。

## 验收

新增 DTB 解析主机测试在两块真机 DTB 上通过；两架构 `CASE=pages` 不回归。
