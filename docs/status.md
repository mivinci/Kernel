# 当前状态

更新：2026-11-16（Asia/Shanghai）。设计以[总览](design/00-overview.md)及主题文档为准；规则见根目录AGENTS.md。

## 当前实现

- PR #5已合入main（`5b5edd3`）；当前分支`codex/process-management`，按用户新授权，每个验收点提交并推送到同一分支。
- 两架构启动、完整用户/内核IRQ整数与FP现场、DTB、伙伴、高半区、Set/Acct、Space/Mem/Mmap、CODE封存和缓存同步已接。Local 48字节、Life 128（含不可变四u32凭据及8字节不增引用的账目身份）、Proc 512、Prep 184、Mmap 64；计费guard栈与独立续点支持离栈回收，旧结果/账目/运行槽分别复用。
- 可信准备支持已确认的ALLOC=0x10、目标Acct出资、源身份/表epoch绑定、全部临时别名发布屏障、提交/取消立即撤权，以及固定10秒期限。IRQ最多扫描8份准备；CPU有界清理，旧别名继续保留原页/Acct/Life，最后引用解除才回收。
- 已接512份64字节Image记录，镜像号由Life单调分配；Proc构造/发布/退出与Prep取消/超时固定同一Image，旧清理引用经Image继续持Life，取消后号码不复用。Rq预付/可信状态机已接，完整管理准入与观察cookie仍未接。
- 已接1024份184字节Rq、每进程8/1/1/2预付及普通960/恢复64分区；FIFO/独立owned、15+1接收名额、取消/回复、三责任和临时引用回收已接。旧请求持双方Image，迁入退役链后可复用Proc邮箱；已接本地CALL/SEND/NOTIFY固定Image引用、减权复制及失效释放，原生IPC消息/阻塞已接，固定CONTROL/connect、grant/revoke、inherit已接局部profile，CHILD/LIFE/外设IRQ仍待接。
- 原生exit/yield/getid、自身alloc/free/map/unmap/seal、clock_gettime/getres已接；本轮增加固定CONTROL BUDGET及TERMINATE。FIFO用户timer约5ms；内核IRQ只记调度请求，受控自身alloc窗口完成后在检查点切换。短VM暂停窗口仍关IRQ。CPU空闲WFI处理开IRQ边界，独立推进到期和回收，不依赖父续点。
- 已接原生call/send/recv/reply/reply_recv/defer及请求查询/取消/ACK/finish/check，Config/运行等待复用48字节，Proc现512。独立C续点持Rq临时引用阻塞/唤醒，强等待边检查DEADLOCK/DEPTH；服务死亡失败携原RPC，真正收尾仍独立。recv已支持事件mask/筛选及交替；资源reply和原生rqclean仍NOTSUP。
- 已接notify/alarmset/alarmcancel，Event32纳入Proc上限（PID去重、当前image复用Rtable）；通知仅用户授权位，单调绝对闹钟有64位代次与重设/取消隔离。RQ_DONE/RQ_CANCEL由记录持久提示合并，结果与真正收尾分别提示；普通通知不退出强等待，CPU空闲可到期唤醒。
- 已接getcred(0x6d)：自身、固定CONTROL QUERY及真正REQUEST的SOURCE/CHILD准备关系核验；凭据在Life中不可变、故障与退出后继续保留。可信构造器可在发布前安装身份，完整原生CREATE/CREDENTIAL/SERVICE准入及FUTURE exec准备仍未接。查询不消费请求或扩大管理权限。
- 可信spacewrite整段预检用户可写Normal页，跨页只读/缺页失败不部分写，不绕过未来COW写准备；已供RESOURCE query输出使用。
- 原生query(0x73)已接PROC、RESOURCE、RQ及ACCT/LIFE，自身0/固定CONTROL QUERY，以及新增有效CONSTRUCT CONFIG仅RESOURCE范围；192字节记录、最多16条及完整输出预检；RESOURCE/RQ按各自槽索引/代次分页，PROC/ACCT/LIFE是整实例单条。窗口按类型还原，隐藏内部Prep索引；旧目标表回收只返回原范围为空，不选同槽新实例。SYSTEM及EXEC_VIEW创建仍待接。
- 已接128份48字节SESSION/GROUP共享关系池与原生getpg/setpg/setsid：自/子同会话、0选择器、保留组号检查及uid 0无豁免；Life仍128、Proc512。组长退出/收取及运行槽复用不改绑孩子PGID/SID；自动孤儿接管、成功exec后的改组限制与终端作业控制仍待实际接入。
- RV64使用SBI TIME；ARM64使用GICv2/虚拟timer，控制器页为Device属性，高半区紧急栈按PC相对地址构造。REALTIME未校准，与单调起点一起推进。仅单核、ARM缓存关闭，无IRQ嵌套或通用长Op。
- normal仍只初始化后停机，不构造模拟init；guest载荷仅在`tests/kernel/`。生成编译数据库、`.su`和`stack-usage.json`；共用现场列表在架构`context.inc`，其依赖纳入缓存；内核仍禁用绝对代码地址跳转表。

