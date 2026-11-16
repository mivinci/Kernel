# 当前状态

更新：2026-11-17（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP陷阱现场、单核调度和约5ms用户timer已接。内核IRQ只请求安全点调度，无嵌套IRQ；ARM缓存仍关闭；现接GICv2/PLIC外设控制器HAL及最小Device映射，现接固定镜像IRQ_BIND/ACK/解绑与真实EVENT递送；IRQ_SOURCE授权/原生bind仍待确认。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；现接可信独占页MMIO视图、固定Device属性与Acct映射租约；共享页设备组、生产硬件发权及COW/DMA未接。
- Local48、Life128、Proc512、Prep184、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权，SERVICE仅由可信fixture安装，生产清单策略未启动。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure的0..8镜像接收保底与CALL资源预留已接；类型化提交核心可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表。原生REPLY_RESOURCE描述符解码仍NOTSUP，现由明确可信桥验证核心，不称用户服务已可直接交付。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置支持DEFAULT/IGNORE、CHLD NOCLDWAIT及mask/pending；创建固定IGNORE/mask基线。现接可信类型化安装/排队、两层用户帧与原生sigreturn，校验映射/顶层/cookie与HAL权限，恢复完整GPR/FP/mask；现接原生sigsuspend的独占等待/M-W-H恢复，原生HANDLER仍NOTSUP。真实信号来源、STOP/CONT、普通阻塞中断/重启及libc前置未接。
- 独立libsys SDK、csu/main、可写argv/environ、基础libc/errno/退出/身份/组会话/wait、C locale整数转换与内存snprintf/vsnprintf、用户态ELF装载已接，库依赖审计无隐藏libc反向依赖。来源仍是只读嵌入ELF，非FS稳定装载会话；fd/Fix启动适配、完整POSIX spawn/fork/exec未接。
- 一次性PID1及专用预校验描述装载器已接；真实用户测试覆盖三代孤儿收取、组wait BLOCKED/READY强杀与PID1退出致命。载荷仍在tests/kernel，生产sbin/init、启动包/SYSTEM/设备发权未接；normal仍初始化后停止。

## 最新本地验证

- 本地完整make test-all通过334项host、格式和102个两架构QEMU，日志`build/host/stage52-mmio/`；SDK依赖审计通过，未执行CI/目标真实硬件/性能验收。
- irqbind载荷现实际map独占UART页、删除管理Cap后继续使用Device映射，由用户清中断并读回再ACK；普通copy/seal/EXEC被拒绝。同Set忙伙伴map实际DENIED；最后unmap/退出回收租约及库存引用，原IRQ/进程/IPC/运行库全量仍通过。共享页virtio、生产硬件发权和DMA未由此验收；范围与初次SDK/链接失败证据见09。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- 启动包容器：64字节头/64字节目录项、最多32项、包内偏移及FNV-1a64候选待用户确认，详见[07开放问题](design/07-boot.md#开放问题启动包容器候选待用户确认)。已有bootimageload不依赖这个新容器。
- 资源reply：候选`reply(request,descriptor_ptr,0,0,0,REPLY_RESOURCE)`，a1/x1指针位置待确认，详见[06](design/06-abi.md)；不提前实现候选解码。
- Siginfo来源code USER=1/CHILD=2/FAULT=3及各来源字段候选待用户确认，见[11开放问题](design/11-terminal.md#开放问题siginfo来源编码待用户确认)，尚未采用候选数值。
- IRQ_SOURCE撤销是否影响既有独立IRQ_BIND仍待用户确认，候选和实现边界见05末尾。
- 继续完成真实信号来源/完整递送与阻塞中断、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
