# 009 Device 租约不支持 RAM 型窗口

状态：待处理　优先级：P1　关联约束：01#3、01#6

## 现象

当前 Device 机制（`devwindow`/`devpage`/`devmap`/`devunmap`）只覆盖 DTB 中登记的 MMIO 设备窗口。`simple-framebuffer` 的 `reg` 指向固件预留的**普通 RAM**，不是 MMIO：按现有租约模型既拿不到合适属性，也拿不到授权。

## 影响

- 13 的 G2（`simple-framebuffer` 上屏）无法实现：dis 拿不到那块 RAM 的受检查映射；
- 若绕过租约直接用普通 Mem，会把"设备区间"混进通用内存模型，破坏 Acct 计费与隔离边界。

## 建议修复

在 09 增加 RAM 型设备窗口：

1. Device 记录增加 kind（MMIO / RAM）；RAM 型来自 DTB 保留区（`reserved-memory` 子节点或 `simple-framebuffer` 的 `reg`）。
2. 启动期把该区间从页管理器可用区排除，并登记为 Device 窗口，不进通用页池。
3. 租约语义沿用现有 Acct 域与"最后 unmap 结清"；dis 持 RW 映射，其他进程不可得。
4. 该窗口**不**授 DMA 域，也不参与 `dmaaddr`：首版由 CPU 直接写，避免把显示内存变成 DMA 授权面。

## 验收

- 内核/宿主测试：登记 RAM 型窗口后，该区间不出现在可用页中；
- 非授权 Acct 映射被拒；最后 unmap 后租约结清、页账目回基线；
- 与 13 的 G2 联合验证：dis 可写、可读回，像素摘要一致。

## 备注

本条只登记"内核侧需要提供什么"。图形侧的规格（能力位、选择顺序、全帧 blit）已在 13 冻结，不在本条重复。
