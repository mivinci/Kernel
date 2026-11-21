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

- stage246：双架构 libsys 实测子进程解除继承的连接映射，解除后子读失败、父映射和结果收尾保持；格式通过。生产多连接协调仍待接。

- stage245：子侧信号入口与普通返回共用 `sysforkrepair`，成功后记下精确身份和 Fix 地址，重复进入不再转换。双架构 sigframe、10 项主机回归和格式通过。

- stage244：父侧 `sysforkfinish` 串起上下文准入、Fix 组装、发布和失败撤销。18 项主机回归、格式及双架构 sigframe/libsys 通过；后端继承与公开 `fork` 仍待接。

- stage243：父侧 Fix 装载、提交和失败撤销已串联，未完成清理可重试。8 项主机测试、格式与双架构 sigframe/libsys 通过；后端准备与公开入口仍待接。

- stage242：父侧 `sysfixpack` 按现有格式验证并组装 Fix，出错不改目标缓冲。28 项主机测试、格式和双架构 sigframe/libsys 通过；生产事务和后端准备仍待接。

- stage241：父侧 `sysctxfixplan` 从稳定 Rbank 导出三层记录并复用子侧路由准入。65 项上下文主机测试、格式、双架构 sigframe/libsys 通过；完整父侧协调器与后端准备仍待接。

- stage240：公开 `qsort`、`bsearch` 已纳入 libc SDK；双架构 runtime QEMU 实际验证排序、重复键、记录完整性和查找边界，格式通过。

- stage239：libc公开getuid/geteuid/getgid/getegid及32位uid_t/gid_t已接SDK，每次读取真实自身凭据并保持errno；8项相关主机测试、格式与双架构elf/runtime四次QEMU通过。凭据修改/附加组未接，fork机制错误真实场景、后端子引用和整体入口仍待接。

- stage238：真实子返回中fd/File两类未提交创建预留分别导致apply拒绝且源状态保留，子明确本地撤销后可继续转换，父预留仍独立完整；双架构sigframe、10项组合测试及格式通过。只改测试，非已提交创建续点支持；机制失败真实fork/后端子引用/整体入口仍待接。

- stage237：已排空普通READ/WRITE的PEER/CANCELLED/REVOKED可在子保留原生状态，不重发或夺父收尾责任；75项相关主机测试、格式及双架构libsys/sigframe通过。新机制失败路径仅主机调用，真实场景/整体fork与后端准备仍待接，子reference_id规则仍待确认。

- stage236：组合apply已在双架构真实fork返回窗口及handler/raw/IPC场景执行，子完成标记避免重复转换，父身份/运输和最终资源守恒保持；10项组合主机测试、格式及sigframe/libsys四次QEMU通过。夹具无继承fd/后端子引用，非空File与生产整体协调器仍待接，子reference_id仍待确认。

- stage235：组合准入后可单次本地转换Rctx/File/目录，保持fd和父运输责任；未支持的创建预留在写入前NOTSUP。74项相关主机测试、格式及双架构libsys/sigframe通过，新增apply仍仅主机调用；真实非空File子修复/整体协调器及后端准备未接，子reference_id仍待确认。

- stage234：fd/File/连接/Rctx已接组合只读准入，核对精确目录存储、File代次与Fix后端/token，并拒绝保留目录代次耗尽；77项相关主机测试、格式及双架构libsys/sigframe通过。组合入口仅主机调用，生产协调器与真实后端准备仍待接，子reference_id规则仍待确认。

- stage233：Rctx完整路由只读准入与实际子修复共用核验，便于协调器在改变File/连接前拒绝未支持续点；64项上下文主机测试、格式及双架构libsys/sigframe回归通过。生产整体协调器/后端准备仍待接，子reference_id候选仍未确认。

- stage232：stage231 最终源码完整1228项主机回归通过，双架构默认 init/serial 生产启动包各一次通过；未改运行代码，File/fork 实际场景证据仍为stage231，未重跑全QEMU矩阵。子 reference_id 规则仍等确认，不能把自动接续当作采用候选。

- stage231：File 子转换的完整只读准入与提交共用核验，支持协调器先组合 fd/File/连接检查；30项相关主机测试、格式及双架构 libsys/sigframe 回归通过。新准入仍仅主机调用；FORK_PREPARE 只回 token 后如何取得子 reference_id 的规则待确认（10篇候选），真实后端准备与整体协调器未完成。

- stage230：fd/File 子保留清单的只读一致性准入已接，精确核对全部 LIVE fd 的本地 ID/alias，保留 CLOEXEC 与创建中的预留责任；29项相关主机测试、格式及双架构 libsys/sigframe 回归通过。新检查仍仅主机调用，真实后端准备/重绑及生产协调器待接。

