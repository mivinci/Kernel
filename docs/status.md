# 当前状态

更新：2026-11-19（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 用户新增设计commit `ddaed1d`：优先M3可用sh，T1启动包/init→T2 fd/Fix及serial/tty→T3 blk/fs→T4完整sh；fork/exec、pipe/dup2与作业控制是关键前置。2026-11-18明确GUI仍属最终目标：net/图形GPU机制等排M3后，系统可通过sh正常使用且其他能力及图形/GPU机制就绪后，最后推进GUI。13候选协议仍未冻结，不提前实现。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP现场、单主核调度及约5ms timer已接；UART/页初始化前通过PSCI/HSM确认其余启用核停放，非零RV hart/动态PLIC S上下文本地四核已验，不是SMP。ARM64 I/D及寄存器已验，DMA/RV物理cache/板端未验。GICv2/PLIC、Device、IRQ_BIND/ACK/解绑/EVENT已接；bind要求CONTROL IRQ+IRQ_SOURCE BIND，SOURCE删除不级联绑定。初始SYSTEM QUERY与所选标准UART的实际MMIO/SOURCE已接，其他SYSTEM/全设备生产发权仍待接。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。Mem内容状态保留到最终回收，managers=0表示退役；现接SEALED规范backing多代视图，Mem96字节，现有临时DATA视图自持的原页接管账本。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；现接可信独占页MMIO视图、固定Device属性与Acct映射租约；现接可信完整成员整页组与规范化租约；现接全局零阶容量及COW额度双重承诺、兑现/取消/旧页转交及原出资者替换基础（Acct64字节）；现接可信逻辑页组/预付分支、DATA Mem克隆及未映射写分离，现接PTE COW位、全部别名分离提交及真实用户DATA写fault原PC重试；现接内核代写/copy及COW栈信号帧；现接可信整个Space映射快照/去重及跨Mem失败回滚；现接独立Local继承/原Handle与墓碑/私有视图重绑定；现接独立fork信号动作/活动帧cookie继承及HAL子返回现场派生，现由可信forksnapshot统一Space/Local/未映射Mem/保底/Signal/现场的同步提交与后段回滚；现接可信Proc/Prep构造、取消/期限及父退出即时撤销；原生fork准入/分批检查点/专用发布与生产发权/DMA仍未接。
- Local48、Life128、Proc512、Prep256、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权；生产PID1已安装16 Set对应的CREATE SERVICE权限，默认serial实际发布/READY已接。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure保底及CALL资源预留已接；原生56字节描述符快照可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表，可信CPU桥已移除。FS/TTY上层连接去重/补偿仍未接。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置支持DEFAULT/IGNORE、CHLD NOCLDWAIT及mask/pending；创建固定IGNORE/mask基线。现接可信类型化安装/排队、两层用户帧与原生sigreturn，校验映射/顶层/cookie与HAL权限，恢复完整GPR/FP/mask；现接原生HANDLER两入口检查、sigsuspend独占等待/M-W-H恢复及libc集合/mask/pending/suspend。CHILD真实退出来源已接，USER/FAULT生产、STOP/CONT、普通阻塞中断/重启及POSIX sigaction前置未接。
- 独立libsys/virtio基础库、csu/main、可写argv/environ、基础libc/身份/组会话/wait/堆、C locale数值与格式化及用户态ELF装载已接。默认init从实际包/清单装载独立serial ELF，含真实PIO/IRQ、两环、异步READY和精确init控制，两架构全量已验。普通用户ELF装载尚非FS稳定会话；fd/Fix适配、完整spawn/fork/exec未接，virtio尚无生产DMA。
- T1.1..T1.5已本地验收：默认init服务装载/READY/监督与失败清理后诊断停机已接。normal含真实init/serial ELF，16 Set/CREATE SERVICE、128槽、固定CONTROL、标准UART/IRQ发权与Boot格式2已用；其他设备和管理域仍待接，不是完整服务管理或可用sh。

## 最新本地验证

- tty输入核心通过805项host/格式及189次双架构QEMU，162份日志和源码/SDK哈希匹配。规范行编辑/EOF/有界短读/逻辑回显与信号动作、原始字节及满队列专项通过；默认init/serial ELF与stage117相同。证据stage118-tty-input/，无CI/板端/性能结果；输出转换历史证据见11/stage116，尚非tty服务或可用终端。

