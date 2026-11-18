# 006 次核停放未约定

状态：待硬件（本地四核单主核验收已完成，缺上板确认；2026-11-18 确认为暂无实板）　优先级：P1　关联约束：01#8（单核是显式选择而非硬件假设）

## 现象

以下为原始缺口；当前实现与本地结果见末尾，不代表已提供SMP。

设计只在单核上运行，但没有约定启动后对其它核的处理。QEMU 默认单核，问题一直未暴露。

## 影响

真机多核上电后，次核可能进入未定义状态，干扰主核的内存与设备访问。

## 建议修复

在 01 写明停放约定：ARM64 用 PSCI `CPU_OFF`，RV64 用 OpenSBI HSM；停放必须早于页初始化与设备访问。

- 停放失败的处理（panic 或受限继续）要明确。
- 不声称支持 SMP；将来加核另立设计。

## 验收

QEMU `-smp 4` 下两架构启动成功且次核被停放（由 QMP 查询或串口打印确认）。

## 当前接续（2026-11-18）

初步接入CPU描述/固件后端与bootcpucheck时，已限定UART MMIO/页初始化前当前RUNNING、其余启用CPU STOPPED，未知/旧接口、错误/ON/PENDING拒启。PSCI CPU_OFF/SBI hart_stop只停调用自身，生产核对既有固件停放，不无差别关闭disabled管理核；自停放入口后续运行结果见下。

初步39项DTB/4项CPU模型与单核normal通过后，继续补了非零启动PE、PLIC和四核场景；不能以该早期结果替代本条最终验收。规格与证据见01末尾及build/host/stage100-cpu-bootstrap/。

专项阶段的四核normal/pages/真实IRQ/自停放共8次通过：RV确定hart1/context3，ARM逐个唤醒到无共享栈入口并自停放，CPU_OFF失败或仅WFI不能通过OFF校验。初始停放在页初始化/设备访问前确认，生产不主动唤醒核；HMP每次显式cpu-index。其后最终全量结果如下，真实板仍待验。

最终完整make test-all通过678项host/格式/167次两架构QEMU，包含8次四核矩阵和12次容量矩阵。162份固件准入快照与QMP CPU库存/寄存器核对，RV确定hart1/context3，PSCI/HSM实际自停放各三个次核，所有源码/共享头哈希匹配，生产链接无fixture。证据build/host/stage100-cpu-bootstrap/evidence.json/test-all.log；相关早期失败保留。本条本地单主核/停放验收完成；SMC/ARM非零affinity仅模型，真实板/EL2及固件交接仍待验，不是SMP支持完成。
