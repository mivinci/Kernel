# 当前状态

更新：2026-11-18（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 用户新增设计commit `ddaed1d`：优先M3可用sh，T1启动包/init→T2 fd/Fix及serial/tty→T3 blk/fs→T4完整sh；fork/exec、pipe/dup2与作业控制是关键前置。2026-11-18明确GUI仍属最终目标：net/图形GPU机制等排M3后，系统可通过sh正常使用且其他能力及图形/GPU机制就绪后，最后推进GUI。13候选协议仍未冻结，不提前实现。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP陷阱现场、单核调度和约5ms用户timer已接。内核IRQ只请求安全点调度，无嵌套IRQ；ARM缓存仍关闭；现接GICv2/PLIC外设控制器HAL及Device映射、固定镜像IRQ_BIND/ACK/解绑与真实EVENT。原生bind现核对CONTROL IRQ及本地IRQ_SOURCE BIND；SOURCE删除不级联已有绑定，生产SYSTEM发权仍待接。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。Mem内容状态保留到最终回收，managers=0表示退役；现接SEALED规范backing多代视图，Mem96字节，现有临时DATA视图自持的原页接管账本。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；现接可信独占页MMIO视图、固定Device属性与Acct映射租约；现接可信完整成员整页组与规范化租约；现接全局零阶容量及COW额度双重承诺、兑现/取消/旧页转交及原出资者替换基础（Acct64字节）；现接可信逻辑页组/预付分支、DATA Mem克隆及未映射写分离，现接PTE COW位、全部别名分离提交及真实用户DATA写fault原PC重试；现接内核代写/copy及COW栈信号帧；现接可信整个Space映射快照/去重及跨Mem失败回滚；现接独立Local继承/原Handle与墓碑/私有视图重绑定；现接独立fork信号动作/活动帧cookie继承及HAL子返回现场派生，现由可信forksnapshot统一Space/Local/未映射Mem/保底/Signal/现场的同步提交与后段回滚；现接可信Proc/Prep构造、取消/期限及父退出即时撤销；原生fork准入/分批检查点/专用发布与生产发权/DMA仍未接。
- Local48、Life128、Proc512、Prep256、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权；生产PID1已安装16 Set对应的CREATE SERVICE权限，实际服务发布仍待接。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure的0..8镜像接收保底与CALL资源预留已接；类型化提交核心可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表。原生REPLY_RESOURCE描述符解码仍NOTSUP，现由明确可信桥验证核心，不称用户服务已可直接交付。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置支持DEFAULT/IGNORE、CHLD NOCLDWAIT及mask/pending；创建固定IGNORE/mask基线。现接可信类型化安装/排队、两层用户帧与原生sigreturn，校验映射/顶层/cookie与HAL权限，恢复完整GPR/FP/mask；现接原生HANDLER两入口检查、sigsuspend独占等待/M-W-H恢复及libc集合/mask/pending/suspend。来源编码、真实生产、STOP/CONT、普通阻塞中断/重启及POSIX sigaction前置未接。
- 独立libsys/virtio基础库、csu/main、可写argv/environ、基础libc/身份/组会话/wait/堆、C locale数值与格式化及用户态ELF装载已接。normal的PID1现用真实外部包，普通用户ELF装载仍以测试只读源验证，非FS稳定装载会话；fd/Fix适配、完整spawn/fork/exec未接，virtio尚无生产DMA。
- T1.1..T1.3已接：normal从chosen外部包按init_index装载实际sbin/init，校验目录并进入孤儿收取/CHILD循环；缺包/坏包分配前拒启。T1.4前置已建立16个真实Set及对应CREATE SERVICE，init表128槽，Boot格式2/320字节含SELF_SPACE/ALLOC/服务CREATE，自身CALL可减权转授。SYSTEM/设备发权、实际服务装载、READY及失败监督仍待T1.4/T1.5。不是完整服务管理或可用sh。

## 最新本地验证

- 最新完整make test-all通过641项host、fmt-check及144次两架构QEMU。IRQ专项由真实用户代码bind并删SOURCE后继续收UART中断，覆盖弱CONTROL/裸IRQ/冲突/非法位/保留输入及高位拒绝。另有10次生产init事件循环、4次包内exit42、6次分配前拒启及RAM128/256/512。全部结果核对当前源码/共享头哈希；normal和实际init链接无测试载荷，默认包只含init。证据`build/host/stage94-native-irq-source/evidence.json`与test-all.log；无CI/真机/性能验收。
- native审计逐页核对完整用户/内核映射、ELF代码/RO字节、栈/guard、Boot/SELF资源、隔离包视图和精确Acct owner；用户initstate显示正确目录/字节数/checksum并阻塞，私有页/页表/内核栈收费与实际owner集合一致，包仍保留且不重复计费。早期私有store符号被优化拆分及RV idle sie假设已直接观测后修正，失败记录保留。完整fork仍待T4前补源别名/长循环/IRQ检查点与STOP/CONT。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- 下一步T1.4：从真实目录装载首个实际服务，补真实服务清单/预算、CREATE SERVICE/固定CONTROL、设备/IRQ授权和READY握手；不能用空进程或测试hello充当serial/sh，不能把T1.3当全部T1完成。normal已要求包，make smoke自动构建默认生产包；原生fork/exec仍未开放。规格见[07](design/07-boot.md)及各主题。
- 资源reply：候选`reply(request,descriptor_ptr,0,0,0,REPLY_RESOURCE)`，a1/x1指针位置待确认，详见[06](design/06-abi.md)；不提前实现候选解码。
- Siginfo来源code USER=1/CHILD=2/FAULT=3及各来源字段候选待用户确认，见[11开放问题](design/11-terminal.md#开放问题siginfo来源编码待用户确认)，尚未采用候选数值。
- IRQ_SOURCE独立撤销及原生bind已接，SOURCE只由可信库存安装、不转授/继承；UART BOOT_RESOURCE携真实MMIO/IRQ_SOURCE Handle已写定，生产SYSTEM发权仍待接，见05/07末尾。
- DMA设备实例域及固定解除入口待确认，见09末尾；新增缓存反例主机模型不代表DMA/HAL已实现。
- 继续完成真实信号来源/完整递送与阻塞中断、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
