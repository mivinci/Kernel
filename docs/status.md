# 当前状态

更新：2026-11-18（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 用户新增设计commit `ddaed1d`：优先M3可用sh，T1启动包/init→T2 fd/Fix及serial/tty→T3 blk/fs→T4完整sh；fork/exec、pipe/dup2与作业控制是关键前置。2026-11-18明确GUI仍属最终目标：net/图形GPU机制等排M3后，系统可通过sh正常使用且其他能力及图形/GPU机制就绪后，最后推进GUI。13候选协议仍未冻结，不提前实现。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP现场、单主核调度及约5ms timer已接；UART/页初始化前通过PSCI/HSM确认其余启用核停放，非零RV hart/动态PLIC S上下文本地四核已验，不是SMP。ARM64 I/D及寄存器已验，DMA/RV物理cache/板端未验。GICv2/PLIC、Device、IRQ_BIND/ACK/解绑/EVENT已接；bind要求CONTROL IRQ+IRQ_SOURCE BIND，SOURCE删除不级联绑定。初始SYSTEM QUERY与所选标准UART的实际MMIO/SOURCE已接，其他SYSTEM/全设备生产发权仍待接。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。Mem内容状态保留到最终回收，managers=0表示退役；现接SEALED规范backing多代视图，Mem96字节，现有临时DATA视图自持的原页接管账本。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；现接可信独占页MMIO视图、固定Device属性与Acct映射租约；现接可信完整成员整页组与规范化租约；现接全局零阶容量及COW额度双重承诺、兑现/取消/旧页转交及原出资者替换基础（Acct64字节）；现接可信逻辑页组/预付分支、DATA Mem克隆及未映射写分离，现接PTE COW位、全部别名分离提交及真实用户DATA写fault原PC重试；现接内核代写/copy及COW栈信号帧；现接可信整个Space映射快照/去重及跨Mem失败回滚；现接独立Local继承/原Handle与墓碑/私有视图重绑定；现接独立fork信号动作/活动帧cookie继承及HAL子返回现场派生，现由可信forksnapshot统一Space/Local/未映射Mem/保底/Signal/现场的同步提交与后段回滚；现接可信Proc/Prep构造、取消/期限及父退出即时撤销；原生fork准入/分批检查点/专用发布与生产发权/DMA仍未接。
- Local48、Life128、Proc512、Prep256、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权；生产PID1已安装16 Set对应的CREATE SERVICE权限，实际服务发布仍待接。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure的0..8镜像接收保底与CALL资源预留已接；类型化提交核心可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表。原生REPLY_RESOURCE描述符解码仍NOTSUP，现由明确可信桥验证核心，不称用户服务已可直接交付。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置支持DEFAULT/IGNORE、CHLD NOCLDWAIT及mask/pending；创建固定IGNORE/mask基线。现接可信类型化安装/排队、两层用户帧与原生sigreturn，校验映射/顶层/cookie与HAL权限，恢复完整GPR/FP/mask；现接原生HANDLER两入口检查、sigsuspend独占等待/M-W-H恢复及libc集合/mask/pending/suspend。来源编码、真实生产、STOP/CONT、普通阻塞中断/重启及POSIX sigaction前置未接。
- 独立libsys/virtio基础库、csu/main、可写argv/environ、基础libc/身份/组会话/wait/堆、C locale数值与格式化及用户态ELF装载已接；串口SPSC辅助层、用户态MMIO架构适配和NS16550/PL011有界PIO后端已接，非实际serial服务。normal的PID1现用真实外部包，普通用户ELF装载仍以测试只读源验证，非FS稳定装载会话；fd/Fix适配、完整spawn/fork/exec未接，virtio尚无生产DMA。
- T1.1..T1.3已接：normal从chosen外部包按init_index装载实际sbin/init，校验目录并进入孤儿收取/CHILD循环；缺包/坏包分配前拒启。T1.4前置已有16 Set/CREATE SERVICE、128槽、可减权转授的自身CALL及SYSTEM QUERY/标准UART实际发权；Boot格式2/352字节含SELF_SPACE/ALLOC/CREATE/SYSTEM。实际服务装载/READY/监督、其他设备和管理域仍待接，不是完整服务管理或可用sh。

## 最新本地验证

- 最新完整make test-all通过715项host、fmt-check；12份源码短暂变更结果排除并逐项重验后，167份成功两架构QEMU结果的源码/头与SDK哈希均匹配。实际孩子七参数解析/权限预检、7字节双向SHARED环、释放IRQ后拒绝及持续READY父下孩子运行资源回收通过，完整PTE/Acct/物理owner审计保持。旧idle-only回收停滞及伙伴页owner残留反例保留，证据`build/host/stage106-serial-start/evidence.json`、test-all.log及rechecks.log。生产init无fixture；无实际serial READY、外部RX流、板端/CI/性能验收。
- native审计逐页核对完整用户/内核映射、ELF代码/RO字节、栈/guard、Boot/SELF资源、隔离包视图和精确Acct owner；用户initstate显示正确目录/字节数/checksum并阻塞，私有页/页表/内核栈收费与实际owner集合一致，包仍保留且不重复计费。早期私有store符号被优化拆分及RV idle sie假设已直接观测后修正，失败记录保留。完整fork仍待T4前补源别名/长循环/IRQ检查点与STOP/CONT。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- 2026-11-18用户集中确认五项：控制台交接0x74/CONSOLE0x20；UART库存data[6..7]布局及serial七参数；资源reply描述符a1/x1；Siginfo来源1/2/3；DMA代次域/固定解除方向。UART库存/七参数解析已同步实现并本地验证；其余实现仍待接，DMA具体权限/调用ABI仍须冻结。
- issues已复核新增009/010及003优先级调整：先完成当前真实启动暴露的退休清理/页元数据归属缺口，再处理010的SDK导出一致性与003无主机观测试点；007按已确认DMA方向推进，009排图形机制前，001/005/006等待实板，不冒充完成上板。

