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
- [11 信号/终端](design/11-terminal.md)：HANDLER/两层帧/cookie/sigreturn/sigsuspend、STOP/CONT、USER/CHILD/同步FAULT来源、原生sigsend与libc raise、普通等待中断、ttycreate/ttybind/getfg；规范FAULT原因/地址、直接帧与同号pending隔离已接。kill、setfg/ttysignal、完整终端关联/挂断/孤儿策略及公开POSIX trampoline/SA_RESTART仍待接。
- [08 运行库](design/08-runtime.md)／[10 文件](design/10-filesystem.md)：独立libsys/csu、基础libc、静态装载及本地fd/File/连接/原RPC记录已接。HELLO/ATTACH/共享READ/控制CLOSE仍为独立fixture；Rctx/快照/IO/关闭worker、参数捕获及目标Boot打包已局部验证。公开POSIX前端、生产FS/TTY/File观察、标准fd API、完整exec路径/Fix尚未完成。
- 用户态 tty 行规程、输出转换与 VMIN/VTIME 等有界核心已验，尚非默认包里的 tty 服务。virtio 基础库不代表已有生产 DMA/blk/net；完整 sh、net、图形/GPU 与 GUI 均未交付。

## 最新完成点与证据

- stage188：未映射 DATA 的 fork 克隆直接遍历 Mem 别名链，拒绝未登记引用，释放后可重试。75 项主机专项、格式检查和双架构 QEMU sigframe 通过。单条别名链内的检查点仍待接入。

- stage187：fork 的 Signal 动作检查分批，活动帧写入前检查子映射私有性，防止共享栈改写父 cookie。36 项主机专项、格式检查及双架构 QEMU sigframe 通过。单个 Mem 的别名扫描仍待拆分。

- stage186：fork 的 Local 装载分批检查 Prep，部分装载超期可撤销资源引用。1187 项主机测试、格式检查和双架构 QEMU sigframe 通过。单个 Mem 的别名扫描和 Signal 阶段仍待拆分。

- stage185：fork 的 Mem 绑定扫描每至多八个源槽开放检查点，恢复后重验准备期限。23 项主机专项覆盖超期后草稿引用及父 PTE 回滚，双架构 QEMU sigframe 通过。

- stage184：当前 Run 的可信 fork 构造在映射批次之间接入内核根续点；双架构 sigframe 验证实际构造器 STOP/CONT 后完成构造并清理。未映射 Mem、Local、Signal 的检查点仍待接。

- stage183：快照从 READY 转入提交时重新检查准备期限，超期仍可回滚；组合构造在发布子信号状态及提交 Local/Mem 前通过此检查。22 项主机专项及双架构 QEMU sigframe 通过。

- stage182：未映射 DATA 的 fork 克隆核对完整源 Acct，拒绝同 Set 下其他 Acct 的对象。26 项主机 Space 快照专项、格式检查及双架构 QEMU sigframe 通过。

- stage181：带 Prep 的分批构造和未映射 Mem 克隆入口主动检查截止时间。21 项主机 fork 快照专项、格式检查及双架构 QEMU sigframe 通过。

- stage180：READY 快照只给目标根开放内核写入，供子信号 cookie 准备。源根拒绝写入，未冻结对端的 SHARED 写入通过。26 项主机 Space 快照专项及双架构 QEMU sigframe 通过。

- stage179：打开带 Prep 的快照前校验源、目标 Spaceid 及准备有效性。同 Acct 的其他根不能借用该准备。主机 fork 快照专项和双架构 sigframe 验证此路径，日志在 `build/host/stage179-fork-prep-root/`。

- stage178：Space 快照期间拒绝源和目标 Local 表的普通安装、撤销、继承修改和资源回复，允许已占用 IPC 槽归还。主机专项及双架构 QEMU sigframe 通过，证据在 `build/host/stage178-fork-local-gate/`。原生 fork 仍待接通。