- 默认init/serial通过792项host/格式及189次双架构QEMU，162份日志与源码/SDK/配置/包ELF哈希匹配。16次实际启动含128 MiB..8 GiB、四核及非零RV启动hart，READY后阻塞/外部IRQ输入与父子完整PTE/物理owner/费用审计通过；10次生产失败均清理回初始PID1费用后诊断停机。证据stage117-default-init/，无CI/板端/性能结果。

- 独立serial通过763项host/格式及171次双架构QEMU，144份日志和源码/SDK哈希一致。原生测试PID1装载与独立构建逐字节相同的生产ELF，验证异步READY、精确控制/晚绑定、实际阻塞后分两批外部输入、24字节输出、IRQ重新启用及10秒READY超时/收尾。PL011判空/清中断竞态有旧C失败反例，已改清后重查；生产init/serial链接无fixture。证据stage112-serial-service/，无CI/板端/性能结论。

- 010 SDK导出与011运行保留工具已修复。011当时清理8,326个目录，15,699个保留目录/174万文件内容与集合不变，统计23.42→17.19 GB；规则见14末尾，证据stage110-run-retention/。控制台交接/64 KiB日志历史证据及边界见07/14，不在本页重复阶段记录。

- native审计逐页核对完整用户/内核映射、ELF代码/RO字节、栈/guard、Boot/SELF资源、隔离包视图和精确Acct owner；用户initstate显示正确目录/字节数/checksum并阻塞，私有页/页表/内核栈收费与实际owner集合一致，包仍保留且不重复计费。早期私有store符号被优化拆分及RV idle sie假设已直接观测后修正，失败记录保留。完整fork仍待T4前补源别名/长循环/IRQ检查点与STOP/CONT。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- stage118已本地验收：用户确认的规范行/禁用cc/编辑/ISIG清空规则已写11，私有有界输入核心已接并通过全量。只输出逻辑回显/信号/输出清空动作，VMIN/VTIME、真正前台信号授权、读提交/取消与tty服务仍待接，不改默认包。

- stage117已本地验收：默认包/生产init已接实际serial装载、READY、监督与失败清理后诊断停机；五类生产失败包括运行中坏环退出。活父子页表/两栈/共享环/各Acct与物理owner精确审计，失败回初始PID1费用，详见07末尾。

- stage116已本地验收：Termarg同源布局、参数校验和独立于serial/PTY的有界OPOST/ONLCR输出转换已接；接续行规程和服务循环，tty尚未进入默认包，详见11末尾。

- stage115已本地验收：真实退出CHLD的SOURCE_CHILD元数据按提交时Life父/ruid/策略排队，wait/观察责任独立；782项host/格式、179次双架构QEMU及152份journal/源码/SDK哈希核对通过，真实用户handler与后续wait均验证。证据stage115-child-signal/，无CI/板端结果。USER/FAULT生产、STOP/CONT与普通阻塞中断仍待接，不将来源常量视为完整信号实现。

- stage114已接原生资源reply与ipc_reply_resource并通过全量；可据此继续受限LOOKUP/FS/TTY连接策略，不把底层交付当成完整连接。

- 2026-11-19用户确认ABI冲突收口：debughandoff保留0x74，未实现的shutdown改为0x76，reboot保留0x75；已同步06，无旧编号兼容层。

- stage113管理引擎已由stage117接入默认init；初始init仅获MONITOR无GRANT，仍须固定CONTROL QUERY作basis，父wait/镜像确认/缓冲回收独立。2026-11-19确认的首次必需serial清理后诊断停机已实现，其他服务/恢复方向按07/09接续。

- 下一步：接tty的VMIN/VTIME读状态和后端/请求循环、前台授权与真实信号，再按T2..T4接fd/Fix、blk/fs与完整sh，不缩减fork/exec、net及最终图形/GUI目标。

- 2026-11-18确认的反向bit32=RX_ROOM/bit33=TX_READY与晚绑定已按07/11落定v1 BIND/STATUS及最近结果缓存，serial侧已实现/本地验；生产tty配对和init编排仍待接，绑定前不发布tty连接。

- 2026-11-18用户集中确认五项：控制台交接0x74/CONSOLE0x20；UART库存data[6..7]布局及serial七参数；资源reply描述符a1/x1；Siginfo来源1/2/3；DMA代次域/固定解除方向。控制台交接及UART库存/七参数解析已实现并本地验证；其余实现仍待接，DMA具体权限/调用ABI仍须冻结。
- issues：010 SDK导出与011运行证据保留/清理已修复；003无主机观测试点与007按已确认DMA方向接续，009排图形机制前，001/005/006等待实板。控制台交接已验，继续实际serial服务，不把本地结果当作上板完成。