## 本地验证与边界

- `make test-all`通过197项host、格式及78个QEMU场景（两架构各37个基础场景，加4个RAM/virtio配置），日志`build/host/stage26-create-authority/test-all.log`；diff检查通过。
- timer场景验证两个无主动syscall忙循环、完整用户整数/FP及ARM标志保持、10秒到期与旧临时映射滞留。irq场景验证内核全部整数/FP现场、WFI边界、仅靠CPU空闲完成目标/Prep/Life/Acct回收，再运行4096次64KiB原生分配/释放，实际收到内核IRQ并经检查点切换。主机独立审计全部内核页表、最终根/特权/停机PC。
- images场景耗尽512槽并验证Proc构造失败回滚、恢复容量后构造/取消、旧Image/Life继续保留时同运行索引新PID的隔离；host另覆盖单ACTIVE、取消不复用号码、池满不烧号码、固定终结原因及旧观察不跟随新镜像。
- requests场景以948份已接收未收尾请求加接收者12份预付占满普通池，旧调用者运行索引已释放仍拒绝普通构造，恢复64槽保留；确认旧收尾后构造恢复。host另覆盖预算隔离、原子ACK、15+1、精确revoke、临时引用及死亡来源不交付。
- locals场景新增准备期入口、发布后CALL/NOTIFY解析、减权复制、同PID镜像切换后旧入口仍PEER及失效free回收；host另覆盖SEND、掩码/继承、满表失败不泄漏引用。原生connect最新切片见接续；该早期locals场景不代替后续IPC验收。
- ipc/ipcdepth通过真实双用户程序检查同步call/send/recv阻塞、异步控制槽/协作取消、reply_recv两段提交、结果与收尾分离、FP和全部IPC输出/来源image、自等待/双进程环，以及深度1返回DEPTH。已接受请求的服务退出后返回原RPC，测试在CPU栈可信确认旧收尾后恢复caller；不代表原生rqclean/任意DMA安全核验。
- events实测notify越权不部分置位、重复合并、EVENTS_ONLY筛选/公平偏好、BUSY保留、过去/未来/取消/重设及混合TIMER代次；两个进程均阻塞时由CPU空闲真实唤醒，时钟复查未提前返回。ipc另验证RQ_CANCEL唤醒和结果/实际收尾两次RQ_DONE。host补齐u64代次耗尽不改状态。
- host runner的EPERM已用独占组僵尸稳定复现：Darwin组信号跳过SZOMB，收取前EPERM、收取后ESRCH。已改为EPERM后有界等组消失，并在KILL后确认整组退出；持续拒绝/残留仍失败，单独保存cleanup_error。14项runner及1000次孤儿压力通过（真实72次SIGKILL EPERM），158项host/格式/68个QEMU完整回归通过。证据在`build/host/stage16-runner/`；此前stage07/15及首轮压力的失败记录保留。
- control实测CONNECT+GRANT才能生成可转授入口；目标INSTALL/REVOKE与源GRANT分别检查。两份独立授权精确撤销，QUEUED为REVOKED、ACCEPTED可回复；同步调用从原C续点唤醒。先map再revoke保留旧映射；旧目标退出/运行槽复用后旧CONTROL的connect/grant/revoke均STALE。inherit不改变rights/目标/掩码，NOTIFY可显式EXEC、私有MEM不行；未执行fork/exec。
- LLVM18.1.8、QEMU11.1.3、OpenSBI v1.8.1。栈水位/编译帧不是最坏栈深证明；无真实硬件缓存、性能或CI验收。完整原生CREATE/CONSTRUCT/CONTROL与完整管理准入、信号/作业关系、完整事件/信号阻塞调度、用户ELF/csu/init、SHARED/DMA、fork/exec/COW、完整IPC及服务尚未接通；当前native profile仍拒绝构造/临时加载权绕过准入。

