# 当前状态

更新：2026-11-17（Asia/Shanghai）。设计入口：[总览](design/00-overview.md)；协作规则：[AGENTS.md](../AGENTS.md)。

## 目标与分支

- PR #5已合入main（`5b5edd3`）；长期分支`codex/kernel-development`。用户授权持续推进，每个完成且验收通过的点提交并推送到同一分支，实质取舍再问。
- 完整目标包括net，不能以串口MVP、局部内核或单个服务验收代替全项目完成。旧ABI不兼容，保留用户改动。
- 设计/执行项/历史证据在对应主题末尾，本页只维护当前交接；不另建待办文档。

## 已实现边界

- RV64/ARM64启动、DTB、伙伴页、内核高半区/W^X/guard、完整整数与FP陷阱现场、单核调度和约5ms用户timer已接。内核IRQ只请求安全点调度，无嵌套IRQ；ARM缓存仍关闭。
- Set/Acct、Space/Mem/Mmap、DATA/CODE/SHARED、封存及代码同步已接。SHARED只向原Acct计费，跨Set减权窗口/撤权后映射保留/出资者退出滞留/最后unmap结清已验。64KiB受检查copy检查权限、普通内存和物理字节重叠，当前全程IRQ屏蔽/VM_BUSY；无COW/DMA。
- Local48、Life128、Proc512、Prep184、Image64、Obs48、Rq192、Space64、Set56；结果/镜像/账目与运行槽独立回收。两层Sigctx预付RV704/ARM960，cookie跨PID/image复用不重置，不回绕。
- 原生create/configure/start/destroy、孩子出资装载及发布别名屏障、固定CONTROL管理、凭据、组/会话、父wait与孤儿接管、query PROC/RQ/ACCT/LIFE/RESOURCE已接。普通孩子CREATE上限减权，SERVICE仅由可信fixture安装，生产清单策略未启动。
- 消息IPC、8/1/1/2出站预付、15+1接收、取消/ACK/独立收尾、事件/闹钟已接。configure的0..8镜像接收保底与CALL资源预留已接；类型化提交核心可原子交付SHARED MEM/CALL USE，ACK不回收资源、迟到回复不改新表。原生REPLY_RESOURCE描述符解码仍NOTSUP，现由明确可信桥验证核心，不称用户服务已可直接交付。
- 固定Image观察及LIFE、CHILD提示已接；每服务128/总1024观察预付，真实REQUEST或显式CONTROL QUERY限定范围；观察、父收取与实际DMA/业务清理各自独立。
- 原生信号配置当前仅DEFAULT/IGNORE、CHLD NOCLDWAIT及mask/pending查询。创建固定IGNORE/mask基线，configure不累计重置；退出保存父策略决定，自动收取只影响后续退出。HANDLER、其他已知flags、信号生产/发送/递送、STOP/CONT、sigsuspend和sigreturn未接。
- 新增HAL信号入口六参数转换与寄存器恢复校验：限制用户地址/对齐，特权/IRQ/诊断值绑定可信快照，允许GPR/FP及ARM NZCV的合法编辑，保留合法原FP环境；拒绝不写输出。实际用户帧映射、最上层/cookie/PID/image校验仍属后续信号核心，HAL不是完整sigreturn。
- 独立libsys SDK、csu/main、可写argv/environ、基础libc/errno/退出/身份/wait包装与用户态ELF装载已接，库依赖审计无隐藏libc反向依赖。来源仍是只读嵌入ELF，非FS稳定装载会话；fd/Fix启动适配、完整POSIX spawn/fork/exec未接。
- 一次性PID1及专用预校验描述装载器已接；真实用户测试覆盖三代孤儿收取、组wait BLOCKED/READY强杀与PID1退出致命。载荷仍在tests/kernel，生产sbin/init、启动包/SYSTEM/设备发权未接；normal仍初始化后停止。

## 最新本地验证

- 完整`make test-all`通过279项host、格式及94个两架构QEMU场景，日志`build/host/stage44-signal-context/test-all.log`；最终Hctx/Sigframe大小/FP偏移断言及专项证据见同目录。未执行CI/真实硬件/性能验收。
- 新增5项host验证入口六参数、拒绝原子性、特权/IRQ/诊断/地址篡改、完整缓冲别名与FP控制规则。sigreserve两架构各两次真正进入U/EL0检查恢复后的GPR/FP、ARM条件码、合法非默认控制和RV原样frm7；每次拒绝8种非法恢复，原cookie/预付/离栈清理仍结清。
- C/汇编测试对象重名和exit未用输入不清零的失败日志保留，已修正载荷/命名，未放宽内核校验。此前资源交付/接收保底/共享页/信号配置与自动收取的详细验收留在各篇末尾及stage40..43日志。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；保留两架构及共享compile_commands.json、`.su`/stack-usage.json。物理ELF高别名仍需`-fno-jump-tables`；编译帧不是完整栈深或IRQ延迟证明。

## 待确认与接续

- 启动包容器：64字节头/64字节目录项、最多32项、包内偏移及FNV-1a64候选待用户确认，详见[07开放问题](design/07-boot.md#开放问题启动包容器候选待用户确认)。已有bootimageload不依赖这个新容器。
- 资源reply：候选`reply(request,descriptor_ptr,0,0,0,REPLY_RESOURCE)`，a1/x1指针位置待确认，详见[06](design/06-abi.md)；不提前实现候选解码。
- 继续完成信号帧/安全恢复与完整递送、原生资源交付、生产init/启动包/清单与SYSTEM、IRQ/设备/DMA、fork/exec/COW、文件/终端及用户态服务（包括net）。详细依赖和验收以01..15为准，不缩减总目标。