- stage229：File 注册表可依子准备清单保留本地 ID、重绑后端引用并减权 alias/pin，父清理副本不发 CLOSE/ACK；完整1225项主机测试、格式及双架构 libsys/sigframe 回归通过。新原语仍仅主机调用，远端准备/真实重绑与生产协调器待接。

- stage228：双架构真实负业务回复中 handler fork 已验，父子保留 DENIED/零进展、后端无重复 WRITE，原成功 READ/WRITE fork 和资源守恒仍通过；格式与19项相关主机测试通过。机制失败、创建结果/File子引用及生产协调器待接。

- stage227：完成 READ/WRITE 的负业务回复可凭排空证明在子保留原错误；75项相关主机测试、格式和双架构 libsys/sigframe 回归通过。新错误路径仍仅主机调用，真实场景、其他结果/File子引用及生产协调器待接。

- stage226：双架构真实 WRITE 完成后 handler fork 已验：子保留3字节部分进展和无效旧 RPC，父完成原 Op 收尾，后端只处理一次 WRITE；格式及17项相关主机测试通过。其他结果/File 子引用与生产协调器待接。

- stage225：完成 WRITE 可凭预先排空证明在子 Rctx 保留实际字节数、不重发；完整 1218 项主机测试、格式及双架构 libsys/sigframe 回归通过。新 WRITE 路径仍仅主机调用，真实场景、其他结果/File 与生产协调器待接。

- stage224：双架构真实 READ 完成后 handler fork 已验：子保留原快照、旧 RPC 无权且退出43，父继续原 Op 收尾；15 项主机信号上下文测试、格式及 libsys 通过。后端 File 子引用、其他结果修复与生产协调器待接。

- stage223：完整快照的普通 READ 可在子 Rctx 保留业务结果/数据并移除父运输责任；71 项相关主机测试、格式及双架构 libsys/sigframe 回归通过。新完成结果路径仍仅主机调用；真实 READ fork、其他结果/File 修复及生产协调器待接。

- stage222：双架构真实 IPC 等待中 handler fork 已验：子本地中断且旧 RPC 无权，父保留原请求并在子退出后收回复/ACK；格式、18 项 IPC 主机测试及 sigframe 通过。完成结果修复与生产整体 fork 协调器待接。

- stage221：等待 IPC 的子 Rctx 可固定本地中断并移除父运输路由，支持后续 fork 与原始层混合；18 项 IPC 上下文主机测试、格式及双架构 libsys/sigframe 四次回归通过。新路径仍仅主机调用；完成结果、真实 IPC fork 与整体协调器待接。

- stage220：子准备 token 已接按需 ATTACH 的本地准备与既有 Bootstrap 路径；完整 1206 项主机测试、格式和双架构 sigframe 回归通过。真实后端、IPC Rctx/File 修复与整体 fork 协调器待接。

- stage219：子 Connwork 保存准备 token，重复修复拒绝不同 token 且不改工作区；70 项相关主机测试、格式与双架构 sigframe 回归通过。真实后端 ATTACH 与整体 fork 协调器待接。

- stage218：子 Connwork 可在别名解除后重绑，清除父运输记录副本且不 free/ACK；10 项主机测试、格式与双架构 sigframe 回归通过。真实运输与整体协调器待接。

- stage217：子运输别名解除原语已接，失败保留责任、不释放父句柄；9 项工作区主机测试、格式与双架构 sigframe 回归通过。真实共享连接及整体修复待接。

- stage216：目录 fork 代次耗尽与失败原子性回归通过，15 项主机测试。运行代码未变，双架构证据仍为 stage215；运输工作区与整体修复待接。

- stage215：目录重绑已在双架构真实 fork/信号返回中验证，子旧 tag 失效、父目录保持；格式及 sigframe 通过。fixture 无运输资源，非空连接与 Connwork 整体修复待接。

- stage214：子连接目录重绑与旧 tag 失效已接，14 项主机测试、格式及双架构 sigframe 回归通过。新目录路径仅主机调用；Connwork/File/Rctx 整体修复与旧共享别名解除待接。

- stage213：Fix 子 token 与父目录的只读准入已接，13 项主机目录测试、格式及双架构 sigframe 回归通过。目录实际重绑与 IPC 责任隔离仍待接。

- stage212：Fix 子 token 精确后端查询已接，20 项主机测试、格式及双架构 sigframe 回归通过。Connwork/目录重绑及实际子接入待接。

- stage211：Fix 子连接描述及有界解析已接，20 项主机 Boot/Fix 测试、格式及双架构 sigframe 通过；非空连接仍仅解析验证，Connwork 和 IPC 修复待接。

