# 005 DTB 解析容量与 compatible 匹配

状态：进行中（解析切片已本地验收，扩展设备/板端待验）　优先级：P0　关联约束：01#2、01#9

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

## 当前接续（2026-11-18）

未提交实现已将结构容量改为1024节点/8192属性/32深度，设备仍64项（现有共享页成员以u64表示，不能盲增），每设备4个reg/16个IRQ，最多16个启用CPU描述；增加dw-apb-uart/GIC-400匹配，保留多hart控制器拓扑但不激活SMP。完整实现/失败语义与验收边界见01末尾；生产UART记录扩展仍待07的用户确认。

已固定来源研究样本位于/private/tmp/kernel-board-dtb-research-20261118，原始文件和依赖blob哈希已核对。VF2原始树现在dtread/dtconsole通过；NEO3厂商原始树缺RAM/stdout且有零长度splash占位，显式测试引导适配后dtread通过，但早期窗口检查仍拒绝另一处零长度USB PHY reg。不能把适配树叫固件实测dump，也不能据此标整板可启动。尚待纳入可复现样本、补齐NEO3边界及全量回归，不提交半份005。

后续已纳入tests/fixtures/dtb的完整树与provenance/显式适配。NEO3不修改解析器拒绝零长度的规则，而在测试适配中停用该PHY并删无效reg；适配树的read/console均通过，原始树继续拒绝。37项DTB与13项设备映射专项通过，旧设备fixture固定2窗口索引已修正；完整回归运行中，失败日志保留。板端内存/固件保留、时钟/复位及CPU/控制器选择尚未验收，本项仍不标已修复。

最终解析切片通过完整658项host/格式/158次QEMU，终稿另完整make unit通过659项（DTB专项38项），所有QEMU源码/共享头哈希匹配终稿。12次内存矩阵及实际IRQ/生产PID1仍通过，证据build/host/stage97-board-discovery/evidence.json与原始日志。VF2完整原树通过，NEO3适配显式留源且与原树拒绝分开；未把无效reg普遍放行。存储/网络compatible发现及板端/启动CPU选择仍未完成，维持进行中。
