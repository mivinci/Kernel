# 001 早期 UART 基址仍为 QEMU 硬编码

状态：待硬件（实现与 QEMU 修复已完成，缺上板确认；2026-11-18 确认为暂无实板）　优先级：P0　关联约束：01#2（地址只能来自 DTB）

## 现象

以下为原始缺陷，2026-11-18已移除两处固定基址；当前落点与未验项见末尾。

- `kernel/riscv/console.c:10` 的 `uartbase` 默认值为 `0x10000000`。
- `kernel/arm64/console.c:10` 的 `uartbase` 默认值为 `0x09000000`。
- 两者都是 QEMU virt 的地址；DTB 解析后由 `kernel/vm.c:113` 的 `hal_debug_rebase(KIO + uartoffset)` 重建为高半区映射。

## 影响

DTB 解析之前的输出会写到错误地址。真机上这段是**静默**的：早期 panic、页初始化失败、DTB 解析失败都看不到任何输出，等于盲调。

## 建议修复

1. 引入"早期 UART 来源"策略并写入 01，三选一：板级编译期默认地址；Bootloader 通过启动参数/DTB fixup 传入；从 Bootloader 已解析的 DTB 中尽早取 UART 节点。
2. 保留 `hal_debug_rebase` 作为切到高半区后的重建路径。
3. 该路径仍是架构诊断例外，不扩展到输入/中断。

## 验收

- QEMU 上把默认地址改成错误值，验证"DTB 解析前仍能出字"。
- 真机上电后能在 DTB 解析前输出一行版本号。

## 修复落点（2026-11-18）

采用DTB早期分阶段解析，规格与失败语义见[01早期UART](../design/01-hardware.md#早期uart必须先取得dtb描述2026-11-18issue-001)。kernel/console.c初始地址为0，只有经dtconsole验证的chosen/stdout-path可启用MMIO；移除两架构旧console与ARM固定时钟分频，高半区rebase保留。新增主机反例与两架构真实启动反例，证据归build/host/stage95-early-console/。

因选择了DTB来源，必须先验证其结构/控制台部分才能输出，不能在任何DTB读取之前保证串口可用；缺失/坏控制台描述安全停止、不猜地址。原“默认地址改错”反例由“无默认地址、坏stdout不回退”替代；完整RAM/IRQ发现之前仍可输出并诊断后续失败。真实板上电尚未执行，固件需先准备时钟/pinmux/波特率；dw-apb-uart及板DTB容量由005继续处理，故不标整项已修复。