## 接续

继续Sigctx必需预付、原生CREATE身份/SERVICE准入和构造加载，再接剩余管理、CHILD/LIFE与外设IRQ，以及信号/终端作业控制和加载；按01/03/04/06执行。当前固定CONTROL短事务不跨调度借表，CONSTRUCT/EXEC_VIEW与临时加载MEM操作仍受原生准入门禁，只读RESOURCE例外已接；SHARED/DMA、资源reply和完整创建装载未接。

本轮预算与终止见02/03末尾，creds验证旧来源退出/槽复用、DRAINING/取消准备只读、证明释放后的STALE，以及CALL/NOTIFY解除后Image消失而Life凭据继续保留；memory另验证输出复制的完整区间预检。RQ、组关系及准备期只读查询已通过188项host/格式/74个QEMU回归。旧日志与失败现场均保留。

用户已确认CALL_CONTROL=0x08只选自身控制槽；connect可转授产物须输入CONTROL CONNECT+GRANT，CONSTRUCT只允许USE（其原生创建门禁仍保留）。

用户已确认准备期只读RESOURCE查询：有效CONSTRUCT/EXEC_VIEW CONFIG只枚举精确准备表，其他kind拒绝，发布/取消/超期失效；父用真实SELF_SPACE/SELF_ALLOC值写Boot，不获得目标本地操作权。已同步03/06/08，代码、两架构creds及188项host/格式/74个QEMU完整回归通过。EXEC_VIEW创建仍未实现。

用户已授权按设计持续推进，遇实质冲突可停下询问；每个完成且验收通过的点提交并推送到当前同一分支。

会话/组规则已由用户确认并写入03/06/11：自身或未exec直接孩子、同会话、0选择器、setsid保留组号检查，uid 0无豁免。关系池、Life挂接及原生getpg/setpg/setsid已通过185项host/格式/74个QEMU完整回归；孤儿自动接管与成功exec后的改组禁止仍未运行验收。

本轮PROC实际统计已接并通过两架构stats局部验证：Proc仍512、Space64；真实组/账目/资源/映射/Rq计数与选中窗口CPU纳秒，阻塞不计费，旧CONTROL不跟随运行槽复用。完整189项host/格式/76个QEMU回归通过，日志见上；IPC/ipcdepth实测阻塞调用者强等待边和实际请求责任数。

用户已确认普通孩子CREATE的内存/峰值/资源/pending上限夹到其Createarg，并清除CREDENTIAL/SERVICE/GRANT，仍共享原Set总额。可信授权/继承与Rmail构造期接收上限机制已接，6项CREATE及2项Rq新增host、两架构createauth局部通过；Set56、Local48、Rmail64，完整197项host/格式/78个QEMU回归通过。原生CREATE仍NOTSUP，接续Sigctx必需预付与创建身份/SERVICE/加载事务。
