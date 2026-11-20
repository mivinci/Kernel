# 当前状态

更新：2026-11-20（Asia/Shanghai）。[设计入口](design/00-overview.md) · [协作规则](../AGENTS.md) · [代码规范](style.md)。历史执行与证据保留在各设计主题末尾，本页只作当前交接。

## 目标与工作约定

- 长期分支 `codex/kernel-development`。用户授权持续推进完整项目（包括 net），每个本地验收通过的点提交并推送；实质未定接口再问，保留用户改动，不兼容旧 ABI。
- 先完成可用 POSIX sh：T1 启动包/init → T2 fd/Fix、serial/tty → T3 blk/fs → T4 完整 fork/exec、pipe/dup2、作业控制与 sh。net、图形/GPU 机制排 M3 后；系统和图形机制就绪后最后做 GUI。13 的候选线协议尚未冻结。
- 设计、执行项和验收写对应主题；README 保持简洁，不另建 todo 文件。不得用局部 fixture、QEMU 或格式检查代替全项目、板端、CI 或性能验收。

## 已实现边界

- [01 硬件](design/01-hardware.md)：RV64/ARM64 启动/DTB、单主核调度及约 5ms timer、GICv2/PLIC、Device/IRQ/EVENT、其余核固件停放。RV 非零 hart/动态 PLIC S 上下文与四核矩阵已验；不是 SMP。真实板、PSCI SMC/EL2 降级、非一致 DMA/RV 物理缓存仍待验。
- [02 内存](design/02-memory.md)：预算/页/Space/Mem/Mmap、W^X/guard、SHARED/Device、COW 承诺/克隆/写分离、完整整数/FP现场。可信 fork 快照/准备与回滚已接，原生 fork 准入、长操作检查点/发布和完整 exec 仍待接。
- [03 进程](design/03-process.md)／[04 IPC](design/04-ipc.md)／[05 事件](design/05-events.md)：原生创建/准备装载/发布/撤销、固定 CONTROL/凭据/组会话/父 wait、生命周期观察、预付 Rq/取消/ACK/独立收尾、事件/闹钟、原生资源 reply。内核不解析文件/终端业务。
- [07 启动](design/07-boot.md)：T1.1..T1.5 默认生产 init 从真实包装载独立 serial、READY/监督，失败清理后诊断停机；实际 UART/双环/PIO/IRQ、控制台交接与有界日志已验。其他服务、完整 SYSTEM/设备发权、端点恢复与生产 DMA 仍待接。
- [11 信号/终端](design/11-terminal.md)：HANDLER/两层帧/cookie/sigreturn/sigsuspend、STOP/CONT、USER/CHILD来源与原生 sigsend、普通等待中断、ttycreate/ttybind/getfg；可信同步帧与同号 pending 隔离已接。同步硬件 FAULT handler/公开原因编码、setfg/ttysignal、完整终端关联/挂断/孤儿策略及 POSIX trampoline/SA_RESTART 仍待接。
- [08 运行库](design/08-runtime.md)／[10 文件](design/10-filesystem.md)：独立 libsys/csu、基础 libc/身份/wait/堆/数值/格式化、静态 ELF 装载；本地 fd 表、File alias/pin/关闭锚、稳定连接 MEM/VA、bootstrap 与原槽 RPC/后台记录已接。真实 HELLO/普通 ATTACH/共享 READ/控制 CLOSE 只在独立 fixture 验证；三层私有Rctx、返回窗口及原RPC责任绑定已局部验证。生产 FS/TTY/File 权限/观察、标准 fd API、Rctx数据快照/Boot/Fix 尚未完成。
- 用户态 tty 行规程、输出转换与 VMIN/VTIME 等有界核心已验，尚非默认包里的 tty 服务。virtio 基础库不代表已有生产 DMA/blk/net；完整 sh、net、图形/GPU 与 GUI 均未交付。

## 最新完成点与证据

- stage145：Rctx在调用前绑定已有Bootstrap/Op及精确claim/页租约，异步提交后唯一RPC移入原记录，等待后结果/资源归原记录；等待中断保留同一个RPC。handler前交接与正常返回共用幂等handoff，不自动cancel/ACK或安装fd，活动Rctx仍不可复用。
- 120字节路由置于原512字节元数据，银行仍50712字节，全银行序号不回绕；16KiB每层快照尚未接参数/共享数据复制。实际HELLO/READ四处首存储信号打断、独立内层调用和恢复通过，八个普通Rq满额下新登记路径的控制CLOSE通过。
- 验收：938项完整host、格式、6次双架构QEMU（libsys/sigframe/normal）与6份journal。源码/头/SDK、无libc依赖、实际链接载荷/窗口均核对，init/serial与stage144逐字节相同；默认包无测试模块，clangd数据库已切回RV64/libsys。
- 证据：`build/host/stage145-context-handoff/`中的unit/场景日志、verify.py及evidence.json。SDK隐式memset/memcpy及fixture初始化失败已修，原失败记录保留；本轮未重跑195场景矩阵，最近完整矩阵为stage144（159+36分段完成）。磁盘空间已恢复；历史执行见各主题末尾，无CI、实板或性能结果。
- 工具：LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构 `.su`/stack-usage.json。编译帧不是完整栈深/IRQ延迟证明，高半区物理ELF仍须禁用跳转表。

## 待确认与接续

- 两项已向用户提问，尚未收到回复，不提前实现：① Sigsel GROUP target=0 在内核受理时固定当前组，供 libc kill(0) 使用；② 同步 FAULT 的12项归一化原因及初次递送失败保留原故障终止策略。候选与边界见11末尾；原生正PGID/PID/CONTROL/ALL已实现，实际硬件故障当前仍默认受控终止。
- 下一步接正式信号前置取消与稳定数据快照、关闭中断/1秒控制故障与后台补偿、服务私有in-flight/生命周期观察；再接VA分配、Boot/Fix、标准fd API及生产tty/fs/blk。保留完整fork/exec、管道/作业控制与sh/net/最终GUI目标，不把当前传输适配当作已完成服务。
- DMA设备实例代次域/固定解除方向已确认，具体权限/调用ABI仍须冻结；设备未停止前不得回收缓冲。图形线协议仍按13候选状态处理。

## 硬件与问题登记

- [issues](issues/README.md)：010 SDK导出、011运行证据保留已修；003无主机观测与007非一致DMA等继续，009排图形机制前；001/005/006的本地前置已有验收，真实板仍待执行。具体状态、修复与失败证据查各 issue 及01/09/14末尾，不将本地结果当作上板完成。
