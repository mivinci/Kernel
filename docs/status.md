# 当前状态

更新：2026-11-16（Asia/Shanghai）。设计入口为[总览](design/00-overview.md)，协作规则见根目录AGENTS.md。

## 分支与工作方式

- PR #5已合入main（`5b5edd3`）；长期分支为`codex/kernel-development`。用户授权持续按设计推进，每个完成且验收通过的点提交并推送到同一分支；遇实质取舍再问。
- 现有设计决策及执行验收写在对应主题末尾；本页只保留当前交接。旧ABI不兼容，保留用户改动。

## 当前实现

- 两架构启动、DTB、伙伴、高半区/guard栈、完整用户与内核IRQ整数/FP现场、Set/Acct、Space/Mem/Mmap、CODE封存与同步已接。仅单核，ARM缓存关闭；Device属性及内核W^X/低别名撤销有QEMU审计。
- Local48、Life128、Proc512、Prep184、Image64、Obs48、Rq184、Space64、Set56；计费栈与CPU独立续点支持离栈回收。Life结果、运行槽、镜像与账目独立回收/复用，不以旧索引选新身份。
- 原生自身内存/时间/exit/yield/getid，消息call/send/recv/reply/reply_recv/defer及请求查询/取消/ACK/finish/check已接；8/1/1/2出站预付、普通960/恢复64请求池、15+1接收和可下调pending已接。资源reply、原生rqclean及SHARED/DMA仍待接。
- 固定CONTROL的connect/grant/revoke/inherit、BUDGET/TERMINATE、不可变凭据/getcred已接。CALL_CONTROL=0x08只选自身控制槽；可转授入口要求CONTROL CONNECT+GRANT，CONSTRUCT只生成USE。
- query已接PROC/RQ/ACCT/LIFE/RESOURCE：自身0/固定CONTROL QUERY，以及CONSTRUCT CONFIG仅RESOURCE；最多16条192字节记录，整区预检、代次游标、隐藏内部指针/消息。实际组/费用/映射/请求责任和选中窗口CPU纳秒已接；SYSTEM/BOOT_RESOURCE枚举未接。
- SESSION/GROUP共享128份48字节记录，getpg/setpg/setsid按自/未exec直接孩子、同会话及0选择器校验，uid0无豁免。组长退出/运行槽复用不改变旧PGID/SID；停止/终端与孤儿组规则未接。
- 每运行槽预付两层Sigctx（RV704/ARM960字节）及动作表，绑定完整Image并接入构造回滚/CPU phase2回收。Sighead64的cookie@48及单调分配已接：跨槽唯一、跨PID/image复用不重置、耗尽不回绕。实际帧写入/核验、信号递送、STOP/CONT、sigreturn仍未接。
- 原生create/configure/start/destroy及构造期alloc/map/unmap/seal、CONFIG grant/revoke、USE connect已接。普通创建核对原Set/四凭据；CREDENTIAL可显式选身份，SERVICE另返固定CONTROL。孩子CREATE四上限夹到Createarg且清除CREDENTIAL/SERVICE/GRANT。返回槽、全部预付与准备失败完整回滚；发布前必须解除父临时别名，发布立即撤权。
- destroy按固定批次让出CPU，WAIT_DESTROY登记独立Prep责任；调用者被terminate时不泄漏被丢弃C栈的引用，旧PID清理不误选复用槽。目标本地拆除完成后仍存在的父Mmap继续按原Mem/Acct计费。CREATE通用再转授、EXEC_VIEW、非零资源回复保底及初始信号配置未接。
- 原生observe/read/next/ack/drop与LIFE提示已接：每观察者镜像预付128、系统1024，SOURCE/CHILD凭真实REQUEST、MONITOR另需固定CONTROL QUERY；旧cookie跨撤权、父收取和槽复用继续绑定原Image，退出逐批回收。FUTURE真实exec准备及生产清单发权仍未接。
- 原生wait已接退出结果、WNOHANG及完整PID/任意孩子/冻结组选择；无匹配孩子返回ERR_CHILD=22。CHILD合并提示与父死亡后的即时逻辑孤儿接管已接，物理关系分批改写；PROC查询显示等待种类/公开选择器。STOP/CONT与捕获信号等待中断仍未接。
- notify/alarm与RQ_DONE/RQ_CANCEL已接；用户timer约5ms，内核IRQ只记事件/调度请求，在安全检查点切换。CPU空闲可独立到期/回收；无IRQ嵌套或通用长Op。
- 独立libsys/Boot校验与两架构C _start静态ELF已接；make sdk导出公共/私有头、库和user.ld，完整库对象审计无外部运行时符号。构建工具将ELF转换为既定装载描述，内核只核验描述；CASE=libsys由可信fixture加载实际ELF并验证IPC/内存/退出。csu/main与基础libc接续见下，普通用户ELF加载器接续见下。
- csu/main、可写argv/environ、getenv、exit/_exit/32槽atexit、errno/strerror及基础内存/字符串已接；sysstart归libsys绑定Boot，C环境归libc，无malloc/fs启动依赖。SDK另导出crt0.o/libc.a/runtimeuser.elf；fd/Fix适配未接，相关非零启动内容明确拒绝。完整libc/stdio/文件/信号适配仍待接。
- libsys用户态ELF64校验/逐页装载/临时清理与创建配置桩已接；成功规划先限页数，边界页合并权限且拒绝W+X，真实SELF通过准备RESOURCE查询补齐Boot。父每次只占一个临时MEM槽，孩子只留映射；清理失败保留Elfwork责任。来源当前是只读嵌入ELF，FS稳定来源及完整POSIX spawn/exec尚未接。
- 一次性可信procboot已接PID1/父0/槽0并复用完整预付；普通创建不准入槽0，PID1退出仍致命。init测试ELF已在两架构真实U/EL0创建三代并收取孤儿，initfatal验证真实用户exit停机；它们不是生产sbin/init，清单/启动包/SYSTEM仍待接。
- normal仍初始化后停机，无模拟init。旧spawn场景仍用位置无关测试代码；新elf场景由用户libsys装载实际csu/main ELF，不是完整posix_spawn。源CREATE/SERVICE由可信fixture安装，生产清单的SERVICE仅init发放策略尚未启动；孩子退出结果已由真实用户父原生wait收取；真实用户PID1收取孤儿已在独立init测试ELF验证，生产清单仍待接。