- stage210：外层真实活动 Rctx 中的 handler fork 已在双架构验证：父保留成功结果、子固定 INTERRUPTED，双方完成汇编恢复和收尾。格式及 sigframe 通过；IPC 路由修复待接。

- stage209：进程信号清理改动追加双架构 sigconfig/sigsuspend/jobs 六次 QEMU 和 11 项主机 sigfork 回归，全部通过。外层活动 Rctx 与 IPC 修复待接。

- stage208：双架构验证 handler 内原生 fork、子嵌套信号及父子各自 sigreturn；修复撤销未发布孩子时遗漏活动信号层的清理。格式与双架构 sigframe 通过。外层活动 Rctx 与 IPC 修复待接。

- stage207：完整 make unit 1198 项通过；最近双架构 sigframe 为 stage206，未重跑完整 QEMU 矩阵。继续活动信号层 fork 与 IPC 修复。

- stage206：RV64 普通 Rctx 返回窗口允许合法半字起点，11 项主机测试、格式及双架构 sigframe 回归通过。handler 内 fork 的真实恢复与 IPC 修复仍待接。

- stage205：两架构主机解码验证子固定中断结果不被旧父寄存器覆盖，10 项测试通过；运行代码未变，真实活动层 fork 汇编恢复仍待接。

- stage204：无路由 WAITING 层在子固定 INTERRUPTED 并清空父参数/返回元组，10 项主机测试、格式及双架构 sigframe 回归通过。真实活动层 fork 接续与 IPC 修复待接。

- stage203：新增原始上下文修复的失败原子性、快照/完成结果保留及重复应用主机回归，9 项通过。运行代码未变，最近双架构验收为 stage202。

- stage202：子上下文保留已完成的无路由纯值查询，双架构 sigframe 核对父稳定结果与子新执行结果。8 项主机测试、格式及双架构 QEMU 通过；其他完成状态及 IPC 修复待接。

- stage201：无路由 PREPARED 层可在子进程复用原 key，信号入口和正常返回重复应用已在双架构 QEMU sigframe 验证。8 项主机上下文测试与格式通过；完成、中断及 IPC 路由修复待接。

- stage200：Fix 动作与继承 Rbank 的来源、序号和阶段逐层核对，8 项主机上下文测试、格式检查及双架构 QEMU sigframe 通过。责任迁移及上下文应用待接。

- stage199：Fix 三层动作头已实现和验证，19 项主机 Boot/Fix 测试、格式检查及双架构 QEMU sigframe 通过。上下文应用及子连接记录待接。

- stage198：信号入口与正常子返回共用 Fix 身份校验，双架构 QEMU sigframe 与格式检查通过；上下文应用待接。

- stage197：双架构 QEMU sigframe 已验证子首次返回窗口的真实 USR1、Fix 元组捕获和 sigreturn；RV64 相对 PC 对齐回归已覆盖。生产上下文修复待接。

- stage196：ARM64/RV64 已能从保存的陷阱现场解码 FORKPREP 子返回。主机上下文测试 7 项、格式检查和双架构 QEMU sigframe 通过；窗口内信号注入及修复待接。

- stage195：ARM64/RV64 专用 FORKPREP 汇编桩已标记返回寄存器保留窗口。格式检查与双架构 QEMU sigframe 通过；信号入口消费与上下文修复待接。

- stage194：libsys 安装子只读 Fix 并清理父临时映射。7 项主机测试、格式检查和双架构 QEMU sigframe 通过。上下文修复仍待接。

- stage193：libsys 私有 forkprep/forkcommit/forkabort 桩已接，双架构 QEMU sigframe 覆盖父子返回及撤销，6 项主机回归和格式检查通过。公开 fork 和上下文修复待接。

- stage192：运行库验证 Fix 头、精确身份和资源表。18 项主机测试、格式检查及双架构 QEMU sigframe 的真实子返回解析通过。三层上下文记录编码和修复仍待接。

- stage191：原生 forkcommit 校验 Fix 权限与本次构造归属，双架构 QEMU sigframe 验证子真实返回及父 wait，格式检查通过。运行库 Fix 解析、修复和公开 fork 仍待接。

- stage190：原生 forkprep/forkabort 已接，按当前 CREATE 和父实际规格准入。双架构 QEMU sigframe 验证真实陷阱、未发布孩子撤销和账目回收。Fix 校验、forkcommit 与运行库接续仍待实现。

- stage189：未映射 DATA 别名的 PTE 核验和写保护每批最多 32 页，超期回滚已覆盖。76 项主机专项、格式检查及双架构 QEMU sigframe 通过。别名登记与根冻结仍在一次临界区。

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