- 001早期UART、004完整8 GiB、005板描述/设备发现、006单主核/停放与007 ARM64开启前置均已有本地交付，真实板及007非一致DMA/RV后端仍待验。接续生产SYSTEM/设备库存与真实serial装载/READY，再按T1..T4推进sh；不能把本地前置当作M3或全项目完成。
- 005解析切片已通过本地全量：1024节点/8192属性/32深度、4 reg/16 IRQ，设备仍64项；完整固定来源树在tests/fixtures/dtb。VF2原始树、NEO3显式测试适配树read/console通过，NEO3原始树拒绝，不放宽零长度检查。原始/适配/固件dump区别见01末尾。005仍需存储/网络设备发现及板端验证；006仍需启动hart/CPU选择与停放，不能把多hart拓扑解析当成SMP或上板已实现。
- 005后续已补MSHC/DWMAC家族/具体别名与多IRQ库存，完整板树实际SD/MMC/GMAC窗口及IRQ已核对且完整回归通过；不写设备寄存器或授DMA/生产Handle。家族不代表寄存器版本/PHY/时钟/复位已实现，实际blk/net驱动仍待完成。设备发现本地验收与板端运行严格分开，详见01/005末尾。
- 007的ARM64开启前置已通过本地全量：入口M/C关闭、独占启动RAM/表页及最后镜像/栈同步，随后M/C/I开启；保留代码/页表同步，主机命名管道GDB独立读SCTLR/TCR/MAIR。漏I-cache反例仅证明控制状态审计，不是漏DMA维护的旧数据反例；007仍留非一致DMA/RV后端/板端待验。接续006的启动CPU/控制器选择及停放，避免在等待生产UART/DMA协议确认时停止全项目进展。
- 006完整本地验收通过：固件单冷入口、当前RUNNING/其余启用CPU STOPPED，异常清空快照并拒启；正常不唤醒核，CASE=cpus实际唤醒到无栈入口后自停放。四核8次与全量均通过；Hirq保持64字节，按CPUINTC选S上下文，主机每条HMP显式cpu-index。SMC/ARM非零affinity仅模型边界、板端/EL2降级未验，不称SMP；详细证据/失败记录见01/006末尾。
- T1.4初始SYSTEM QUERY/UART库存已本地验收：Boot352字节/四项含tag7，精确初始Image绑定、真实MMIO RW/GRANT与IRQ_SOURCE BIND，查询不重装授权；失效STALE。现含data[6]=shift/data[7]=width及对齐验证，结构大小不变。接续从真实包装载serial、完成控制READY和监督；首服务未启动，Set/domain CONTROL及其他SYSTEM业务仍待接。
- 生产UART/早期诊断交接已确认：debughandoff 0x74、初始SYSTEM CONSOLE=0x20，普通输出进有界日志，panic停止调度后独占恢复固件配置并输出停机；实现待接，当前权限仍仅QUERY，详见07/14。
- serial启动约定已确认并接严格解析/真实RESOURCE预检：私有tag 0x100..0x104，七参数含shift/width；失败不提交输出。独立fixture实际装载孩子并验证跨Set授权和SHARED往返，尚非生产serial主循环/READY。RESOURCE不暴露底层Mem身份，真实MMIO/独立SHARED来源仍由可信加载器保证，详见07。
- init控制协议版本编码已于2026-11-18确认：word0高32位版本1、低32位opcode，其他三字不变；共享proto/init.h及SDK导出已接，两架构目标编译检查通过。实际serial/READY监督仍待接，详见07。
- 串口环proto/serial.h与libsys私有serialinit/read/write已通过本地全量：16 KiB固定布局、32字节批次、acquire/release、背压与失败不提交；不自动notify/yield/reset。共享映射/IRQ循环、装载/READY和端点恢复仍待接，详见11末尾。
- UART PIO后端已本地验收：保留固件波特率/字符格式，32次有界尝试、FIFO背压、错误统计及LSR读清暂存、PL011 BUSY稍后重试；ISA IO在libsys，驱动在sbin/serial，不依赖内核HAL。实际共享映射/IRQ服务循环、外部字节流、装载/READY、交接和端点恢复仍待接，详见09执行与验收。
- 下一步T1.4：从真实目录装载首个实际服务，补真实服务清单/预算、CREATE SERVICE/固定CONTROL、设备/IRQ授权和READY握手；不能用空进程或测试hello充当serial/sh，不能把T1.3当全部T1完成。normal已要求包，make smoke自动构建默认生产包；原生fork/exec仍未开放。规格见[07](design/07-boot.md)及各主题。
- 资源reply已确认a1/x1为descriptor_ptr，其他未用参数0、a5为REPLY_RESOURCE，reply_recv不交付；实现待接，详见06。
- Siginfo来源code USER=1/CHILD=2/FAULT=3及各来源字段已确认，实现待接，见11。
- IRQ_SOURCE独立撤销及原生bind已接，SOURCE不转授/继承；初始标准UART的BOOT_RESOURCE已有实际MMIO/IRQ_SOURCE Handle，SYSTEM QUERY已接，其他设备/布局和管理业务仍后续，见05/07末尾。
- DMA设备实例代次域及固定解除方向已确认，具体权限/调用ABI仍须冻结，见09；缓存反例主机模型不代表DMA/HAL已实现。
- 继续完成真实信号来源/完整递送与阻塞中断、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