- 001早期UART、004完整8 GiB、005板描述/设备发现、006单主核/停放与007 ARM64开启前置均已有本地交付，真实板及007非一致DMA/RV后端仍待验。接续生产SYSTEM/设备库存与真实serial装载/READY，再按T1..T4推进sh；不能把本地前置当作M3或全项目完成。
- 005解析切片已通过本地全量：1024节点/8192属性/32深度、4 reg/16 IRQ，设备仍64项；完整固定来源树在tests/fixtures/dtb。VF2原始树、NEO3显式测试适配树read/console通过，NEO3原始树拒绝，不放宽零长度检查。原始/适配/固件dump区别见01末尾。005仍需存储/网络设备发现及板端验证；006仍需启动hart/CPU选择与停放，不能把多hart拓扑解析当成SMP或上板已实现。
- 005后续已补MSHC/DWMAC家族/具体别名与多IRQ库存，完整板树实际SD/MMC/GMAC窗口及IRQ已核对且完整回归通过；不写设备寄存器或授DMA/生产Handle。家族不代表寄存器版本/PHY/时钟/复位已实现，实际blk/net驱动仍待完成。设备发现本地验收与板端运行严格分开，详见01/005末尾。
- 007的ARM64开启前置已通过本地全量：入口M/C关闭、独占启动RAM/表页及最后镜像/栈同步，随后M/C/I开启；保留代码/页表同步，主机命名管道GDB独立读SCTLR/TCR/MAIR。漏I-cache反例仅证明控制状态审计，不是漏DMA维护的旧数据反例；007仍留非一致DMA/RV后端/板端待验。接续006的启动CPU/控制器选择及停放，避免在等待生产UART/DMA协议确认时停止全项目进展。
- 006完整本地验收通过：固件单冷入口、当前RUNNING/其余启用CPU STOPPED，异常清空快照并拒启；正常不唤醒核，CASE=cpus实际唤醒到无栈入口后自停放。四核8次与全量均通过；Hirq保持64字节，按CPUINTC选S上下文，主机每条HMP显式cpu-index。SMC/ARM非零affinity仅模型边界、板端/EL2降级未验，不称SMP；详细证据/失败记录见01/006末尾。
- T1.4初始SYSTEM QUERY/UART库存已用于默认serial启动：Boot352字节/四项含tag7，精确初始Image绑定、真实MMIO RW/GRANT与IRQ_SOURCE BIND，查询不重装授权；失效STALE。data[6]=shift/data[7]=width及对齐验证已接，结构大小不变；Set/domain CONTROL及其他SYSTEM业务仍待接。
- 生产UART/早期诊断交接已接且默认init实际调用：debughandoff 0x74、精确初始SYSTEM CONSOLE=0x20，确认FIFO/移位寄存器排空后交接，忙则BUSY；普通输出仅进64 KiB日志，panic屏蔽IRQ后恢复固件配置并回放交接后保留记录再停机。两架构全量已验，详见07/14。
- serial Boot七参数/私有tag、16 KiB双SPSC环、PIO/IRQ、异步READY及BIND/STATUS均已本地验，默认init编排已接；实际tty配对/端点恢复仍待接，没有活环自动重置。RESOURCE不暴露Mem底层身份或NOTIFY掩码，真实MMIO/两个独立SHARED及精确双位通知仍由可信加载器保证，内核强制映射属性和通知权限，见07/09/11。
- 资源reply已按a1/x1指针、余未用参数0、a5=REPLY_RESOURCE接入，reply_recv保持普通消息；细节与验证见04/06。
- Siginfo来源code USER=1/CHILD=2/FAULT=3及各来源字段已确认；CHILD真实退出已接，USER/FAULT生产仍待接，见11。
- IRQ_SOURCE独立撤销及原生bind已接，SOURCE不转授/继承；初始标准UART的BOOT_RESOURCE已有实际MMIO/IRQ_SOURCE Handle，SYSTEM QUERY已接，其他设备/布局和管理业务仍后续，见05/07末尾。
- DMA设备实例代次域及固定解除方向已确认，具体权限/调用ABI仍须冻结，见09；缓存反例主机模型不代表DMA/HAL已实现。
- 继续完成真实信号来源/完整递送与阻塞中断、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