- stage177：快照期间新私有DATA别名准入已补齐；源Mem已有冻结alias root时，向另一未冻结root的普通RO/RW/COW映射返回BUSY，不取PTE/引用/费用。内部子映射、SHARED/SEALED及独立Mem规则保持；未开放原生fork或一般并发VM。
- 6项host调用真实Space/Mem/Acct及两架构页表模型；旧版源码RO/RW/COW成功及RW可写PTE反例已记录。双架构可信内核root续点各6次拒绝、15次独立VM变更与STOP/CONT/终止回滚保持，固定记录/池不增。
- 验收：完整test-all退出0，1181项host、格式、195个双架构QEMU及168份内核诊断journal通过；16次生产启动、10条失败清理及累计运行库/文件终结路径、SDK/载荷/生产隔离均审计。所有构建后clangd切回RV64/sigframe。
- 证据：`build/host/stage177-snapshot-alias-gate/`。修复前3项反例及准确旧源码/哈希保留；init/serial ELF保持，最近完整矩阵为stage177，无CI/板端/性能结论。stage157及后续提交暂未推送，仍待公开发布确认。
- 工具：LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构 `.su`/stack-usage.json。编译帧不是完整栈深/IRQ延迟证明，高半区物理ELF仍须禁用跳转表。

## 待确认与接续

- fork接续：新DATA别名准入已闭合；Source Local/Mem引用变化、未映射/Local/Signal完整长循环与自动IRQ/检查点、原生准入/Fix/专用发布仍须接通，不把可信续点/快照验收当作已实现fork。
- 待确认：生产rqclean的CONTROL_CLEAN=0x80、仅初始init持有及完整REQUEST参数方案已单独提问，尚未修改CONTROL/0x1b实现；候选及04/06旧表差异见04管理代收尾节。
- 待确认：堆/连接共用VA登记、保留128堆区域及8份连接预付责任、短屏蔽信号更新范围的方向已提问，见08；当前vm.c仍不承诺信号安全，未新增映射查询或内核保留区。
- 待确认：setfg/ttysignal的修正版后台/错误规则见11；旧“屏蔽/忽略TTOU也EIO”候选已撤回，修正版允许该情形继续切换。尚未新增ERR_IO=23或开放原生终端调用。
- 待确认：公开kill与原生仅存活Run规则存在僵尸语义冲突；建议普通选择器纳入未收取ENDED Life，授权后成功但不排队，快照上限从64 Run改256 Life，CONTROL保持存活规则。见11候选；未改内核或提供受限kill封装。
- 已确认的token定点ABORT、Image统一序号及创建记录与清理运输的类型化衔接已接。生产token授权/撤销、准备观察与服务私有in-flight仍待接；不把三角色fixture当成真实VFS/后端服务，规则见10篇。
- 块缓存、日志顺序与快照退休衔接已局部验收；10篇magic/CRC精确参数候选已提问待确认，全部整数端序、inode flags及目录type等仍须收口；再接盘上编码、逻辑事务/分类、恢复及blk运输，不以内部顺序/门禁代替持久证明。
- 创建/CLOSE、资源ABORT、map/复用失败、bootstrap/创建者终结及普通读写槽收尾已局部接通；继续File/槽生命周期观察与整体终结协调、待定VA/管理入口，再接完整装载/公开前端/Fix。公开POSIX trampoline/errno/SA_RESTART未完成；raise已接、kill待上文确认，外部总线/FPE/SP对齐仍仅host分类检查。
- 继续装载/公开信号前端、服务私有in-flight/生命周期观察，再接VA分配、Fix、标准fd API及生产tty/fs/blk。保留完整fork/exec、管道/作业控制与sh/net/最终GUI目标，不把当前传输适配当作已完成服务。
- DMA设备实例代次域/固定解除方向已确认，具体权限/调用ABI仍须冻结；设备未停止前不得回收缓冲。图形线协议仍按13候选状态处理。

## 硬件与问题登记

- [issues](issues/README.md)：010 SDK导出、011运行证据保留已修；003无主机观测与007非一致DMA等继续，009排图形机制前；001/005/006的本地前置已有验收，真实板仍待执行。具体状态、修复与失败证据查各 issue 及01/09/14末尾，不将本地结果当作上板完成。
