# 当前状态

更新：2026-11-21（Asia/Shanghai）。[设计入口](design/00-overview.md) · [协作规则](../AGENTS.md) · [代码规范](style.md)。历史执行与证据保留在各设计主题末尾，本页只作当前交接。

## 目标与工作约定

- 长期分支 `codex/kernel-development`。用户授权持续推进完整项目（包括 net），每个本地验收通过的点提交并推送；实质未定接口再问，保留用户改动，不兼容旧 ABI。
- 先完成可用 POSIX sh：T1 启动包/init → T2 fd/Fix、serial/tty → T3 blk/fs → T4 完整 fork/exec、pipe/dup2、作业控制与 sh。net、图形/GPU 机制排 M3 后；系统和图形机制就绪后最后做 GUI。13 的候选线协议尚未冻结。
- 设计、执行项和验收写对应主题；README 保持简洁，不另建 todo 文件。不得用局部 fixture、QEMU 或格式检查代替全项目、板端、CI 或性能验收。

## 已实现边界

- [01 硬件](design/01-hardware.md)：RV64/ARM64 启动/DTB、单主核调度及约 5ms timer、GICv2/PLIC、Device/IRQ/EVENT、其余核固件停放。RV 非零 hart/动态 PLIC S 上下文与四核矩阵已验；不是 SMP。真实板、PSCI SMC/EL2 降级、非一致 DMA/RV 物理缓存仍待验。
- [02 内存](design/02-memory.md)：预算/页/Space/Mem/Mmap、W^X/guard、SHARED/Device、COW 承诺/克隆/写分离、完整整数/FP现场。fork 快照、回滚与长操作检查点已局部验证；完整 exec 仍待接。
- [03 进程](design/03-process.md)／[04 IPC](design/04-ipc.md)／[05 事件](design/05-events.md)：原生创建/准备装载/发布/撤销、forkprep/forkcommit/forkabort、固定 CONTROL/凭据/组会话/父 wait、生命周期观察、预付 Rq/取消/ACK/独立收尾、事件/闹钟、原生资源 reply。内核不解析文件/终端业务。
- [07 启动](design/07-boot.md)：T1.1..T1.5 默认生产 init 从真实包装载独立 serial、READY/监督，失败清理后诊断停机；实际 UART/双环/PIO/IRQ、控制台交接与有界日志已验。其他服务、完整 SYSTEM/设备发权、端点恢复与生产 DMA 仍待接。
- [11 信号/终端](design/11-terminal.md)：HANDLER/两层帧/cookie/sigreturn/sigsuspend、STOP/CONT、USER/CHILD/同步FAULT来源、原生sigsend与libc raise、普通等待中断、阻塞 waitpid 的 SA_RESTART、ttycreate/ttybind/getfg 已接。公开 sigaction 支持单参数、SA_SIGINFO、已绑定 Rbank 的 Rctx 前置和私有 fork 返回的子 Fix 前置。kill、setfg/ttysignal、完整终端关联/挂断/孤儿策略仍待接。
- [08 运行库](design/08-runtime.md)／[10 文件](design/10-filesystem.md)：独立libsys/csu、基础libc、静态装载及本地fd/File/连接/原RPC记录已接。HELLO/ATTACH/共享READ/控制CLOSE仍为独立fixture；Rctx/快照/IO/关闭worker、参数捕获及目标Boot打包已局部验证。公开POSIX前端、生产FS/TTY/File观察、标准fd API、完整exec路径/Fix尚未完成。
- 独立 `sbin/tty` 已构建，异步 READY、规范行与双环回显已在启动包夹具中验证；默认 init 上线、终端授权和文件协议仍待接。virtio 基础库不代表已有生产 DMA/blk/net；完整 sh、net、图形/GPU 与 GUI 均未交付。

## 最新验证