## 本地验证与边界

- 完整make test-all通过243项host、格式及92个QEMU场景，日志`build/host/stage35-pid1/test-all.log`；最终两架构init/initfatal及完整host/格式复验见同目录`*-final.log`。
- init两架构由真实PID1/csu/main创建三代，先拒绝越权收取，随后分别收取旧父33、已结束孤儿11和原活孩子22；最终保留1个init实例且费用/引用精确回到其启动基线。initfatal确认真实用户PID1 exit唯一致命停机；QMP审计保留的4页RW/NX栈、表及guard，host反例验证审计不放行错误映射。
- elf两架构实测用户C装载真实csu/main ELF，2个成功孩子通过wait退出42，4种装载失败均destroy且无wait结果；孩子仅3资源槽、父仅1临时槽可完成装载，VA冲突不误拆原映射，页/Set/引用最终结清。6项host覆盖解析变异对照、边界页内容/BSS、每步失败、清理责任保留与非法工作区拒绝。
- runtime两架构各10份镜像实测5次main、38次atexit回调、8次fini，正常/直接退出、可写参数/环境及精确16KiB边界；5种畸形/未支持Boot在构造与main前拒绝，全部页/Set/引用结清。8项新增host覆盖环境、退出与基础库；独立libsys入口仍通过。
- 新增11项host覆盖Boot/ELF/预校验描述范围、对齐、重叠、保留位和预算，以及IPC错误携带RPC；libsys两架构实测独立C ELF、BSS/数据、合法Boot、内存清零/释放、四字同步/异步CALL/SEND、请求确认与完整回收。
- 新增8项host覆盖128槽耗尽/完整枚举、8份总预付、GRANT失败回滚/撤权、cookie跨预付区复用及耗尽、父结果独立回收和取消exec不冒充当前镜像。creds两架构实测SOURCE/CHILD/MONITOR授权、LIFE合并、DRAINING/证明释放、原生OBSERVE grant和观察者死亡回收。
- spawn由真实用户父阻塞wait收取三类孩子，覆盖WNOHANG、完整PID/任意孩子/冻结组、PGID1的wait(0)、重复收取ERR_CHILD、非法选项/INT64_MIN及CHILD提示消费；继续验证既有创建/加载/限额/destroy回收。life的两架构可信机制场景及host覆盖父死亡后在物理改写/清理前收取、活孤儿和父槽复用。真实用户PID1的孤儿收取接续见本次init场景；被terminate的组wait续点仍缺专门端到端场景。
- 既有timer/irq、镜像/请求池耗尽、独立结果/收尾、旧身份复用等证据仍保留在此前stage日志；Darwin runner僵尸组EPERM诊断与压力证据在`build/host/stage16-runner/`。失败现场不覆盖、不删除。
- LLVM18.1.8、QEMU11.1.3、OpenSBI1.8.1；生成两架构及共享`build/compile_commands.json`、`.su`和`stack-usage.json`。物理ELF高半区别名运行仍保留`-fno-jump-tables`，无通用指针重定位。编译帧不是最坏栈深证明，不宣称真实硬件缓存、性能或CI通过。

## 接续

接生产init/启动包/清单与SYSTEM发权；继续完成信号/终端、资源reply、SHARED/DMA、fork/exec/COW、外设IRQ及用户态服务。完整边界与验收以01/03/04/06/08/11等主题为准，不以当前局部原生路径称全系统完成。
