# 001 早期 UART 基址仍为 QEMU 硬编码

状态：待处理　优先级：P0　关联约束：01#2（地址只能来自 DTB）

## 现象

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