- stage317：TTY 回显暂存受背压时仍可处理 RX 队首的 ISIG 字符。RV64/ARM64 QEMU `tty`、46 项主机回归和格式检查通过。
- stage316：私有运行库入口串接本地 fork 的准备、Fix 发布和子返回。RV64/ARM64 QEMU `sigframe` 验证跨两代复用同一 Fix 地址及发布前失败清理；47 项主机回归和格式检查通过。
- stage315：子运行库消费 Fix 后立即归还映射，信号前置和正常返回共用一次性清理。RV64/ARM64 QEMU `sigframe`、47 项主机回归和格式检查通过。
- stage314：fork 快照打开、映射构建及未映射 DATA 克隆保留 Prep 的超期和撤销状态。103 项主机专项、RV64/ARM64 QEMU `sigframe` 回归和格式检查通过。
- stage313：fork 快照提交时到期返回 `ERR_TIMEDOUT`，并完整回滚。94 项主机专项、RV64/ARM64 QEMU `sigframe` 回归和格式检查通过。
- stage312：RV64/ARM64 QEMU `sigframe` 验证硬件 timer IRQ 在可信 fork 构造检查点触发让出、就绪进程先运行和父续点恢复；费用与页回到基线，格式通过。
- stage311：未映射 DATA 的别名登记与根冻结可按 32 项开放 fork 检查点；70 项主机专项、双架构 QEMU `sigframe` 和格式检查通过。
- stage310：fork 快照期间禁止激活与冻结根共用可写私有 DATA 的用户根；双架构 QEMU `sigframe`、`jobs`、54 项主机专项及格式检查通过。
- stage309：fork 快照期间，内核写入及复制在改动任何字节前检查整个目标区间；双架构 QEMU `sigframe`、54 项主机专项及格式检查通过。
- stage308：fork 快照保护已有私有 DATA 别名免于提前解除映射；双架构 QEMU `sigframe`、53 项主机专项及格式检查通过。
- stage307：libc 提供 C locale 的 `fnmatch`；双架构 QEMU `runtime`、主机匹配比对和格式检查通过。目录枚举及 `sh` 接入仍待实现。
- stage306：libc 提供 C locale 的 `strcoll`、`strxfrm`；双架构 QEMU `runtime` 和格式检查通过。
- stage305：TTY `GETATTR` 可沿原 REQUEST 返回 Termarg；双架构 QEMU `runtime` 验证页末输出及越界拒绝，31 项主机测试和格式通过。生产分派仍待接入。
- stage304：TTY 字节间计时已接入待处理 READ；到期先回复旧数据，保留环中下一字节。双架构 QEMU `runtime`、31 项主机测试和格式通过。
- stage301：已终结的文件连接在引用与运输清理完成后归还目录槽；旧代次不能清理复用槽。双架构 QEMU `runtime`、59 项主机测试和格式通过。
- 历史验收见各设计主题末尾；最近一次完整双架构矩阵见[内存 stage177](design/02-memory.md#快照期间的新data别名准入2026-11-21)。

## 待确认与接续

- 待确认：生产 TTY/VFS 首次发放 `OBSERVE_SOURCE` 的方式，见[文件连接观察](design/10-filesystem.md)；目前仅夹具接通。
- 待确认：后端继承引用的 `reference_id` 命名空间及 Bootfd 交付格式，见[文件继承](design/10-filesystem.md)和[Boot](design/07-boot.md)；实际启动文件表仍未接通。
- fork接续：原生准入、Fix 映射校验和专用发布已有双架构用户载荷验收；继续核对 Source Local/Mem 引用变化和剩余长循环，再接服务继承及完整运行库路径。
- 待确认：生产rqclean的CONTROL_CLEAN=0x80、仅初始init持有及完整REQUEST参数方案已单独提问，尚未修改CONTROL/0x1b实现；候选及04/06旧表差异见04管理代收尾节。
- 待确认：堆/连接共用VA登记、保留128堆区域及8份连接预付责任、短屏蔽信号更新范围的方向已提问，见08；当前vm.c仍不承诺信号安全，未新增映射查询或内核保留区。
- 待确认：setfg/ttysignal的修正版后台/错误规则见11；旧“屏蔽/忽略TTOU也EIO”候选已撤回，修正版允许该情形继续切换。尚未新增ERR_IO=23或开放原生终端调用。
- 待确认：公开kill与原生仅存活Run规则存在僵尸语义冲突；建议普通选择器纳入未收取ENDED Life，授权后成功但不排队，快照上限从64 Run改256 Life，CONTROL保持存活规则。见11候选；未改内核或提供受限kill封装。
- 已确认的token定点ABORT、Image统一序号及创建记录与清理运输的类型化衔接已接。服务私有在途账本及已接入客户端的退出观察已在真实 IPC 夹具验证；生产 VFS/TTY 接入、token 授权/撤销和准备观察仍待接，规则见10篇。
- 块缓存、日志顺序与快照退休衔接已局部验收；10篇magic/CRC精确参数候选已提问待确认，全部整数端序、inode flags及目录type等仍须收口；再接盘上编码、逻辑事务/分类、恢复及blk运输，不以内部顺序/门禁代替持久证明。
- 创建/CLOSE、资源ABORT、map/复用失败、bootstrap/创建者终结及普通读写槽收尾已局部接通；继续File/槽生命周期观察与整体终结协调、待定VA/管理入口，再接完整装载、公开前端与服务继承。kill待上文确认，外部总线/FPE/SP对齐仍仅host分类检查。
- 继续装载/公开信号前端、服务私有in-flight/生命周期观察，再接VA分配、Fix、标准fd API及生产tty/fs/blk。保留完整fork/exec、管道/作业控制与sh/net/最终GUI目标，不把当前传输适配当作已完成服务。
- DMA设备实例代次域/固定解除方向已确认，具体权限/调用ABI仍须冻结；设备未停止前不得回收缓冲。图形线协议仍按13候选状态处理。

## 硬件与问题登记

- [issues](issues/README.md)：010 SDK导出、011运行证据保留已修；003无主机观测与007非一致DMA等继续，009排图形机制前；001/005/006的本地前置已有验收，真实板仍待执行。具体状态、修复与失败证据查各 issue 及01/09/14末尾，不将本地结果当作上板完成。
