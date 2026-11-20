# 当前状态

更新：2026-11-21（Asia/Shanghai）。[设计入口](design/00-overview.md) · [协作规则](../AGENTS.md) · [代码规范](style.md)。历史执行与证据保留在各设计主题末尾，本页只作当前交接。

## 目标与工作约定

- 长期分支 `codex/kernel-development`。用户授权持续推进完整项目（包括 net），每个本地验收通过的点提交并推送；实质未定接口再问，保留用户改动，不兼容旧 ABI。
- 先完成可用 POSIX sh：T1 启动包/init → T2 fd/Fix、serial/tty → T3 blk/fs → T4 完整 fork/exec、pipe/dup2、作业控制与 sh。net、图形/GPU 机制排 M3 后；系统和图形机制就绪后最后做 GUI。13 的候选线协议尚未冻结。
- 设计、执行项和验收写对应主题；README 保持简洁，不另建 todo 文件。不得用局部 fixture、QEMU 或格式检查代替全项目、板端、CI 或性能验收。

## 已实现边界

- [01 硬件](design/01-hardware.md)：RV64/ARM64 启动/DTB、单主核调度及约 5ms timer、GICv2/PLIC、Device/IRQ/EVENT、其余核固件停放。RV 非零 hart/动态 PLIC S 上下文与四核矩阵已验；不是 SMP。真实板、PSCI SMC/EL2 降级、非一致 DMA/RV 物理缓存仍待验。
- [02 内存](design/02-memory.md)：预算/页/Space/Mem/Mmap、W^X/guard、SHARED/Device、COW 承诺/克隆/写分离、完整整数/FP现场。可信 fork 快照/准备与回滚已接，原生 fork 准入、长操作检查点/发布和完整 exec 仍待接。
- [03 进程](design/03-process.md)／[04 IPC](design/04-ipc.md)／[05 事件](design/05-events.md)：原生创建/准备装载/发布/撤销、固定 CONTROL/凭据/组会话/父 wait、生命周期观察、预付 Rq/取消/ACK/独立收尾、事件/闹钟、原生资源 reply。内核不解析文件/终端业务。
- [07 启动](design/07-boot.md)：T1.1..T1.5 默认生产 init 从真实包装载独立 serial、READY/监督，失败清理后诊断停机；实际 UART/双环/PIO/IRQ、控制台交接与有界日志已验。其他服务、完整 SYSTEM/设备发权、端点恢复与生产 DMA 仍待接。
- [11 信号/终端](design/11-terminal.md)：HANDLER/两层帧/cookie/sigreturn/sigsuspend、STOP/CONT、USER/CHILD/同步FAULT来源与原生 sigsend、普通等待中断、ttycreate/ttybind/getfg；规范FAULT原因/地址、直接帧与同号 pending 隔离已接。setfg/ttysignal、完整终端关联/挂断/孤儿策略及公开 POSIX trampoline/SA_RESTART 仍待接。
- [08 运行库](design/08-runtime.md)／[10 文件](design/10-filesystem.md)：独立libsys/csu、基础libc、静态装载及本地fd/File/连接/原RPC记录已接。HELLO/ATTACH/共享READ/控制CLOSE仍为独立fixture；Rctx/快照/IO/关闭worker、参数捕获及目标Boot打包已局部验证。公开POSIX前端、生产FS/TTY/File观察、标准fd API、完整exec路径/Fix尚未完成。
- 用户态 tty 行规程、输出转换与 VMIN/VTIME 等有界核心已验，尚非默认包里的 tty 服务。virtio 基础库不代表已有生产 DMA/blk/net；完整 sh、net、图形/GPU 与 GUI 均未交付。

## 最新完成点与证据

- stage165：lifeorphan只读判定已发布组的当前孤儿状态；同会话组外的存活父阻止孤儿化，停止成员仍算存活，完整PID/逻辑收养不随槽复用重绑。未发HUP/CONT、未开放setfg/ttysignal，无新增状态或权限。
- 6项主机专项及两架构实际已发布Run/Life的4次关系检查，覆盖空/构造、跨会话、同组父、停止、父退出及复用；原全部用户态创建/信号/连接恢复验收随完整矩阵重跑。
- 验收：完整make test-all退出0，1089项host、格式、195次双架构QEMU和168份journal通过；SDK/实际载荷/默认启动包审计通过，init/serial与stage164相同，clangd切回RV64/groups。
- 证据：`build/host/stage165-orphan-groups/`。首个RV预检固件路径拼写错，未执行客体，改回既有路径后通过；失败记录保留。无CI/板端/性能结果。stage157及后续提交暂未推送，仍待公开发布确认。
- 工具：LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构 `.su`/stack-usage.json。编译帧不是完整栈深/IRQ延迟证明，高半区物理ELF仍须禁用跳转表。

## 待确认与接续

- 待确认：生产rqclean的CONTROL_CLEAN=0x80、仅初始init持有及完整REQUEST参数方案已单独提问，尚未修改CONTROL/0x1b实现；候选及04/06旧表差异见04管理代收尾节。
- 待确认：堆/连接共用VA登记、保留128堆区域及8份连接预付责任、短屏蔽信号更新范围的方向已提问，见08；当前vm.c仍不承诺信号安全，未新增映射查询或内核保留区。
- 待确认：setfg/ttysignal的修正版后台/错误规则见11；旧“屏蔽/忽略TTOU也EIO”候选已撤回，修正版允许该情形继续切换。尚未新增ERR_IO=23或开放原生终端调用。
- 已确认的token定点ABORT、Image统一序号及创建记录与清理运输的类型化衔接已接。生产token授权/撤销、准备观察与服务私有in-flight仍待接；不把三角色fixture当成真实VFS/后端服务，规则见10篇。
- 创建前置、旧后端CLOSE、未回复创建、资源ABORT无RPC/已提交失败及首次VA冲突恢复已接；继续其他失败bootstrap、持久映射故障及待定VA/管理入口，再接完整装载/公开前端/Fix。公开POSIX trampoline/errno/SA_RESTART未完成；公开libc kill/raise未接，外部总线/FPE/SP对齐仍仅host分类检查。
- 继续装载/公开信号前端、服务私有in-flight/生命周期观察，再接VA分配、Fix、标准fd API及生产tty/fs/blk。保留完整fork/exec、管道/作业控制与sh/net/最终GUI目标，不把当前传输适配当作已完成服务。
- DMA设备实例代次域/固定解除方向已确认，具体权限/调用ABI仍须冻结；设备未停止前不得回收缓冲。图形线协议仍按13候选状态处理。

## 硬件与问题登记

- [issues](issues/README.md)：010 SDK导出、011运行证据保留已修；003无主机观测与007非一致DMA等继续，009排图形机制前；001/005/006的本地前置已有验收，真实板仍待执行。具体状态、修复与失败证据查各 issue 及01/09/14末尾，不将本地结果当作上板完成。
