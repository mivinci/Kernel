# 终端、会话与信号

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：2026-11-17已接组/会话、wait策略、原生信号配置/两层帧/sigreturn/sigsuspend及libc mask子集，局部双架构证据见文末与status。终端/作业控制、真实来源、普通等待中断/重启及POSIX trampoline仍未完成，未完成CI/真实硬件/性能验收。

职责修订：普通会话/进程组与前台操作由 sh、tty 直接请求内核并接受关系/权限校验；init 仅配置初始终端权限。wait/自动收取依内核退出记录，不再以 init 复制和 exack 为前置。 以 [init 与普通操作的责任边界](07-boot.md#init-与普通操作的责任边界修订确定) 为准；既有资源安全条件继续有效，未细化的新接口不得提前实现。

## 控制终端与交互

已确定：serial 管 UART，tty 管终端实例，init 为 sh 指定控制终端，子命令默认继承，系统服务默认无终端。
数据路径为 sh/程序 ↔ tty ↔ serial ↔ UART；tty 不持有 UART MMIO/IRQ，只有 serial 获授这些设备资源。
tty与serial使用本篇固定SPSC共享环及静态用户通知位。
禁止 tty 通过长期不返回的 call 等待 serial 输入；tty 暂存客户端读请求并继续处理输出，serial 依设备事件推进收发。
待处理数据、通知重开、缓冲满、双方退出和共享区安全复用须在协议中明确。
控制终端独立于标准流；重定向输出不改变控制终端，终端标识本身不授予权限。
单串口首版支持回显、退格、行读取、换行处理和原始字节输出；定义输入缓冲满时的行为。
进程查询可返回终端标识供 ps 显示，终端身份复用也不得把旧关联指向新实例。

### 会话、作业与信号（按审查完善，已纳入本轮范围）

撤回单个前台 PID、Ctrl-C 直接强杀以及不设计进程组/信号的简化方案。
控制终端关联会话，前台目标为同会话进程组；子进程默认继承组，管道等同作业成员共享组而非依赖父子树。
init 配置初始授权策略；sh 组织作业，tty 管终端状态，内核强制组/会话及前台权限边界；身份均须防复用。
Ctrl-C 在启用终端特殊字符解释时产生前台组中断信号，默认终止，可捕获/屏蔽/忽略；强制终止另设不可捕获机制。
信号由内核安全点处理，不依赖目标执行 recv；处理函数入口/用户信号帧/sigreturn 须校验用户地址和特权状态。
需要 STOP/CONT、组级等待的停止/继续/退出状态、后台终端读规则、终端模式恢复及挂断/孤儿策略。
这些语义纳入本轮设计和验收，不宣称完整 POSIX 兼容；不得再以单进程特例固定不可扩展 ABI。
前台切换、组成员发布与 start 协调，失败可回滚；等待整个作业停止或退出后再交回 shell，不能仅看首个进程。
控制路径不得同步依赖可能阻塞在 fs 的 init；具体内核授权及终端/组关联提交协议须按[全局设计与冻结条件](00-overview.md)冻结。

内核是进程会话/组成员、终端会话关联及前台组的权威来源；tty 管输入缓冲/行模式，sh 管作业命令策略，init 管构造授权。
会话/组/终端使用有配置上限的关系表，不引入通用对象方法或消息队列；组长退出不销毁仍有成员或管理引用的组。
子进程默认继承组；sh通过setpg仅组织自身或尚未exec的直接孩子，且必须同会话。getpg、setpg和setsid的0选择器、组长限制及失败规则见[进程关系契约](03-process.md#会话与进程组操作2026-11-16用户确认)。uid 0不绕过关系检查，系统服务在启动流程显式setsid隔离。
tty 通过获授权终端句柄请求 INT，内核在投递点解析前台组，与前台切换有明确先后序；不使用 tty 缓存的 PID 作为权威目标。
暂存读取在实际交付前重查前台资格，前后台切换不能让旧读继续抢输入；输入的归属/丢弃策略由切换协议显式规定。
终端挂断独立于 close(fd)：挂断唤醒等待者、拒绝后续 I/O 并向授权作业发送 HUP；关闭一个 fd 不解除整个会话控制终端。

### 基础信号语义（已确定）

首轮支持 INT/TERM/HUP/PIPE（默认终止）、KILL（强制终止）、STOP/TSTP/TTIN/TTOU（默认停止）、CONT（继续）及 CHLD（默认忽略通知）。
实现接续：停止/继续的Life持久报告核心及wait接线见03末尾；调度暂停/继续、真实信号生产及CHLD停止/继续仍待接，不能将可信状态表用例当作实际用户作业控制。
WINCH 纳入首轮，默认忽略，表示终端尺寸变化；实际尺寸从终端状态查询，通知可合并。
KILL/STOP 不可捕获、忽略或屏蔽；KILL 对停止中的进程也立即进入内核退出处理，不等待 CONT。
每进程保存 pending/mask 和有上限的 action 表，普通信号可合并、不按次数排队；数据消息仍走 IPC。
忽略 CHLD 不丢弃持久子进程结果。CONT 即便被屏蔽也先解除 STOP，处理函数投递另按 mask/action；相互抵消过期 STOP/CONT 待处理状态。
进程只改自身 action/mask，发送至他进程/组检查独立授权；处理函数恢复使用受检查 sigreturn，不以普通 ret 恢复被中断现场。
后台终端读取采用 TTIN；启用相应终端策略时后台操作采用 TTOU。若停止信号被屏蔽/忽略或孤儿组无法由控制会话恢复，则返回明确 I/O 错误，不永久等待。
终端挂断投递 HUP，TTY 的 Ctrl-C/Ctrl-Z 等映射由终端模式决定；原始模式可交付普通字节。
孤儿组判定、停止/继续状态记录和 wait 的确认协议须在下一个生命周期规格中统一，不以信号位代替结果记录。

### wait 与组状态（已确定）

wait 可返回 STOPPED（停止信号）、CONTINUED、EXITED（退出码/终止信号/故障原因），附完整进程身份及单调状态序号。
父直接 wait 的交付方案只消费选定状态版本，不清除更新状态，不另要求用户 waitack；服务生命周期观察仍独立确认。具体交付见 [进程设计](03-process.md#父直接-wait-与结果消费已冻结)。CHLD 仅提示变化，持久结果是权威来源。
当前状态快照可更新，最终退出结果不可被后续通知覆盖，必须保留到确认；停止/继续不形成无限历史日志，不能把过期停止通知当作当前状态。
用户态支持指定子进程、任一子进程、授权作业组和非阻塞查询；目标须有父子关系或明确观察授权。
组中仍有运行成员则未停止，全部未退出成员停止才是组停止，全部成员退出才是组结束；组长先退出不销毁组。
终端归还与结果消费分开，按组状态申请切换，内核在提交时重查状态/成员变化及前台关联代次，防止查询后状态改变。

### 前台切换、模式与孤儿组（已确定）

切换按准备/提交执行：校验会话/权限/旧代次，tty 暂停旧组的新输入提交并处理暂存读，准备模式，内核提交新前台及代次，再恢复输入分发。
准备失败保留旧前台，提交后失败按新代次恢复；重入/过期控制请求不得覆盖更新关联。

前台切换epoch（2026-11-19用户确认）：getfg返回PGID/SID/单调epoch，setfg接terminal/pgid/expected_epoch，匹配才原子提交并递增；旧版本返回STALE且保留当前前台，epoch耗尽返回OVERFLOW而不回绕。ttybind只绑定尚未关联终端，ttysignal仍按实际提交时当前前台投递。epoch只描述该终端关联版本，不能作为其他终端的身份或授权；成功提交即推进版本，即便目标PGID相同也不能让旧请求再次成功。终端创建/镜像绑定、关联引用与挂断回收仍须按本篇生命周期规则独立验证，不能以epoch代替权限或完整准备/提交协议。原生创建/绑定/查询接续见文末，setfg/ttysignal及完整切换协议仍待实现。

例子：sh读到前台G0、epoch=e，准备切G1；另一次合法切换已先提交G2并推进到e+1，则迟到setfg(G1,e)返回STALE，G2和e+1不变。调用者须重新查询并按实际状态重新准备，不能把原请求改写为新epoch后盲目重试；在此之后的ttysignal依提交时当前G2投递。成功setfg(G1,e+1)与下一次信号的提交先后决定投递到G2还是G1，不依赖tty缓存的PID。
已交付旧组字节不可重复交付，未提交的旧读须重查资格；尚未分发的终端队列输入按显式保留/清空选项处理，不与旧请求私有缓冲混用。
作业停止时保存其终端模式、恢复 shell 模式再交回前台；fg 先恢复模式并设置前台再 CONT，bg 只 CONT 不取得前台。
后台读遵循 TTIN，后台模式修改/输出按已定义 TTOU 策略检查，不通过直接用户 RPC 绕过终端资格。
若组内没有成员具有同会话且组外的父进程，则为孤儿组；父子/成员关系变化重新判定，孤儿组含停止成员时发送 HUP 与 CONT。
验证原始模式程序异常、组长先退出、sh 退出、停止期间挂断及切换中 Ctrl-C，不能留下无法恢复的终端模式或停止作业。

- [ ] 定义会话/组身份、成员继承、前台切换原子点及其授权。
- [ ] 定义信号投递/屏蔽/合并、处理帧、sigreturn、阻塞 IPC 中断与共享缓冲收尾。
- [ ] 测试命令派生子进程、组长先退出、组停止/继续、前后台切换同时 Ctrl-C、tty/sh 退出与模式恢复。
- [ ] 忙循环进程不执行 syscall 也能接受信号；不得以普通事件位冒充异步信号。

### 信号、执行上下文与 RPC

独立内核栈路线要求：被中断 syscall 先沿原内核调用链退至统一用户出口，再递送 handler；不能挂着深层 C 栈直接进入用户 handler。handler 的 syscall 使用同一线程已退栈后的栈空间，sigreturn 仅恢复用户现场；具体组织见 [内核栈规格草案](03-process.md#独立内核栈的首轮实现组织已冻结)。

- 用户线程保持单线程，但可有受 CONFIG_SIGDEPTH 限制的嵌套信号上下文；有效掩码按进入前掩码、sa_mask 及当前信号组成，不默认屏蔽全部可捕获信号，KILL/STOP 不可屏蔽。
- 每层保存独立用户现场和同步等待引用；Rq 不嵌在单个可被覆盖的出站槽中。用户信号帧及内核校验记录在创建时预付。
- 捕获信号中断已提交call/rqwait只退出等待，原QUEUED/ACCEPTED请求及槽保留，call返回原RPC。是否请求协作取消由库/原生调用者决定，内核不因信号自动摘队或转DRAINING。
- 原请求后续reply固定结果到原Rq，不写handler当前现场；只有退出/exec等机制强制终结才转DRAINING、丢业务结果收尾。rqfinish仅用于rqcheck=4，不得丢弃仅协作取消（rqcheck=3）的实际结果。
- 回复完成先于信号的原子提交点：保存成功结果再投递信号，不把已经成功的调用改写为 EINTR。信号先退出等待则保留原RPC，不自动重放原操作。
- 信号处理函数的直接IPC使用该层独立预付Rq及同后端连接的专属页对（第1层2/3、第2层4/5），不再各持整条连接；遇外层接入中或本层旧页未安全归还时不等待外层、不窃取页，按10返回明确资源错误；stdio/malloc/一般服务库默认非异步信号安全，必须列出安全 API。
- 运行库修改共享槽表、RPC结果归属等本地元数据时短暂屏蔽可捕获信号，提交后恢复掩码；不得屏蔽期间阻塞 RPC，也不持锁跨用户处理函数。
- 为安全信号 I/O 提供独立的有界运行库上下文和缓冲，不复用被打断函数的锁/偏移临时状态；不能宣称所有 C 库函数均可在处理函数内调用。
- STOP 保留阻塞调用并禁止回用户态，完成结果可保存；CONT 恢复，KILL 强制撤离请求并异步清理，不等待用户处理函数。
- sigreturn 检查活动帧、用户 PC/SP、信号掩码和特权位，不能恢复内核地址或特权状态；达到配置嵌套上限时可捕获信号保留 pending，不丢弃。

验收：排队/已接收/回复提交各阶段注入信号，处理函数再次 IPC，验证消息与返回现场独立；STOP/CONT/KILL、坏帧和嵌套上限均覆盖。

### 信号中断与操作重启（已确定）

等待被信号打断不终结原请求。原生IPC不按SA_RESTART自动重发，call/rqwait只退出当前等待；已提交call返回同一RPC，之后只从它取得实际结果。rqcancel对QUEUED原子摘队保证无效果，对ACCEPTED只置cancel_requested，服务reply业务EINTR（保证零效果）、实际部分量、完整结果或真实错误；rqfinish不能替代这份结果。

POSIX信号入口先把中断返回的RPC/槽转交原Rctx；对read/write/open/poll等允许中断的等待，trampoline在调用应用handler前短调用rqcancel（poll按本轮已提交RPC列表处理），只提交标记、不等待服务，不占新Rq/共享槽。这样handler重入不会依赖外层先恢复才能提出取消。RESTART标志不改变这一步；handler正常返回后才决定继续领取原结果，或在原请求已经明确零效果且安全收尾后新发一次操作。close已摘除、exec替换准备等必须继续的事务按自身规则保留，不盲目对所有Rctx取消。原生ipc_*不注册这项POSIX取消策略，使用者自行决定。

SA_RESTART不允许绕过原结果：已发生部分量保留、完整结果保留，仅QUEUED/CANCELLED或服务明确零效果EINTR才可安全新提交；数据/参数沿原稳定输入，不按handler修改后的fd整数重新定位。已接收且进展未明时继续查同一RPC；服务失效或规定故障期限到期按明确EIO等结束并转预付后台责任，未安全停止的槽不可复用，也不新发RESULT查询或自动重做业务。信号入口不能等设备/服务才调用handler；进入handler的取消请求亦不承诺它的I/O不等待外部设备。

协作取消的故障等待采用CONTROL_NS=1秒，从第一次rqcancel记录成功起设置绝对逻辑截止，不因handler/STOP或重查重置，不转为内核请求deadline。handler返回/恢复时先查询原RPC，已固定实际结果优先；仍未终结且过期则当前read/write/open等报EIO并转预付后台记录。close已摘除按其原后台关闭规则，poll按已固定整体结局处理；不在handler执行期间强行向外层返回。后台只接管迟到结果/资源及排空，不再写已结束调用的应用缓冲；新资源晚安装时也须由该记录释放或交给既有补偿对象。

实现后仍须逐接口核对POSIX及真实信号返回窗口；当前是设计契约，不把模型当作完整SA_RESTART合规证明。

#### read/write 的中断与重启（已冻结）

本表适用于本阶段普通文件、管道和终端的 read/write；网络及后端特定超时另行讨论。先递送 handler，不为查询原业务结果推迟 handler；下表决定 handler 正常返回后的外层调用行为。

| 信号到达时原操作的状态 | 未设置 SA_RESTART | 设置 SA_RESTART |
| --- | --- | --- |
| 尚未提交，或服务确认零进展且不会继续产生效果 | 返回 EINTR | 继续确定安全的步骤/等待 |
| 已确认部分进展且能交付对应结果 | 返回实际字节数 | 同样返回实际字节数，不补满原长度 |
| 完整结果已固定 | 保留原结果 | 保留原结果 |
| 已交付，进展尚不清楚 | 跟踪原操作，取得明确结局或报告明确故障，不能凭 Rq 中断伪报零进展 | 跟踪同一业务操作，不能另发一次读写 |

服务消费输入、接受写入或推进File偏移时，记录实际完成量及原槽数据的归属；协作取消与业务提交在服务自己的状态机排序，但不提前终结Rq。read数据保持在原槽直到复制/保存，不通过第二次请求重读；原Rq保存短结果，服务reply后不再保留等待业务ACK的结果表。原槽安全归还前不复用。
无 SA_RESTART 不表示允许无限等待故障服务；无法确认时按明确故障结束，不伪报 EINTR、成功或已知完成量。复制失败遵循运行库 EFAULT 规则，不把服务完成量直接冒充已交付字节数。
每次操作固定原 File/会话引用；handler 关闭并复用同号 fd 后，外层不按 fd 整数重新定位对象。安全继续仍须遵循原 File 的有效性、关闭及取消协议，不隐式重新打开文件。

示例：终端 read 等待100字节。信号先到且服务确认零进展，无 SA_RESTART 返回 EINTR，有则在 handler 后继续等待；若已经消费20字节，则保留这20字节，复制成功后返回20。handler 将原 fd3 关闭并复用给另一文件，外层仍只处理原终端操作；服务崩溃导致结果无法恢复时报告明确故障，不从新 fd3 补读。

原生 IPC 的已接受方向统一见 [捕获信号中断矩阵](04-ipc.md#原生-ipc-的捕获信号中断矩阵第6项审查修订)：不按 SA_RESTART 自动重发；call/rqwait中断都只撤离等待，已入队call返回原RPC，rqcancel对排队/接收分开，recv/send 已提交的交付不重做。

### sigaction 表示与安装（已确定）

公开 sigaction(signum, action, old_action) 由 libc 将 SIG_DFL/SIG_IGN/用户函数转换为原生定宽动作类型 DEFAULT/IGNORE/HANDLER、入口地址、掩码及支持标志，不直接将特殊函数指针值当执行地址。
只修改自身，校验信号/支持标志，未知位拒绝；KILL/STOP 不可修改。HANDLER包含handler和trampoline两个用户执行入口，均须合法且不能内核/MMIO地址；完整检查后原子替换，原生输入/输出校验失败保留旧动作。DEFAULT/IGNORE 的两个入口字段为零。
trampoline是运行库提供的信号前置及返回入口；递送时先进入它，由它调用本次快照中的handler，最后sigreturn。自定义运行时可替换；递送时重新验证两入口，不依赖固定libc地址/符号。
需要交付旧动作时，原生/兼容适配须保证输出路径及副作用契约，不在替换成功后才因无效输出返回“未安装”；具体输入/输出布局及受检查复制如何配合仍须在 ABI 冻结。
递送时重新检查入口/执行映射，注册后被 unmap 则产生受控用户故障，不进入非法内核执行。IGNORE/掩码变更不取消已有请求收尾，不复用旧 IPC 缓冲。

#### 示例：坏入口不改变旧动作

SIGINT 初始 DEFAULT，安装的新 HANDLER 入口不合法，调用失败且原动作保持；安装成功后程序主动 unmap 处理代码，再递送不能触发内核异常或跳往内核空间。

### sigreturn 与用户信号帧（已确定）

内核先进入注册的信号trampoline，它完成必要前置操作、调用用户handler，handler返回后由同一trampoline执行sigreturn(frame)恢复用户上下文；自定义运行时可提供自身入口，不将 libc 私有符号硬编码进内核。
仅允许当前最上层活动信号帧，校验所属上下文、地址/大小/对齐、用户 PC/SP 及架构允许的寄存器/FP状态；特权控制位由内核构造，不能照抄用户帧。
恢复掩码不能屏蔽 KILL/STOP，帧成功消费后不可重复返回或影响新上下文。非法帧/恢复地址/特权位直接以 BAD_SIGNAL_FRAME 用户异常进入强制退出，POSIX 状态映射 SIGSEGV，不执行半恢复、不回 handler 继续或再递送可捕获 SIGSEGV 递归处理。
内核 Rq/等待/取消结果及共享槽引用不由用户帧恢复；处理函数不能靠修改帧复活已取消请求或覆盖已提交终态，接口重启走既定协议。

### 示例：伪造特权位不能复活 READ

READ 被信号中断后，处理函数修改帧中的特权字段并 sigreturn；内核拒绝非法状态或按明确架构规则验证，不获得特权、不将旧 READ 重新置为等待。合法帧按约定恢复中断结果或已定义的重启路径。

### sigprocmask 与 pending（已确定）

公开 sigprocmask(how, set, oldset) 使用 SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK 增加/移除/替换当前上下文有效掩码；set 为空只查询，oldset 需要时交付修改前值，具体输出与原子副作用契约在 ABI 冻结。
掩码更新与 pending 检查原子提交；KILL/STOP 位不进入有效屏蔽。被屏蔽普通信号保留 pending，相同信号按位合并，不承诺实时信号排队语义。
解除屏蔽在返回用户态前安全出口检查递送，默认终止/停止不要求 recv；处理上下文基于旧掩码/动作附加掩码/支持标志建立，sigreturn 恢复保存掩码，不用单一全局保存值覆盖嵌套上下文。
动作、掩码和请求收尾分别管理，不以掩码变更撤销旧 Rq；需要原子临时掩码等待时使用 sigsuspend，不拼接 unmask 与普通 recv。

### 示例：屏蔽期间收到 SIGINT

进程屏蔽 SIGINT，期间收到信号仅置 pending；解除屏蔽后安全出口按动作递送处理或默认行为，不需再次生成信号或等待普通消息。重复 SIGINT 可合并，但 pending 不因查询/掩码变化丢失。

### sigsuspend 原子等待（已确定）

公开 sigsuspend(mask) 保存当前掩码，并将安装临时掩码、检查 pending 和登记信号等待作为原子状态更新；使用当前上下文预付记录，不新增等待对象。
符合条件的捕获信号执行处理函数后恢复原掩码，返回 -1/errno=EINTR，不因 SA_RESTART 自动重等；默认终止则结束进程，不返回，默认忽略不构成有效完成。
STOP 暂停而不丢等待状态，CONT 恢复后按实际信号/等待条件继续；普通消息/通知/TIMER 不使 sigsuspend 假成功。KILL/STOP 仍不能被临时掩码屏蔽。
SIGCHLD 等信号是提示，调用者醒来检查真实子进程状态，不按信号次数消费退出结果。

#### sigsuspend 的掩码恢复位置（已冻结）

用户已接受以下细化方向；信号/运行库布局和标准审计仍未完成。

确定明确三份逻辑掩码：调用前 M、临时等待掩码 W、本次 handler 的有效掩码 H。校验并快照 W 后，内核原子保存 M、安装 W、检查可递送 pending 并登记等待；输入校验失败不安装 W。捕获信号 S 时，H 由 W、action.sa_mask 及未设 SA_NODEFER 时的 S 组成，不在 handler 执行前先恢复 M。

本次handler正常返回且未主动修改合法返回mask时，经sigreturn恢复M 并让原 sigsuspend 返回 EINTR；不能先返回应用再由 libc 单独调用 sigprocmask 恢复，以免在临时掩码下出现应用执行窗口。信号上下文须能区分 handler 生效掩码基线 W 与最终恢复掩码 M；这是现有 Sigctx 的特殊恢复责任，不新增等待对象。

handler中临时sigprocmask修改当前有效掩码，普通正常返回仍以该活动帧保存的M恢复；用户显式修改合法帧restore_mask时按新值校验恢复；嵌套 handler/嵌套 sigsuspend 各自保存自己的入口掩码，不覆盖外层 M。非局部跳转或用户主动修改恢复 context 的契约不由本例提前冻结。恢复 M 后立即重新检查 pending，不能让本应递送的信号永久滞留；允许在外层调用最终返回应用前先运行另一个合法 handler。

被屏蔽或忽略的信号不结束等待；纯 STOP 不结束等待，CONT 只解除停止。若 CONT 本身配置了可递送 handler，则可按普通捕获信号结束 sigsuspend；KILL/默认终止不返回。SA_RESTART 均不让已经被捕获信号结束的本次等待重新挂接。

示例：M 屏蔽 SIGCHLD，W 解除 SIGCHLD。登记时发现 pending SIGCHLD，立即进入 handler，其有效掩码按 W 和动作构造。正常返回原子恢复 M，随后 sigsuspend 返回 EINTR，调用者在 SIGCHLD 再次被屏蔽的状态下检查孩子结果；不需要第二个 SIGCHLD 才醒，也不会短暂把 W 暴露给恢复后的主程序。

本段具体 Sigctx/原子恢复组织为本项目细化提案。本轮检索未取得可核验正文，POSIX.1-2024 完整标准核对仍未完成。
待核对资料：[GNU C Library sigsuspend](https://sourceware.org/glibc/manual/2.29/html_node/Sigsuspend.html)。

### 示例：等待子进程不丢 SIGCHLD

调用者先屏蔽 SIGCHLD 并检查子进程结果，尚无结果时用临时解除 SIGCHLD 的掩码 sigsuspend。检查后到达的信号已置 pending，原子换掩码/等待时立即发现，不会错过后永久睡眠；返回后恢复调用前掩码再检查真实结果。

### 信号投递的权限与目标（已确定）

普通进程按可信凭据/自身权限/关系校验目标；tty 持终端授权只向当前前台组投递获准终端信号；init 持管理授权投递或强制终止。SIGKILL 不可捕获也不绕过发送权限。
tty 提交终端 Handle 与允许信号，由内核在投递解析点取得当前前台组并固定关系身份，不接受 tty 缓存的任意 PID/组替代；与前台切换有明确原子先后。
POSIX kill 正/零/负选择器由 libsys 转为明确目标种类与完整身份，内核最终授权；负数不能直接作为普通 Handle 解析。sig=0 仅检查存在/权限、不置 pending，组身份不依赖原组长仍存活。
投递成功不等于处理函数已执行或目标已退出；管理者按退出记录确认终止。普通 notify 不伪造终端信号、SIGCHLD 或强制终止。

### 示例：Ctrl-C 与前台切换

tty 识别 Ctrl-C 后通过终端授权请求 SIGINT，内核按提交时明确顺序解析该终端当前前台组；前台若已切换则作用于新关联，若尚未切换则作用于原关联，不以 tty 旧 PID 缓存为准。组投递在当前线程内核栈固定组身份及最多64个成员PID/必要Life引用，每批32个只允许调度，不进入用户handler；同次游标到完整投递/失败端点才退栈。源STOP/退出仍可仅推进这份内核责任，已投递不重发；新入组成员不补投本次信号。

### 用户同步故障与信号（已确定）

普通用户代码故障按冻结的架构映射表转换为同步信号：地址/权限 SIGSEGV、非法指令 SIGILL、明确总线/对齐 SIGBUS、支持的算术异常 SIGFPE；有合法动作则递送，默认终止并保留硬件故障来源。
处理函数合法返回通常恢复原故障位置，内核不擅自跳过指令；条件未修复可再次故障。处理成功继续执行不产生退出记录，最终终止才映射 POSIX wait 状态。
处理入口/帧非法、信号栈无法安全使用，或同步故障信号被忽略/屏蔽时按本系统明确不可恢复策略直接受控终止，不在损坏环境递归投递。
__sys_copy 受控检测到坏用户地址返回地址错误，不递送普通用户故障信号；内核自身异常仍保存诊断并停止，不包装成用户 SIGSEGV。异常映射不只依赖笼统 trap 类别。

### 示例：非法访问与受检查复制

应用直接读未映射地址，合法 SIGSEGV 处理函数可运行，未修复且返回原 PC 会再次故障；相同坏地址作为 __sys_copy 输入则得到错误，不能递归触发用户 handler。内核自己访问错误地址不走上述用户恢复路径。

### 处理期间掩码与嵌套容量（已确定）

处理期间有效掩码等于进入前掩码加动作 sa_mask，加当前正在处理信号；支持并设置 SA_NODEFER 时不自动加入当前信号，但不移除原掩码/sa_mask 中显式屏蔽位。
其他未屏蔽可捕获信号可使用预付嵌套上下文，sigreturn 恢复对应层进入前掩码并重新检查 pending，不能以实现简化名义隐式屏蔽全部信号。
每层现场/Rq/共享槽受 CONFIG_SIGDEPTH 及预付资源约束；达到上限时普通异步信号保留 pending，不能安全递送的同步故障按不可恢复策略终止。KILL/STOP/默认终止不需再分配 handler 上下文。

### 示例：SIGINT 处理时屏蔽 SIGTERM

SIGINT 动作的 sa_mask 含 SIGTERM，处理时新 INT/TERM 均 pending，其他允许信号可嵌套。返回后恢复原掩码再处理 pending；SA_NODEFER 仅取消自动加 INT 的步骤，不能清除显式 sa_mask 中的 INT。

### 首批 sigaction 标志（已确定）

本阶段纳入 SA_RESTART（逐接口等待重启，不重放副作用）、SA_NODEFER（不自动加入当前信号但保留显式掩码）、SA_RESETHAND（递送时恢复默认后进入本次 handler）及 SA_SIGINFO（三参数处理入口）。未知/尚未支持标志拒绝，不静默忽略。
SA_SIGINFO 提供已完整初始化的信号/来源类别、适用的发送者 PID/UID、用户故障地址/原因，以及被打断用户 context；不适用字段清零，不泄漏内核地址。siginfo/context 布局在共享 ABI 冻结，架构寄存器部分隔离。
context 的修改仍受 sigreturn 校验，不得恢复特权或内核地址。SA_NOCLDSTOP/SA_NOCLDWAIT 与 wait 策略联合冻结，SA_ONSTACK 须配完整 sigaltstack 契约，不能仅提供标志当作支持。

### 示例：一次性 SIGINT 处理

SIGINT 设置 HANDLER|SA_RESETHAND，第一次递送时动作先恢复 DEFAULT，再执行该 handler；后续 SIGINT 使用默认动作。SA_SIGINFO handler 则按 (sig, info, context) 调用，不按单参数函数错误传递。

### SIGCHLD 策略与 wait（已确定）

SIGCHLD 的 DEFAULT 默认忽略通知但保留子进程 wait 结果；显式 IGNORE 按自动收取策略处理，不与 DEFAULT 合并。纳入 SA_NOCLDWAIT，不保留普通可等待退出结果；纳入 SA_NOCLDSTOP，控制规定停止/继续通知而不删除实际状态。
退出结果由内核按当前合法父关系及其 SIGCHLD 策略决定保留或自动收取，不再先交 init 复制/exack。自动收取只解除普通父收取责任，不跳过 Life/Acct/Rq/观察/DMA 清理。
方案在退出结果提交点记录父完整身份及当时策略版本：DEFAULT/HANDLER 保留结果，显式 IGNORE 或 SA_NOCLDWAIT 不保留普通可收取退出结果。与 sigaction、wait 交付和孤儿转交原子排序，已经提交的 wait 返回不能被后续改策略夺走。
方案采用 SA_NOCLDWAIT 且未显式 IGNORE 时仍生成退出 SIGCHLD；显式 IGNORE 不生成该通知。阻塞 wait 必须重新检查匹配孩子及结果：没有匹配孩子时返回无孩子错误，仍有存活匹配孩子时不能仅因暂时自动收取一个孩子就提前报告无孩子。此通知取舍参考 [Linux do_notify_parent](https://github.com/torvalds/linux/blob/master/kernel/signal.c)，不是已完成的 POSIX 审计。
策略切换方案只影响后续退出提交，不追溯丢弃已经保留的结果；曾自动收取的结果也不因切回 DEFAULT 重建。历史结果与已提交交付保持稳定，首版行为已固定，标准兼容核对是交付验收。SA_NOCLDSTOP 影响通知，不删除可报告停止/继续状态。

#### SIGCHLD 策略切换与历史结果（已冻结）

确定保留前文“策略只影响后续退出提交”的方案，并把通知与收取责任分别裁决。sigaction 安装策略、孩子退出结果提交、wait 领取及父死亡转交在既有短临界区按顺序决定：退出先提交则保存其已决定的保留/自动收取结局；策略先提交则后续退出使用新策略。已提交给外层 wait 的结果始终保留，不被 handler 修改动作夺走。这里只要求裁决结果稳定，不要求每个 Life 永久保留完整 action 副本或新增全局策略对象。

安装显式 SIG_IGN 时可按信号规则丢弃尚未递送的 SIGCHLD pending 提示，但不因此消费历史 wait 结果。改回 DEFAULT/HANDLER 不复活曾自动收取的结果，也不为现存结果伪造“每个孩子一份新 SIGCHLD”；调用者需要主动 drain wait，不能以没有 pending 提示推断没有历史结果。SA_NOCLDWAIT 只改变后续普通退出结果的保留决定，未 IGNORE 时仍通知的既有本系统方案保持；SA_NOCLDSTOP 不删除停止/继续状态。

例子：A 的孩子 B 在 DEFAULT 下退出并留下结果，C 仍运行。A 改 SIG_IGN，B 的结果仍可由 wait 收取；C 此后退出自动收取，不留下普通结果。A 再改 HANDLER，不重建 C，也不合成 B 的退出事件；若 B 未被其他合法路径收取，主动 wait 仍得到 B。信号 pending 清除、父 wait 消费和 Life/Acct/设备实际清理是三个独立责任。

孤儿接管及 exec 动作变化也不重新解释历史结果。已保留结果交接给 init 仍可收取；新镜像沿用同一 PID/Life 的父关系，已保留孩子结果不因 exec 重置 handler 而消失。后续退出使用实际父当时动作/标志，不以“此孩子创建时的策略”决定终生行为。SIGCHLD 明确 IGNORE 在 exec 后保留的本系统方向保持，SA_NOCLDWAIT 等标志的 exec 重置须与动作安装一起核对。

局部依据：Linux do_sigaction 安装忽略动作时清 pending，而 do_notify_parent 在孩子退出时决定 autoreap；该源码为机制分工参考，不是本项目完整标准证明。POSIX.1-2024 sigaction/exec 正文本轮仍403，历史结果规则、NOCLDWAIT 通知及 exec 时序的完整审计未完成。
参考：[Linux kernel/signal.c 的 do_sigaction/do_notify_parent](https://github.com/torvalds/linux/blob/master/kernel/signal.c)。本节细化由本轮自主决策授权确定。

### 示例：显式忽略不跳过设备清理

父进程显式忽略 SIGCHLD，内核在孩子退出结果提交时自动解除收取责任；孩子遗留 DMA 仍保留对应 Life/Acct，待设备停止再清理。父 DEFAULT 则即使无 handler 仍可 wait 取得退出结果，不需要 init 代办。

### exec 的信号继承（已确定方向）

exec成功将HANDLER动作重置DEFAULT，旧handler/trampoline不进入新镜像；显式IGNORE按本系统选定策略保留，包括SIGCHLD。动作附加掩码/处理标志重置，不继承旧handler配置；显式IGNORE导致的自动收取语义仍依动作存在。
保留调用时应用有效掩码及尚未递送pending，按新动作/掩码继续处理；内部exec临时屏蔽不能混入继承掩码。已经递送的当前handler信号不重新变pending。
所有旧活动帧失效，不能sigreturn回旧镜像；提交前失败则旧handler/帧/掩码仍可继续，成功进入新_start不再返回旧handler。STOP/EXIT独立于exec运行屏障，解除屏障不清STOP，KILL提交后不能再运行新镜像。
新镜像首次回用户态前检查pending，不先运行任意新代码再补信号；具体动作表/掩码和镜像切换在同一提交关系中冻结，避免出现新地址空间搭配旧处理入口。

### 示例：SIGINT handler中exec

handler中exec成功，新程序不返回旧handler，SIGINT若仍在应用有效掩码中则继续屏蔽；调用者希望新程序接收INT可在exec前显式解除。exec提交前失败则返回原handler，随后能正常sigreturn。

### fork 发布与信号的交接（已冻结）

fork 的准备中断、发布后成功结果、STOP/CONT/KILL 与超时排序见 [进程设计](03-process.md#fork-失败信号与发布顺序已冻结)。STOP 不清除准备或自动唤醒，KILL 不等待用户态回滚。

## 首版信号、恢复与历史结果规格（已冻结）

支持信号编号：HUP=1、INT=2、QUIT=3、ILL=4、ABRT=6、BUS=7、FPE=8、KILL=9、USR1=10、SEGV=11、USR2=12、PIPE=13、ALRM=14、TERM=15、CHLD=17、CONT=18、STOP=19、TSTP=20、TTIN=21、TTOU=22、WINCH=28。其他编号EINVAL，不宣称实时信号队列。mask位为1<<(signum-1)；未知位拒绝，KILL/STOP不进入有效mask。CHLD/WINCH默认忽略通知，CONT先解除STOP再按动作处理；STOP/TSTP/TTIN/TTOU默认停止，其他默认终止。无core文件支持，诊断保留明确故障原因。

Sigact见06。flags RESTART=1、NODEFER=2、RESETHAND=4、SIGINFO=8、NOCLDSTOP=16、NOCLDWAIT=32；后两位只允许CHLD，未知位拒绝，ONSTACK/sigaltstack首版不支持。DEFAULT/IGNORE的handler/trampoline必须0，HANDLER两入口合法且EXEC映射；注册和递送时都校验。handler可重入的标准接口按08预付上下文执行；普通fork钩子不声明异步安全，_Fork没有钩子。

Sigframe前缀64字节：magic、bytes、depth、previous_user_frame、restore_mask、flags、cookie、reserved八u64；magic=0x4b53494730303031、flags=0，cookie在offset48、reserved在56且必须0。cookie由内核单调分配，用户不能用其他值替代；sigreturn须同时匹配当前最上层帧地址、深度、完整PID/image归属与内核保存的cookie。Siginfo在64处占64字节：signum(u32)、code(u32)、pid(u64)、uid(u32)、reserved0(u32)、address(u64)、reason(u64)、status(u64)、reserved[2] (u64)。TF在128，FP在416；RV64总688、ARM64总944，均16字节对齐。previous仅用户帧地址，无内核指针；原用户现场仍保存在TF，不以信号入口当前寄存器覆盖它；具体trampoline六参数入口见下文，公共handler仍为一参数或SA_SIGINFO三参数。

每进程预付2层Sigctx，普通帧位于当前用户栈下方、检查完整可写/普通内存和16字节对齐，不能向guard/MMIO写帧。栈不足/非法帧以BAD_SIGNAL_FRAME→SIGSEGV不可恢复退出；不靠递归SIGSEGV掩盖。用户可修改可合法恢复的PC/SP/GPR/FP及mask，但恢复时重新检查用户范围、执行映射、EL0/U态、允许特权位、FP控制合法值；不能修改内核保存的Rq终态/已经消费结果或重启责任。只能最上层当前帧，重复/旧image/其他PID拒绝并终止，sigreturn不回已退栈C续点。

### 信号前置入口（交叉复核修订）

Sigact仍48字节，offset16字段由旧草案的restorer改为trampoline；它不是仅供handler返回的地址。内核对本次动作取得稳定快照、保存完整原TF/FP及活动Sigctx、安装H掩码后，令用户PC=trampoline，a0..a5/x0..x5依次为[signum,frame+64,frame+128,handler,frame,action_flags]；SP=frame（16字节对齐，调用栈向更低地址增长），ra/x30置0，入口不得作为普通函数返回。原Sigframe大小/偏移和用户寄存器恢复规则不变，不新增syscall。

trampoline从frame中的原TF及原Rctx识别被中断call的RPC，不能把入口a3/x3里的handler地址误当RPC。先把RPC/页归属稳定转交原Rctx、按既定策略短调用rqcancel，再进入应用handler；不等待服务。handler按本次action_flags选择一参数或三参数原型调用，保存并恢复libc层errno，返回后以frame调用sigreturn。libsys只提供前置/结果归属辅助，不反向调用libc errno或直接读取libc私有表。

只有原TF的PC位于已声明的原始syscall返回窗口、且原Rctx阶段/镜像匹配时，才按06解码尚未接管的原始输出；桩在把结果写入稳定记录并发布完成标记前不能覆盖这些输出。任意应用寄存器数值不作为RPC。异步提交成功或已固定资源结果同样先接管归属，不因信号把它丢掉。

子fork/_Fork在本地Fix完成前收到信号时，trampoline先运行与子返回桩相同的本地修复，再允许应用handler使用库。通过本次fork返回窗口、稳定Fix记录或有界Sigframe链定位子Fix，不扫描任意C栈；修复阶段以真实子身份为归属标记，幂等且仅短暂屏蔽可捕获信号，无远端RPC。嵌套在屏蔽前到达可先完成这同一修复，外层随后只确认完成；修复开始改动记录后保持短屏蔽直到表一致，KILL/STOP仍有效。不把临时屏蔽带到子应用或改变继承掩码。需要安全解除的父staging别名通过子自身unmap完成，父实际映射/在途访问不受影响，细节见08。

handler、flags、掩码及trampoline均来自同一次内核动作快照；不采用先后更新“内核dispatcher入口+用户handler表”的双份动作状态，避免sigaction/RESETHAND/嵌套期间错用另一版本handler。原生调用者可以注册自己的trampoline，但跳过POSIX适配不自动获得库的中断/errno保证。NODEFER嵌套以各活动frame.depth选择预付Rctx，不能只靠一个可被打断的全局depth自增；原Rctx所有权未转交前不发布为可复用。fork子修复保留本层合法frame/入口，exec则使旧活动帧失效。

例子：旧READ退出等待返回h，信号入口a3是本次handler地址，原TF里的a3才是h；trampoline登记h并请求协作取消后调用handler。此时另一个信号嵌套，使用下一frame/Rctx，不把h当作自己的新操作。sigaction在此期间换handler只影响后续递送，外层仍调用已快照的原handler。原回复中的资源/部分量仍从h领取。

信号与运行库操作交接：内核Sigctx保存被打断返回寄存器/等待结局及必要Rq临时引用，建立用户帧前原syscall先退栈。Rctx在每次syscall前已登记准备，handler使用下一预付层；外层已提交结果保持，返回后先接管结果再改变库表。无需新增通用延迟信号syscall或公共OP；长克隆快照CPU阶段的handler推迟只到03规定完整提交/回滚端点。

sigsuspend接受前保存M、临时W与handler H分开；原子换W/查pending/登记。捕获时H=W|action.mask|当前信号（NODEFER例外），正常sigreturn恢复M后原调用EINTR；嵌套各自保存。普通handler的sigprocmask变更不覆盖活动帧保存的返回mask，用户主动修改合法frame.restore_mask则按受检查新值恢复；不提供siglongjmp/setcontext承诺。恢复mask后立即复查pending，可能先递送另一handler才回应用。

SIGCHLD在退出结果提交点决定保留/自动收取：DEFAULT/HANDLER保留，显式IGNORE/NOCLDWAIT自动收取；NOCLDWAIT未IGNORE仍通知，IGNORE不通知。切IGNORE丢待递送CHLD提示，不消费此前已保留结果；切回HANDLER不复活自动收取、不按历史结果数补信号。wait主动检查持久结果，不能靠信号次数。策略安装/退出提交/wait/孤儿迁移原子排序；已交付结果不得夺回。该选择作为首版接口行为固定，完整POSIX合规仍未完成，不因Linux参考路径而宣称所有Unix同一行为。

例子：B在DEFAULT下退出→保留；改IGNORE→pending可清但B可wait；C随后退出→自动收取；改HANDLER→B仍可主动wait、C不复活。父exec保留PID/Life及历史结果，旧HANDLER重置DEFAULT、显式IGNORE保留、附加标志清零，后续退出按新动作，旧历史不重解释。只解除父收取责任，DMA/观察/Acct按真实清理继续。

终端由init用ttycreate(SYSTEM POLICY,TTY CONTROL)建立，绑定TTY完整PID/image；返回的TERMINAL授权仅通过构造grant减权给TTY/sh。TTY镜像终结即撤销该终端新操作，旧SID/前台关联进入挂断清理，引用归零后回收，重建TTY使用新终端代次。内核不解析UART协议，也不由普通进程用整数自建终端。

首次发权细则（2026-11-19用户确认）：SYSTEM POLICY=0x10，只授精确初始init PID1/image1；owner_control须有CONNECT。ttycreate固定到解析时目标PREPARING/ACTIVE的精确镜像，不能随CONTROL的Life绑定追随exec。返回调用者本地的QUERY|FG|SIGNAL|BIND|GRANT、完整允许信号mask、继承0，以及不提供权限的公开TERMINAL_ID；目标表须另经准备期grant取得缩小授权。资源表满或终端池满等失败清理全部临时引用，失败寄存器输出全0。

绑定与查询细则（2026-11-19用户确认）：ttybind要求BIND这一显式管理权，可跨调用者会话，把尚未关联的终端绑定到指定且有存活成员的会话；不自动设置前台，不重绑已有终端。getfg只要求QUERY，可跨会话读该终端的PGID/SID/epoch；未绑定时PGID=SID=0且epoch=1，已绑定但未选前台时PGID=0、SID为绑定会话、epoch仍1。setfg另要求FG及调用者属于关联会话，并接TTOU检查；QUERY查询不授予切换权。SID/PGID均为既有公开身份，不接受裸内部关系槽号或以uid0绕过授权。

ttybind的SID=0或不是完整PROCESS语法返回BAD_HANDLE；找不到既有会话返回STALE，只剩预留/清理引用而无存活成员返回STATE。已绑定终端不重绑（STATE），会话已被另一未释放终端占用返回BUSY；失败不改变关联和epoch。getfg/ttybind传非TERMINAL本地Handle返回BAD_HANDLE，正确类型缺业务权返回DENIED；owner结束返回STALE。getfg成功才同时发布公开PGID/SID/epoch，失败三个输出均0；无前台与无会话各自用0表达，不能将内部SESSION_ID/GROUP_ID直接当作公开PROCESS身份返回。ttybind只使用a0/a1，getfg只使用a0，其他输入非0均拒绝。

例子：init与sh分属会话S0/S1，T未绑定时双方QUERY均得到0/0/1；init凭BIND将T绑到有存活sh的S1，查询得到0/S1/1。sh仅持QUERY时可查该结果，调用ttybind则DENIED；再次绑定T返回STATE，另一终端抢S1返回BUSY，均保留原关联。后续选择前台必须走独立FG与TTOU检查；绑定本身不选择sh组、不推进epoch。T的tty owner结束后，两者getfg均STALE且输出0，Local枚举仍显示原tty身份，不能改绑到新镜像。

例子：init用TTY构造期间已获得的持续CONTROL创建终端T，再向该准备表grant SIGNAL/INT子集，并向准备中的sh授QUERY|FG和FORK|EXEC。即便CONTROL含INSTALL，改用它运行中grant TERMINAL仍拒绝。若TTY在init持T期间退出，T仍只报告旧PID/image；新TTY镜像必须重新ttycreate，旧T不能对新实例操作。调用者表已满时ttycreate不安装T；释放一个槽后可重试，不遗留占池终端或旧Image费用。

2026-11-19用户确认Local权限：QUERY=1、FG=2、SIGNAL=4、BIND=8；SIGNAL只授INT/QUIT/TSTP/HUP/WINCH的子集。grant offset表示减权后的信号mask，length=0，其他权限仍只能缩小；无SIGNAL则mask=0。QUERY/FG可使用既有FORK/EXEC继承位，SIGNAL/BIND不继承；从完整管理权减为QUERY/FG时可给产物设置这两种继承标记，不要求完整管理权自身带可继承标记。TERMINAL只经有效CONSTRUCT/EXEC_VIEW准备期转授，运行中CONTROL INSTALL拒绝，资源reply亦不交付终端权。RESOURCE查询data[3]为信号mask、data[4]=0、data[6..7]为精确tty PID/image；查询旧失效Local仍报告其旧身份，不把它改绑到新镜像。

tty模式首版：ICANON、ECHO、ISIG、ICRNL、OPOST/ONLCR；控制字符VINTR=^C、VQUIT=^\、VSUSP=^Z、VERASE=DEL、VKILL=^U、VEOF=^D、VMIN/VTIME。规范行缓冲4096，原始环8192；溢出丢当前超限输入并记录计数，不覆盖已有待读内容。非规范模式固定四种组合：VMIN=0/VTIME=0立即返回已有字节或0；VMIN>0/VTIME=0等min(VMIN,请求长度)字节；VMIN=0/VTIME>0从接受READ起计时，有任意字节即返回、到期无数据返回0；VMIN>0/VTIME>0先无期限等首字节，再按每次到字节重置的间隔计时，达到min或间隔到期返回已有量。VTIME单位0.1秒，O_NONBLOCK优先返回现有数据或EAGAIN；不新增内核TTY等待对象。tcsetattr TCSANOW/TCSADRAIN/TCSAFLUSH顺序通过TTYCTL服务实现，排空只等已提交输出，不占死控制循环。

2026-11-19用户确认行规程边界：规范行最多4095普通字节，另留一字节给LF；控制字符值0表示禁用，NUL仍可作为普通输入。DEL仅删当前未提交行的最后一个字节并回显BS/空格/BS；^U仅清当前未提交行并回显LF，均不修改已完成待读行。ISIG的VINTR/VQUIT/VSUSP丢弃全部未交付输入及尚未提交给后端的输出，再请求INT/QUIT/TSTP；已交付字节不能撤回。此处输出清空仅涉及tty尚持有的staging/业务输出，不能重置serial活环或写serial拥有的位置；后端已收下的前缀继续按原所有权完成。ECHO关闭不影响编辑/信号语义；溢出只丢本次输入并累计饱和计数，不覆盖旧字节。

后台读按终端/会话权威关系产生TTIN，后台设置前台/规定输出控制按TTOU，忽略/屏蔽时的错误明确EIO/接口规定失败，不任意杀PID。TERM前台信号入口只接受INT/QUIT/TSTP/HUP/WINCH及其授权mask；普通sigsend按相同凭据或固定CONTROL判断，CONT同会话规则保留，不把PID认识当管理权。

### 串口环、公开标志与恢复限制

serial/tty静态共享区包含两个独立SPSC方向环，各头64字节八u64=[magic,capacity,epoch,producer,consumer,flags,reserved,reserved]；magic=0x4b5352494e473031、capacity=8192、epoch从1起。单环映射16 KiB，其中头后8192字节有效数据，其余0；两环32 KiB。producer/consumer为单调u64，0<=producer-consumer<=8192，索引取模容量，release发布/acquire读取；索引溢出前双方在排空状态通过epoch握手重置，不能无声回绕。flags EOF=1、ERROR=2；持久位置/标志决定实际工作，notify只提示。

RX_READY用用户bit32、TX_ROOM用bit33，静态授予对应目标掩码，不允许serial借通知发内核位。环满背压/设备FIFO溢出计数，不能覆盖未消费数据；驱动每批最多32字节/完成项后允许让出，仍有工作不睡等第二次通知。serial退出/授权撤销不是DMA设备停机证明。

2026-11-18用户确认反向唤醒：tty→serial在serial接收端以bit32=RX_ROOM、bit33=TX_READY提示已释放RX空间/已追加TX数据；serial→tty继续以bit32=RX_READY、bit33=TX_ROOM提示输入/输出空间。两边各持只允许这两位的独立NOTIFY，位在各目标命名空间内解释，不把反向提示当作额外授权。写方先release发布位置再notify；收方根据持久环位置决定工作/睡眠，提示可合并或重复，不能靠等待第二次通知推进已知工作。

serial可先完成设备/两环/IRQ初始化并向init报告READY，尚无对端NOTIFY时不发布tty连接。tty自身完成初始化并READY后，init用serial的固定CONTROL INSTALL安装指向该精确tty镜像的NOTIFY，再经只接受精确PID1/image的控制CALL绑定；绑定必须核验实际本地NOTIFY目标/权限，重查持久环状态并补发提示，随后才允许发布tty连接。init不转发字节；旧实例/迟到绑定不能复活旧连接。此方向已确认，控制CALL具体字布局与重复/撤销细则仍须在服务实现前写定，本段不冒充已接生产服务。

NOTIFY的精确bit32/33掩码由可信init创建/授予时保证；现有RESOURCE记录能核验种类、USE/GRANT、目标PID/image和继承位，不暴露NOTIFY掩码，不能声称接收方可仅凭该查询独立证明没有额外通知位。实际notify仍受内核掩码强制限制，此连接方向不隐式扩展查询ABI或通知权限。

例子：serial镜像S已READY但未绑定，RX环有17字节且没有tty连接发布。tty新镜像T已READY，init安装指向T的NOTIFY并发起绑定；若提交的是旧T0的句柄/镜像则拒绝，S的旧状态和环位置不变，init撤销本次安装项。成功则S按实际RX数据及TX空位补提示，init确认绑定成功后才发布依赖连接。若T在绑定中退出，提示失败/退出观察进入本次失败收尾，不凭迟到成功复活T或把旧环直接重定向到下个tty；活环重置仍须双方停止及epoch协议。

#### 首次serial控制连接的实现收口

按上述已确认方向，serial接收端单独使用版本1的控制协议：四字请求=[(version<<32)|opcode,seq,arg0,arg1]，BIND=1、STATUS=2。与init接收端的READY编号分属不同目标协议，不新增syscall。仅接收来自Boot INIT_CALL所指精确PID1/image的CALL；SEND不能管理服务。seq必须非0且单调，不回绕。普通reply=[s64原生业务状态,原seq,value0,value1]，机制状态仍独立于业务状态。

首个UART的IRQ_BIND固定投递设备位16，由init实际bind时选择；驱动只能在消费该事件并完成本批设备处理后ACK，首次初始化完成后ACK一次启用。RESOURCE当前也不暴露绑定位，此启动约定由可信init配置，不通过额外整数制造IRQ权。

BIND参数为init已安装到serial本地表的NOTIFY Handle及预期peer image。serial核对实际RESOURCE种类/USE且无GRANT、继承0及完整目标身份，拒绝自身目标；精确两位掩码仍由init创建时保证。两环必须仍为本epoch且无终结标志。重查环并保守补发RX_READY|TX_ROOM双位提示（可含无工作提示），同时通过内核实际操作检查两位均获授权和对端有效；仅成功后提交绑定并回复目标PID/image。绑定不分配或安装资源；失败仍未绑定，由init撤销其记录的安装项，不按不可信输入随意free其他本地资源。已有绑定不接受新目标或活环重置，返回BUSY；替换/恢复另走停止和epoch协议。

STATUS要求arg0/arg1均0，成功value0/value1分别为UART累计接收/发送字节（不是上层已消费或物理排空证明）。READY结果尚未确认时，未取消的已知新请求返回BUSY，无设备/绑定副作用，业务参数校验在就绪后进行；精确重复仍返回已缓存的原结果。未知版本/操作返回NOTSUP，未授权来源返回DENIED，均不推进控制序号。

每实例仅缓存最近一次已识别、已授权请求及回复，不建无限历史表：较旧seq返回STALE；同seq且四字完全相同返回该回复，不重复查询/通知/提交；同seq内容不同返回STATE。新seq的业务失败也记录结果，修正后须用新seq。协作取消在副作用前观测到时记录CANCELLED；操作已开始则回复实际结果，不能伪称零效果。机制已终结的REQUEST只rqfinish，不执行迟到命令；缓存不是跨新RPC安装资源的许可。

例子：BIND seq1的镜像不匹配，回复STALE并仍未绑定；init撤销该安装项，安装正确NOTIFY，以seq2绑定成功。完全相同的seq2再来只回原结果，不再次通知；随后STATUS seq3后，旧seq2返回STALE，原RPC的内核结果仍可独立查询。peer退出后的新BIND不能复活此旧环，须按恢复协议重建。实现落地/运行结果记录在本篇和09末尾，不以本段文字表示服务已经上线。

共享布局唯一来源为include/proto/serial.h。libsys私有serialinit/read/write提供普通一致SHARED内存上的有界字节操作：单生产者仅写producer，单消费者仅写consumer，以无锁64位acquire/release发布；初始化仅在分享前或双方停止后执行。每次至多32字节，返回实际字节数，满/空返回0，0不等于EOF；旧epoch、损坏几何/位置、重叠调用缓冲及计数溢出分别拒绝，失败不推进位置。EOF/ERROR不清空已有队列，消费者仍可排空，生产者不再追加。识别终结时消费者须先acquire读取终结标志，再读取/排空队列，不用读前的空快照断言没有最终数据。

该辅助层不执行notify、yield或活环重置；批次间让出/重新检查工作及排空后的epoch握手仍由服务状态机负责，不能把计数耗尽返回OVERFLOW当成已实现自动恢复。映射有效性、单写者及运行期epoch/几何不变是两端契约，不是对恶意共享写入的内存安全承诺，也不将一致内存原子操作当作DMA/cache维护。

Termarg位：iflag ICRNL=1；oflag OPOST=1、ONLCR=2；lflag ICANON=1、ECHO=2、ISIG=4；cflag CS8=1；cc索引VINTR=0、VQUIT=1、VERASE=2、VKILL=3、VEOF=4、VTIME=5、VMIN=6、VSUSP=7，其他cc必须0。输入Ctrl字符按当前模式解释，不由serial直接kill前台PID。公开libc flag转换保持这些固定协议数值，未知配置拒绝。

信号帧恢复时ARM64 pstate只接受用户NZCV字段变化，EL/中断屏蔽等由内核重建；FPCR只允许用户修改舍入/FZ/DN位，其他位须与该层可信快照一致并保留原合法状态，不能因库API子集较小而拒绝原本合法的FP控制现场；FPSR仅合法累积状态位。RV64 x0必须0，sstatus非用户字段与该层可信快照一致后由内核构造U态返回，FCSR允许fflags及0..4舍入模式；其他原始控制取值若未被用户改变则按该层可信快照保留，不把handler的临时舍入模式泄漏到外层。PC/SP和映射权限重新校验，不接受特权寄存器原值直写；保持合法非默认舍入模式的测试必须覆盖。

异步安全允许表：read/write/close/dup/dup2、受检查信号配置/掩码/投递、waitpid、_exit、execve和_Fork按预付上下文支持；普通fork只在未使用不安全钩子时可用，malloc/free/stdio/目录流/posix_spawn不承诺handler重入。容量不足返回各接口明确错误，不借外层槽。handler中_Fork后，子不接管父未完成服务操作；外层尚未知且仅父能查询的业务在子固定EINTR、不自动SA_RESTART查询父身份；已完成快照仍可在子交付。这一首版边界明确列为兼容限制，不能重复父读写来补全未知结果。

### tty 后端与作业控制（2026-11-17）

tty 的行规程、回显、模式、前台组与 `WINCH` 必须与"终端从哪来"解耦：serial 与 pty 是同一服务的两种后端，首版即按可插拔后端实现，避免接入 PTY 时重写。

PTY 从端经 `/dev/pts` 打开，故由 T3 提供路径能力后交付；主端只授予创建者或明确继承者。图形窗口与串口终端不得共用同一终端实例。

T4 的 sh 要求作业控制可用，故 STOP/CONT、前台组切换与控制终端语义属于 sh 的前置，不是可延后项；它们与 07 的 ready/gate 前台切换方案共用同一套组与终端代次规则。

## 执行与验收

终端关系实现接续（2026-11-19，可信核心已本地验收）：kernel/terminal.c以64项固定池保存精确owner Image、session/group引用与epoch，每条56字节，使用现有TERMINAL类别身份；新建从epoch1起，首次关联仅安装同会话约束，成功前台提交才递增。每次新操作检查owner处于PREPARING/ACTIVE，结束后仅允许drop清理旧引用，不跟随新镜像。同一会话旧关联仍占清理引用时拒绝另一终端抢占，最后drop后才释放会话/前台组/Image引用。该机制不自行投递HUP/CONT，不把保留引用称为挂断处理已经完成。

7项host以真实group核心和模拟Image边界检查STALE提交无修改、相同组提交推进版本、跨会话拒绝、槽复用/满池、结束后引用释放，以及UINT64_MAX epoch/u32引用计数耗尽、imagehold失败不发布/不改输出和坏身份拒绝。测试直接设置私有边界值，没有新增生产故障开关。CASE=images链接相同terminal.c，真实Life发布/旧Image EXEC结束并detach/新Image激活，验证旧终端立即拒绝、新终端不接管旧关联、两次drop后旧Image实际回收及新终端可关联；64项满池/代次复用、关系引用与Life/Set/物理页回到基线，两架构专项已通过。ARM64整体结构快照引入隐式memcpy已改为逐字段赋值，失败构建日志保留。

此点仅接可信内核核心和CASE=images，未进入normal生产授权路径。Local转授/原生调用、会话控制终端继承、前台作业策略和HUP/CONT挂断/信号仍待接，不能将可信set视为已完成调用者权限检查。完整make test-all通过824项host、格式检查、191次双架构QEMU；164份journal、最终源码/头/SDK/配置/包哈希及两份真实Image终端关系记录另核对，默认init/serial ELF与stage117相同。CASE=images的编译数据库包含新增源。证据build/host/stage120-terminal-relations/，无CI/板端/性能结果。

### TERMINAL Local减权与引用（2026-11-19）

abi/terminal.h现给出已确认权限及信号集合。Local仍为48字节：objectgen/object保存内部终端代次/索引，rights为业务位/GRANT，offset为信号mask、length=0；每份可见Local持一个终端引用，不为授权创建另一个终端或增加Image引用。可信localterminal在取得空槽、验证字段后才hold并发布，失败无表项/引用泄漏；localterminalaccess仅接受单个业务权限，查精确旧终端的owner liveness，ENDED立即STALE，成功返回内部身份与授权mask，仍由Proc核对调用者会话和前台资格。

localcopy检查源GRANT/权限子集和信号mask子集，拒绝非0 length及非PREPARING目标；原生procgrant还重新要求输入为PREP_CONFIG构造权限，不能凭持续CONTROL INSTALL安装终端权，即使它指向尚未start的孩子。资源reply保留原MEM/CALL白名单，不增加终端交付。QUERY/FG产物可以标FORK/EXEC，SIGNAL/BIND任一仍在权集中时禁止继承；这是授权句柄继承，不是控制终端会话关系已经接入。

fork只复制标记FORK的QUERY/FG表项和原Handle/墓碑，使用独立保留引用；未继承的SIGNAL/BIND不带入。旧tty已ENDED时，fork仍可保留旧失效Local，保持其身份/STALE语义，不把父已持有的失效授权当成新授权重新安装。专用terminalretain只供已有引用复制，普通安装/hold仍检查live。RESOURCE枚举通过保留Image返回旧tty身份和mask，不在过期条目处中断整张表枚举；free/revoke/close/fork回滚各归还自己的引用，最后drop才结清原终端持有的Image/关系引用。

例子：init持完整TERMINAL，向准备中的sh减为QUERY|FG并标FORK|EXEC，向tty减为SIGNAL/INT子集且不继承。sh不能由该句柄生成SIGNAL或转授无GRANT产物；tty退出后，两者操作旧Local均STALE，枚举仍显示旧PID/image。sh fork出的孩子继承同一个失效身份，不能复活tty；父子分别free后，最后一份引用释放旧终端和Image。正在运行的进程不能靠CONTROL INSTALL取得新终端授权。

新增5项host以真实Local/Image/Life/Space/fork核心验证准备期减权、信号子集/越权拒绝、运行目标拒绝、QUERY/FG继承和SIGNAL/BIND不继承、旧tty结束后查询/继承但不可使用、坏字段/满槽不泄漏；与终端/fork既有专项共22项通过。两架构CASE=images新增实际Proc准备表转授/权限拒绝、构造撤销后的旧Image保留/STALE/枚举、最后两表项free使Image实际回收及两Proc费用归零。完整make test-all通过829项host、格式检查、191次双架构QEMU；164份journal、最终源码/头/SDK/配置/包哈希与新权限头精确导出另核对，默认init/serial ELF与stage117相同，CASE=images编译数据库含新增核心/测试。证据build/host/stage121-terminal-local/；尚未发放初次原生终端权、开放终端syscall、接控制终端继承或真实挂断信号；当前Proc grant的准备期限制未以已发权原生用户场景验收，不将可信表项测试冒充它。无CI/板端/性能结果。

### 原生ttycreate首次发权（2026-11-19）

SYS_TTYCREATE=0x6e与libsys私有桩已接。初始PID1/image1的SYSTEM新增POLICY=0x10，普通镜像仍只有QUERY；localsystemaccess对POLICY与CONSOLE均重新核对精确初始身份，不凭uid0或伪造低位获得权。Proc先检查POLICY和固定CONTROL CONNECT、目标Life/Run/资源表，再检查调用者空槽；terminalnew只接目标当时PREPARING/ACTIVE的精确Image。localterminal安装完整业务/GRANT/信号全集、继承0，成功后归还临时创建引用；安装失败同样drop，系统调用只在全部成功后发布本地Handle与公开ID。没有开放其他POLICY业务的成功占位。

两架构service-authority使用真实用户ELF和原生create/ttycreate/grant/query/free/destroy：错误SYSTEM/owner类型、未用参数及失败寄存器清零；填满父资源表后128次创建失败，再成功取得64项终端并拒绝第65项；释放后准备期QUERY|FG/FORK|EXEC和SIGNAL/INT子集转授成功，KILL扩权及CONTROL INSTALL均拒绝。实际子用户用同步CALL固定ACTIVE镜像，父在其存活时ttycreate，wait后仍查到旧PID/image和完整首授权；另验取消owner后向新准备表grant返回STALE，以及旧CONTROL释放后不能再用。CONTROL各业务位拒绝由既有host穷举另验，不把持完整CONTROL的原生成功路径称为原生逐位拒绝测试；exec固定身份仍用stage120真实Image核心证据，不称原生exec已接。

完整make test-all通过829项host、格式检查及191次双架构QEMU；13项SYSTEM/CONTROL/Local专项、164份journal、最终源码/头/SDK/包哈希和两份实际ttycreate用户证明另核对，保留编译数据库。默认init/serial全部可装载段的内容、权限、地址和大小与stage117一致；新增桩导致编译器局部标签重编号，ELF差异仅.symtab/.strtab，已从stage121保留的实际启动包取旧ELF逐段/逐节核验，而非忽略哈希差异。证据build/host/stage122-ttycreate/；无CI/板端/性能结果。首次专项曾错误重复ACK同步CALL已消费的结果，已按现有IPC契约修正fixture，失败日志保留。默认清单没有tty，ttybind/getfg/setfg/ttysignal、控制终端继承、HUP/CONT和生产tty仍待接。

### 原生终端绑定与查询（2026-11-19）

ttybind=0x69/getfg=0x6a及libsys私有桩已接。Proc以TERMINAL业务权解析实际表项；BIND将公开SID解析到固定内部会话并要求members>0，再使用既有首次关联/唯一占用事务，不添加前台或改变epoch。QUERY快照保留的终端关系，将内部SESSION/GROUP转换成公开完整PROCESS身份，全部成功才同时发布PGID/SID/epoch；缺权限、坏类型、旧owner均无部分输出。两种操作仍为IRQ屏蔽的有界短事务，不在借用关系记录时调度。

实际用户专项给孩子准备表授QUERY/FORK|EXEC，不授BIND；孩子先读未绑定0/0/1，再真实setsid形成不同于init的会话并用同步CALL固定存活窗口。init凭BIND跨会话绑定后读0/孩子SID/1；孩子在回复后读到同一关联但BIND被拒绝。重复绑定返回STATE、另一终端抢会话BUSY，后者保持0/0/1；owner退出后父持有的两终端getfg均STALE且三输出0。另验0/非TERMINAL、未知会话与未用寄存器拒绝，失败不改epoch。测试额外QUERY占用真实Local，fixture资源槽由8增至16，不改变生产CREATE规格或增加默认tty服务。

两架构CASE=images的真实Proc/Local/Life/Group表另验members=0的既有会话拒绝原生绑定；随后用可信关系核心安排非零前台，检查Proc查询返回真实公开PGID/SID，SIGNAL-only无QUERY被拒，owner结束后清零输出，并随最后表项释放全部关系/Image/费用。非零前台准备使用可信核心，不将此用例称为原生setfg、TTOU或完整作业控制。

完整make test-all通过830项host、格式检查及191次双架构QEMU；26项串口/SYSTEM/CONTROL/Local专项、164份journal、最终源码/头/SDK/包哈希和两份实际跨会话绑定/QUERY-only客户证明另核对，保留两架构及共享编译数据库。默认init/serial可装载段的内容/权限/地址/大小仍与stage117相同，变化仅在局部标签符号元数据。首次原生测试将非TERMINAL句柄误期望为DENIED，已按既有类型化Local规则改为BAD_HANDLE；首轮全量另暴露PANIC半行观测竞态，已补碎片回归并重跑完整全量，原失败保留，细节见14末尾。证据build/host/stage123-terminal-bind/；无CI/板端/性能结果。尚无原生setfg/ttysignal、TTOU/STOP/CONT、控制终端继承/挂断或生产tty，不将只读getfg称为完整前台切换。

- [ ] 两架构信号PC先进入trampoline，六个入口参数与原TF分开，a3/x3的handler地址不会被误读为原RPC；普通返回地址0不得跳回任意旧代码。
- [ ] 子发布后第一条用户指令前及Fix半途注入HANDLER/NODEFER/STOP/KILL：应用handler只见修复后的子表，父staging内容不变；嵌套只完成同次Fix，不增加发布后ready syscall或远端握手。
- [ ] sigaction/RESETHAND与前置取消、NODEFER嵌套交错仅使用本次动作快照；一/三参数handler及errno恢复正确，不维护第二份可竞态更新的handler表。

- [ ] 信号trampoline保存原RPC后仅提出协作取消即进入handler，不等服务；handler重入/_Fork不接管或释放父/外层原Rq，RESTART只在明确零效果且旧请求结束后新提交。
- [ ] 零进展、部分量、完整结果及服务失效四类均从原Rq裁定；原call中断返回码不被当作业务EINTR证据，资源/数据在保存前不ack/复用。

- [ ] handler内exec成功丢弃旧帧/入口但保留应用有效掩码，失败旧handler可继续；pending不丢失也不重复生成已递送信号。
- [ ] HANDLER重置/IGNORE保留/附加标志重置与地址切换一致，新镜像首次进入检查pending，STOP/KILL不被解除exec屏障绕过。

- [ ] DEFAULT 与显式 IGNORE、NOCLDWAIT 分别验证 wait 结果保留，NOCLDSTOP 仅影响约定通知而不抹掉真实组状态。
- [ ] 按历史结果审计候选覆盖 B先退出/改IGNORE/C后退出/改HANDLER；pending 清除不消费B，C自动收取不复活，策略改变/exec/孤儿转交不夺走已交付结果；通知与Life/Acct/DMA清理保持独立。
- [ ] 退出与父策略变更/wait分配交错使用正确版本，不丢已交付结果；自动收取不跳过独立资源清理。

- [ ] 各标志及组合有明确支持状态，未知位拒绝；一次性动作在递送提交点重置，NODEFER 不移除显式屏蔽。
- [ ] SA_SIGINFO 原因/来源/故障地址正确且未用字段清零，两架构 context 校验与恢复一致，不泄漏特权信息。

- [ ] 默认掩码、sa_mask、SA_NODEFER 和多层返回正确组合，未屏蔽其他信号可嵌套，显式当前信号屏蔽不被 NODEFER 清除。
- [ ] 最大深度普通信号保留 pending，同步故障受控终止，KILL/STOP/默认终止不依赖新增上下文；逐层资源不串用。

- [ ] 两架构逐类用户异常核对信号/诊断，合法 handler 可处理，坏入口/栈及屏蔽同步故障受控终止，无内核异常。
- [ ] __sys_copy 地址错误不递送用户故障信号，内核异常不冒充用户异常；恢复 PC 不被隐式推进，处理继续不生成退出记录。

- [ ] HANDLER 双入口校验/递送前重查，DEFAULT/IGNORE 非零入口拒绝，自定义trampoline可用；非法帧只受控终止一次，不递归投递或执行半恢复现场。

- [ ] 普通/tty/init 信号权限独立，sig=0 不投递，负选择器及陈旧组身份正确拒绝；SIGKILL 不越权。
- [ ] Ctrl-C 与前台切换/组长退出交错投向正确关联，不依赖 tty 缓存；投递成功不提前声称目标退出。

- [ ] 信号在结果检查前后、临时掩码安装及等待登记边界到达均不丢唤醒，处理函数返回恢复原掩码并 EINTR。
- [ ] 按上述方向核对 M/W/H、SA_NODEFER、handler 临时改掩码及嵌套 sigsuspend；正常 sigreturn 恢复入口 M 后才继续应用，坏输入不改掩码，纯 STOP/CONT 不假结束，捕获 CONT 与普通信号同样裁决。
- [ ] 忽略信号/普通事件不假完成，STOP/CONT 保留等待；重复 SIGCHLD 合并后仍能取得全部真实子进程结果。

- [ ] 掩码更新/pending 到达/用户返回各交错正确递送，KILL/STOP 不可屏蔽，重复普通信号不伪报排队次数。
- [ ] 嵌套信号各层掩码保存恢复正确，只查询不修改状态，坏参数/输出不导致半更新；掩码变化不释放旧请求槽。

- [ ] sigreturn 覆盖错误地址/大小/代次、非顶层/重复帧、内核 PC/SP 和特权位，拒绝后无半恢复或内核异常。
- [ ] 通用/FP状态合法恢复，KILL/STOP 不可屏蔽；用户帧不能恢复内核请求生命周期或导致旧回复写入新上下文。

- [ ] sigaction 无效信号/标志/入口、KILL/STOP 和坏输出路径均遵守失败不部分安装；递送时再检查映射，不因旧成功注册绕过权限。
- [ ] 公共特殊动作值转换为明确原生类型，IGNORE/掩码变化不提前释放 IPC 收尾引用。

- [ ] 在交付前、交付后、结果提交前后注入信号，覆盖 read/write、管道、终端和等待；检查偏移/数据只消费一次，部分完成量不丢失。
- [ ] read/write 在零进展、部分进展、已固定结果及进展未知四阶段注入信号；handler 关闭/复用原 fd，外层不读写替代文件，read 快照不丢失，查询故障不伪报零进展。
- [ ] 处理函数执行后按接口验证 SA_RESTART 或 EINTR；QUEUED取消已终结时不复活，ACCEPTED协作取消仍保留原结果，旧共享槽未收尾时不复用，服务崩溃不会无限重试。

全部为待执行项；通用测试纪律见 [总览](00-overview.md#执行与验收)，实际结果记录在 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 阶段 13：IRQ、serial、tty 与控制终端

依赖：12。产物：PLIC/GIC 接入、独立 serial 与 tty、共享收发协议、会话/进程组/前台状态和信号处理。

测试方法：两架构注入串口输入，检查 IRQ 认领/屏蔽/确认，不以轮询回退冒充 IRQ 成功。
核对只有 serial 持有 UART MMIO/IRQ，tty 未获设备权限；测试双方任一退出的取消与共享区回收。
覆盖无输入时持续输出、serial RX/TX 缓冲满、通知合并/重开、行输入、回显、退格、并发输出、等待中关闭终端；sh 子进程继承控制终端，服务无控制终端。
测试标准输出重定向后控制终端不变；前台忙循环可由约定的控制操作终止，不影响其他进程。

验收：tty 不阻塞在长期 serial call 上，输入输出均有进展；无空闲中断风暴；控制终端标识不绕过资源授权；旧终端身份失效。
使用单串口，验证本轮定义的会话/组/信号语义，不以仅杀单个前台 PID 通过验收；不宣称完整 POSIX 兼容。


### 信号预付与帧身份接续（2026-11-16）

已接可信sigreserve/sigrelease及每运行槽固定2份有类型Sigctx和动作表；记录含完整HAL用户整数/FP现场、动作快照、Rq/等待身份及三个掩码责任，当前RV64每份704字节、ARM64每份960字节，均16字节对齐且低于1024预算。不是用户Sigframe布局，不能把内核记录直接复制给用户。预付存储随编译容量驻留BSS，不临时在信号/低内存路径申请页面或引入通用对象池。

每份预付绑定完整PID、镜像号与Imageid，持独立Image cleanup引用；同运行槽其他PID/镜像不能借用。ACTIVE镜像禁止提前释放，构造失败或ENDED镜像的CPU离栈清理才解除；当前没有激活帧或保留Rq责任，出现这类责任时拒绝释放，未来递送实现必须先明确退栈/转交，不能沿用空帧假设丢弃责任。原生sigaction/递送/sigreturn、STOP/CONT尚未接。

- [x] 5项host在两种上下文布局检查固定2层、重复预付不多持引用、ACTIVE不可释放、完整镜像隔离、全运行槽容量、cleanup引用耗尽的原子失败，以及退出结果不继续占预付存储。
- [x] 两架构sigreserve在真实Proc构造中注入早期费用不足和晚期资源槽不足，均回滚Sigctx/Image/Rq/Life/页/费用。实际U/EL0 exit42后仅到CPU清理phase2才解除预付，随后仍持观察的旧Image和新PID/同运行槽互不误选。
- [x] 完整202项host、格式及80个QEMU场景通过，日志`build/host/stage27-signal-reserve/test-all.log`。当前normal仍只初始化停机，不模拟init或处理函数，不称信号递送或sigreturn通过。

帧身份问题已由用户于2026-11-16确认：offset48的一个reserved改为单调cookie，保持64字节前缀及TF/FP偏移；内核预付槽跨PID/image复用时不重置，sigreturn核验最上层地址/depth/cookie及完整主体。共享Sighead64布局已落在abi/signal.h。cookie仅区分未更新的旧帧副本，不替代对可修改寄存器、特权位和mask的独立校验；调用者合法改写自己的恢复现场不被误称为跨主体授权。


### 单调帧cookie分配（2026-11-16用户确认）

已接可信sigcookie，仅对实际ACTIVE且仍绑定预付槽的完整Imageid生成不为0的cookie；PREPARING/ENDED、旧镜像/陈旧身份均不消耗序列。预付释放和再次绑定只清除现场/动作/归属，保留每槽序列。私有实现以58位序列和6位运行槽号组合，跨槽也不重号，达到上限OVERFLOW且输出0、不回绕；cookie是不透明恢复标签，不是Handle或任何管理授权。

Sigctx把三个有界小状态缩为u8以容纳cookie，预付槽序列利用原有对齐空间；RV64仍704、ARM64仍960字节，无新运行期分配。4项新增host验证镜像替换、完整PID/同槽复用、跨槽唯一及把上限缩到2后的耗尽不重置；两架构sigreserve在实际Proc/用户exit生命周期中验证同槽新PID继续递增、旧Image不能消耗新序列。

这只验证共享前缀布局及内核分配/生命周期机制。实际用户帧写入、最上层校验、合法TF/FP/mask恢复和sigreturn尚未实现，不能将分配器测试当成处理函数/反例递送验收。

本次分配器变更另通过完整206项host/格式和两架构sigreserve，日志`build/host/stage28-signal-cookie/`；最近整轮QEMU为stage27的80场景，不把本轮两场景称作重新执行全套。

### 默认/忽略动作、掩码与SIGCHLD收取策略接续（2026-11-17）

已接原生0x60 sigaction、0x61 sigmask、0x62 sigpending与libsys私有桩。当前明确为配置profile：只安装DEFAULT/IGNORE、CHLD上的NOCLDWAIT；HANDLER及RESTART/NODEFER/RESETHAND/SIGINFO/NOCLDSTOP等尚未有完整递送语义的已知标志返回NOTSUP，未知位/编号INVALID，KILL/STOP不可安装。没有假装注册成功或调用半实现处理函数。DEFAULT/IGNORE入口字段必须0，保留字段及动作掩码未知位拒绝。

动作输入一次拷入内核快照；合法旧动作输出在安装/策略提交之前整区预检并写出，输出FAULT则旧动作/子收取策略不变。new/old地址相同时仍使用原输入快照，不能被输出的旧动作覆盖。原生sigmask支持BLOCK=0/UNBLOCK=1/SET=2，保留完整旧掩码寄存器输出；未知位/方式拒绝，KILL/STOP从有效mask与action.mask剔除，BLOCK(mask=0)可作无副作用查询。sigpending读取真实bank.pending，当前未接信号发送/硬件到信号的生产与递送，正常profile为空，不能把返回0称为递送验收。

每次创建在完整Sigctx预付后从真实父Image捕获信号基线：只保留显式IGNORE种类，不复制handler/trampoline地址或动作flags/mask，pending清空，应用mask取当时可信父bank。初始init独立默认。不可变IGNORE位集使用原Signal.reserved的u32，不增加预付银行大小；configure每次从该基线重建默认/忽略表，再应用sigdefault与显式sigmask，不能累计丢失上一次被重置的IGNORE，也不因父后续改策略而改变已接受准备的基线。捕获动作转DEFAULT的通用分支尚待真正允许HANDLER后再作端到端验证。

configure先校验全部地址/Boot/掩码及Signal/Life准备状态，再原子替换资源保底并应用初始动作/掩码；输入或容量失败不先改信号bank。libsys的Elfwork新增私有sigmask快照，由创建调用者在接受创建前取得，elfload只传到Config，不在装载结束再读取可能变化的父mask；现有adopt/elf测试加载器已接。初始掩码不是内核运行库内部的临时阻塞位，当前尚无handler/Rctx临时掩码，未来递送需保持其既定区分。

Life仍128字节：原尾部padding容纳当前父child_policy及本实例exit_policy快照。显式CHLD IGNORE记为AUTOCHILD|IGNORECHLD，DEFAULT+NOCLDWAIT记AUTOCHILD，普通DEFAULT记0；孩子退出在同一提交点按完整合法父快照这份决定。AUTO仅解除父waitref/实例额度及对应退出组保留，Life/Acct/Image/Rq/观察和真实页仍按独立引用保留；同时CHILD提示让阻塞wait复查，若仍有匹配活孩子不能提前ECHILD。CHILD事件是关系提示，不冒充已递送的SIGCHLD。后续动作变更不重评旧exit_policy，父死亡后的活孤儿在自身后来退出时使用新合法父的策略；已经保留的旧结果转交也不重新裁决。

本profile只完成结果保留/自动收取决策，尚未生成真实SIGCHLD pending或调用处理函数。NOCLDWAIT且非IGNORE的通知生成、IGNORE抑制通知、NOCLDSTOP和STOP/CONT的通知路径须后续与递送一起完成，不能以自动收取成功声称通知已齐备。sigreturn/帧TF/FP权限验证、信号发送授权、sigsuspend及库errno/中断重启仍未实现。

新增CASE=sigconfig，两架构真实父程序以raw原生接口检查掩码/KILL-STOP清除、非法方式/编号/标志、只读旧动作输出失败保留策略、new/old别名快照及不支持HANDLER显式拒绝。随后真正装载6个csu/main孩子：2个保留结果退出31，4个按IGNORE/NOCLDWAIT自动收取；旧结果在安装IGNORE后仍能领取，新自动收取孩子尚活时WNOHANG=0，阻塞wait在退出后醒来返回ECHILD。孩子自己查询验证IGNORE/掩码继承、NOCLDWAIT flags不随普通创建复制、sigdefault重置及再次configure恢复原基线，失败配置不产生畸形mask。

host新增3项Life策略测试和3项Signal配置测试，覆盖退出提交后不追溯、自动收取后独立观察/清理引用、旧父AUTO不控制未来孤儿退出、掩码/动作profile与不可变基线。可信CPU还核对每份真实child_policy/exit_policy，结束后所有孩子Life、动态页、Set/Acct/组及信号预付归还。本地完整274项host、格式及94个两架构QEMU通过；退出策略快照最终补强后再跑完整274项host、格式及两架构sigconfig专项，日志build/host/stage43-signal-config/。normal未启动生产init，net及完整系统验收仍在目标范围，未运行CI/真实硬件/性能验证。

### 信号上下文HAL转换/恢复契约（2026-11-17）

已加入hal_signal_enter和hal_signal_restore，将目标架构寄存器规则留在kernel/riscv/context.c与kernel/arm64/context.c。它们只转换内核持有的Hctx数据，不直接读用户指针、不操作请求或信号栈层级，不写硬件CSR；真正返回用户仍沿现有完整现场恢复/eret或sret。调用者另须验证完整帧普通可写映射、PC执行/SP写权限、最上层地址/深度/cookie及完整PID/image归属，不能将HAL校验当作sigreturn已经安全完成。

入口转换核对用户地址窗口、PC指令对齐、frame16字节对齐和整个固定前缀/Hctx范围不溢出，再设置PC=trampoline、SP=frame、ra/x30=0及六个冻结参数[signum,frame+64,frame+128,handler,frame,flags]；其余GPR/FP/诊断字段保持原值。参数flags的语义由上层动作快照验证，HAL不擅自决定重启/挂起或处理函数动作。

恢复在任何写输出之前校验全部架构状态。两架构只接受候选PC/SP在既定用户窗口且指令/栈对齐；cause/address是该层诊断快照，不允许用户改写。RV64要求x0=0，sstatus保持可信快照且原快照为U态/内核SIE关闭，再由内核清除SPP/SIE重建用户返回；用户不能通过帧屏蔽时间片或返回S态。ARM64只允许PSTATE.NZCV变化，EL/执行态/DAIF等必须与可信EL0t快照一致，再重建EL0返回。

FP数据寄存器按合法用户编辑恢复。RV64允许fflags和0..4的舍入模式；其他原始frm值仅在等于该层可信原值时保留，不能切换到另一保留模式，其他FCSR位不许变化。ARM64允许FPCR舍入/FZ/DN变化，其他原有合法位须保持；FPSR只允许累计状态字段变化，其他原始字段保持该层可信值，不能因为当前库接口较小而清除合法原环境。两者FP.reserved必须0。拒绝不改目的Hctx；可接受完整out与任一输入精确重合，不支持任意部分重叠。Hctx与Sigframe总长/FP偏移已有编译断言绑定（RV688、ARM944）。

5项新host测试覆盖六参数映射、非法地址/帧入口拒绝无半更新、可编辑GPR/FP/条件码、精确缓冲别名、特权/IRQ/诊断字段篡改、保留FP控制和新保留值拒绝。CASE=sigreserve两架构另执行两次真实用户入口：可信CPU用HAL恢复后的现场进入U/EL0，用户汇编实际检查GPR、FP寄存器、非默认控制及ARM NZCV；RV另外检查原样保留frm7的FCSR值，不把它称为新增合法舍入模式。每次还拒绝8种架构非法恢复且输出全字节保持0xa5，另检查坏frame入口不改原状态；原cookie/预付/回滚/离栈回收断言继续执行。

首次试验中C/汇编同主名输出对象冲突已通过拆分测试文件名修正；后续用户载荷未清零exit未用参数导致首个退出被INVALID拒绝，已按既有ABI清零额外比对寄存器，未放宽内核参数/恢复验证。失败日志保留。本地完整279项host、格式及94个两架构QEMU通过，最终架构实现/用户载荷源码哈希与已验镜像匹配，日志build/host/stage44-signal-context/；原生HANDLER仍NOTSUP，实际用户帧写入、映射/最上层/cookie校验、sigreturn、信号生产/递送/中断重启及libc适配仍待执行，无真实硬件/性能/CI验收。

### 用户信号帧与原生sigreturn接续（2026-11-17）

已接单一动作表上的可信类型化安装/排队、完整用户Sigframe及原生0x65 sigreturn。排队只更新pending与首份Siginfo，实际递送在首次用户入口、timer返回或syscall完成寄存器输出后的统一出口执行；不在yield挂起的C续点上提前改写入口参数。当前可信入口只接受READY/RUNNING目标，拒绝KILL/STOP/CONT及作业控制停止信号，不提供未定义的原生来源code。

每种pending信号保留首份64字节Siginfo，同编号后续信号合并；IGNORE清除对应pending/元数据。递送取得动作快照，重新核对handler/trampoline普通RX映射，将前缀、Siginfo及完整原Hctx写入当前SP下方16字节对齐的可写普通用户栈，再提交预付层、cookie及有效掩码。达到两层时继续保留可捕获pending。NODEFER只取消自动追加当前信号，显式action.mask仍生效；RESETHAND在进入用户前将主动作复位，活动层继续使用原快照。处理函数内IPC按实际depth选择信号一/二预算，CALL_CONTROL仍单独选择控制预算。

sigreturn只接受最上层精确地址、深度、previous和cookie，绑定原PID/image银行；检查整个帧及reserved/mask、恢复PC普通RX/SP普通RW映射，再由HAL对照内核原Hctx校验GPR/FP/权限。成功直接恢复完整现场并复查pending，不经过普通syscall结果覆盖/PC推进。失败以私有BAD_SIGNAL_FRAME诊断不可恢复退出SIGSEGV；退出先结束镜像并清空活动层及pending，CPU离栈后再释放预付银行，cookie序列不重置。当前帧不持Rq临时责任，遇到非空责任拒绝清理，未实现的中断/重启不能借此丢弃Rq。

CASE=sigframe使用真实csu/main镜像和自定义trampoline验证10种路径：单层恢复及返回mask中的KILL/STOP清除；嵌套USR2；篡改特权；内层归还外层；旧帧副本重放；未映射PC；无活动帧返回；handler内exit；RESETHAND后第二信号默认终止；NODEFER同信号嵌套。实际FP舍入模式在应用/外层/内层各不相同，返回逐层恢复；核对六参数、Siginfo、动作快照、掩码及跨槽复用cookie。结果应为4个正常退出42、5个坏帧SIGSEGV、1个默认USR1终止，每例结清动态页/Set/Acct/关系/信号层。host补测pending合并、IGNORE清除/保留mask及非法/特殊来源/终态拒绝。

首次运行发现信号核心把函数/栈字节地址直接交给要求页对齐的页表查询，导致入口重验误拒绝；现按页对齐查询，未放宽映射权限。失败记录和最终验证保存在build/host/stage45-signal-frame/。

边界：原生HANDLER安装仍NOTSUP；原生sigsend、真实CHLD及同步异常信号生产、STOP/CONT、阻塞syscall中断、Rctx/重启、sigsuspend、libc信号/errno前置尚未接。可信fixture不等于用户态已能注册和发送信号，normal仍未启动生产init，完整目标包括net。

本地最终验证：完整make test-all的281项host、格式及96个RV64/ARM64 QEMU全部通过；补充3项帧核心host后，完整host重跑284项通过，格式再次通过。日志、96个原始产物目录及关键源码哈希在build/host/stage45-signal-frame/。无CI、真实硬件或性能验收。

#### Siginfo来源编码（2026-11-18已确认）

用户已确认原生code为USER=1、CHILD=2、FAULT=3：USER携发送者完整PID/euid；CHILD携孩子PID/ruid及已冻结退出/停止/继续原因与状态；FAULT携故障地址及归一化异常原因。各来源无关字段清零，libc再转换标准si_code，不直接沿用Linux值。64字节布局不变；CHILD真实退出生产已于2026-11-19接入，USER/FAULT及停止/继续仍待接，可信测试code=0不定义生产协议。后续历史记录的“来源编码待确认”已由本次决定取代。

2026-11-19接续先接CHILD真实退出生产：共享头统一SOURCE_USER/CHILD/FAULT常量；CHILD的reason/status采用原生wait kind/code，当前退出为WAIT_EXITED及exit code，信号终止另置WAIT_SIGNALLED，故障另置WAIT_FAULT。不是libc wait编码或Linux si_code。STOP/CONT尚未产生状态，不能假装本次已经支持。USER/FAULT的真实来源及归一化异常映射另接，不因有常量就开放sigsend或同步故障处理。

### 原生sigsuspend与M/W/H交接接续（2026-11-17）

已接原生0x63和私有__sys_sigsuspend。输入先验证，保存当前M、安装清除KILL/STOP的W、检查pending并登记信号等待在现有单核IRQ屏蔽边界原子完成；没有unmask后才另睡眠的窗口。M先位于线程预付内核栈的原syscall续点，捕获成立时转入下一Sigctx，未新增通用等待对象。唤醒仅入FIFO并回原C续点复查；原调用先固定-INTERRUPTED的完整用户返回现场再退栈，统一出口以W和动作快照构造H，用户前缀restore_mask取M。正常sigreturn恢复M，handler中临时sigmask不覆盖它；显式合法帧mask编辑仍生效。

被屏蔽/忽略信号和普通IPC事件不结束等待。两层满额时捕获信号留pending，最内层sigsuspend继续等待，不能靠第三层/隐式放宽容量解套；默认终止无需新层。修正选择器：满层的低编号捕获pending不再挡住较高编号默认终止，保留捕获位并选择可执行的默认动作。当前没有真实STOP/CONT生产或状态机，这两种路径仍未验证，不以本次等待承诺代替它们。

CASE=sigsuspend两架构各9例：M屏蔽的预存USR1在原子换W时立即捕获；真正BLOCKED后忽略WINCH/屏蔽USR2均不唤醒，预存自NOTIFY事件也不结束等待，随后USR1使其醒来，恢复M后立即递送先前USR2且普通事件仍可领取；handler内嵌套sigsuspend逐层恢复M/W/H；非法mask不变更；默认TERM；阻塞及已醒READY续点固定CONTROL强杀；两层已满时捕获USR1继续pending而TERM仍可结束；显式改返回mask。应用/外层/内层实际FP舍入环境逐层验证，正常5例仅EINTR一次、终止4例不回应用，所有动态页、预算、关系及信号银行结清。

host另测等待恢复M与H分离、handler改mask/嵌套返回，以及满层低编号捕获不挡默认终止。早期ARM测试误在发布后重新借构造表，proctable依设计拒绝，已修正可信fixture的构造时表引用，未放宽生产授权；RV首次命令的固件路径笔误未进入guest，已用正确路径重跑。失败产物保留在原运行目录。

边界：原生HANDLER仍NOTSUP，来源编码仍待确认；可信安装/排队仅额外接受独占信号等待，不提前支持普通IPC/父wait中断。SA_RESTART、Rctx、libc信号前置、STOP/CONT和完整信号生产仍待接，生产normal及包括net的全项目目标未完成。

本地最终验证：完整make test-all通过286项host、格式及98个RV64/ARM64 QEMU，最终源的帧核心5项host及格式也单独通过。9例信号等待各架构5个恢复EINTR、4个终止，charged=0且物理页回到内核表基线；日志、原始运行目录及关键源码哈希在build/host/stage46-signal-suspend/。未运行CI/真实硬件/性能验收，源布局/预算静态断言保持通过。

### 原生 HANDLER 与 libc 掩码接口（2026-11-17）

原生sigaction现允许HANDLER，复用既有两层用户帧/restore核心。注册时在输入快照之后、旧动作输出及提交之前，检查handler和trampoline均为当前空间合法对齐的普通用户RX映射；任一失败返回INVALID或FAULT并保留旧动作。输入/输出同址仍用输入快照安装新动作、向原对象返回旧动作。递送再次检查两入口，注册不固定代码页；解除映射后收到信号按BAD_SIGNAL_FRAME受控结束，不跳入空洞或触发内核异常。只操作当前Image，不新增调用号、权限或结构字段。

原生与可信安装共用同一动作校验/存储，去掉两套HANDLER门禁；NODEFER/RESETHAND/SIGINFO及CHLD NOCLDWAIT复用已实现语义，已知未完成的RESTART/NOCLDSTOP继续NOTSUP。DEFAULT/IGNORE的入口仍须0，KILL/STOP仍禁止修改，未知标志/掩码拒绝。创建基线只继承显式IGNORE及当时mask，不复制HANDLER地址或附加标志；host另明确验证Native设置的HANDLER在孩子基线中为全零DEFAULT。

libc signal.h现提供64位sigset_t、支持的SIG_*及SIG_BLOCK/UNBLOCK/SETMASK常量，和sigemptyset/sigfillset/sigaddset/sigdelset/sigismember/sigprocmask/sigpending/sigsuspend。集合助手操作有效C对象，未知/未支持编号EINVAL且不改对象；fillset包含所有本系统支持位，包括不能真正屏蔽的KILL/STOP。mask/等待接口用__sys_copy快照/交付8字节普通内存，不需要malloc、stdio、fd、RPC槽或惰性连接。

sigprocmask(set=NULL)忽略how，以原生BLOCK零掩码查询；非空set才验证how并提交变更。old输出坏指针发生在原生mask已提交之后，返回EFAULT，不回滚并覆盖可能由handler更新的状态。输入复制失败或未知mask/方式不提交；输入/输出同址返回旧mask且内核使用已快照的新值。sigpending非消费；sigsuspend使用原生M/W/H等待，捕获处理后返回-1/EINTR，无正常成功返回。参考[Linux man-pages sigprocmask说明](https://man7.org/linux/man-pages/man2/sigprocmask.2.html)核对空set查询规则，公开mask宽度/编号继续使用本项目冻结值，不导入Linux结构布局。Open Group在线正文仍未取得，不声称完整标准合规。

sigframe的11个真实用户进程现在自行调用原生sigaction注册两入口，CPU只负责可信排队。新增未映射/只读/高地址/未对齐入口、保留位/未知标志、只读old输出和同址快照检查；单独创建可执行Code页，成功注册后解除映射，再排USR1验证受控失败。原完整GPR/FP、两层嵌套、NODEFER、RESETHAND、cookie重放/非顶层/坏帧验证继续运行。定制测试trampoline自行保存/恢复每层errno，不把它当作已交付的POSIX trampoline。

sigsuspend的9个进程也改为用户原生注册，实际等待改走libc；覆盖已pending、先检查后阻塞、IGNORE/被屏蔽信号、嵌套/深度满、M/W/H恢复与强杀，仍由可信生产者注入code=0。8项libc host检查本地集合、无堆依赖、how忽略、同址/坏指针、输出失败已提交、pending非消费及errno映射；Native配置host检查HANDLER快照/失败保持及孩子DEFAULT基线。

本轮不实现候选Siginfo来源code，也不接USER/CHLD/FAULT或sigsend生产；普通IPC/父wait阻塞中断、STOP/CONT、SA_RESTART、Rctx/Fix及公开POSIX sigaction trampoline仍待接。signal.h只声明已交付的集合/mask/pending/suspend子集，没有注册一个绕过前置责任的公开sigaction。原生自定义trampoline仍由调用者承担运行库适配责任，normal仍未启动生产init，包括net的完整目标未完成。

本地make unit全部406项、补充配置6项、make fmt-check及make test-qemu全部108个两架构场景通过；证据build/host/stage59-native-signal/，核对所有原始结果中的当前proc/signal源码哈希及两架构SDK，普通镜像不含测试。尚无CI、真实硬件、性能或完整POSIX信号验收。

### COW 栈上的内核信号帧输出（2026-11-17）

sigdeliver继续通过统一spacewrite写完整Sighead/info/TF/FP，现会先准备逻辑可写COW栈页并更新全部别名，再写用户帧；不通过直接物理写绕过分离。动作、cookie、mask及TF/FP布局不变，坏/真正只读栈输出仍受控结束。该变化不新增Siginfo来源编码或POSIX trampoline/Rctx接口。

sigframe的mode0在Proc已经发布但尚未调度时，可信fixture把其64KiB栈克隆到独立裸Acct影子视图（额度恰好16页）；不放宽普通configure/publish的硬件RW校验，也不把这个构造步骤称为原生fork。用户frame_wait汇编临时把SP置于已独占顶页底部64字节处yield；CPU确认下方页仍R/COW，再注入已注册USR1。内核写688/944字节信号帧必须跨入该未分离页，随后真正handler和sigreturn恢复完整整数/FP/mask及原调用栈。

结束时源下方页实际为RW，影子栈全部16页仍为零（无应用栈内容/寄存器/信号帧泄漏），源进程清理及影子释放后物理/Acct/预留归零；两架构目标场景均验证。新增helper保存本来的callee-saved寄存器和SP再恢复，固定临时地址只在明确Boot栈布局的测试载荷中，不是运行库地址分配契约。该场景证明信号帧的kernel COW写准备，完整fork/_Fork活动层复制、Rctx、来源生产/编码及STOP/CONT仍待接。

COW栈输出最终本地验收：473项host/格式及110个两架构QEMU通过，精确运行和trace见build/host/stage69-kernel-cow/；信号帧下方新私有页没有用户store故障，影子16页全零并正常sigreturn。没有将可信栈冻结当原生fork/_Fork，也未执行CI/真实硬件/性能或完整信号Rctx验收。

### fork 信号动作与活动帧继承（2026-11-17）

新增可信内部sigfork(source_image, source_space, target_image, target_space)。源须为LIVE父实例的ACTIVE镜像，目标须为该父的CONSTRUCT孩子/PREPARING镜像及已预付的全新信号银行；Life的稳定Acct身份与传入Space归属准确对应，两个活Acct同Set且不同，目标无pending/活动层/附着等待。它不创建Proc、复制当前fork调用的返回现场或发布孩子；Space映射快照必须先在相同VA完成，并由上层在整个冷事务内关IRQ/禁止handler与checkpoint。

fork专门复制当前动作表（含合法handler/trampoline、IGNORE、mask和已实现flags）及有效当前mask，清空孩子pending和全部来源信息，不按父pending补发。所有继承处理入口在目标空间须为普通用户RX；CHLD的IGNORE/NOCLDWAIT策略按复制的动作同步目标Life，不能复用普通create将HANDLER重置DEFAULT的基线。source的原始pending、动作和策略不变。该层读取当前可信mask，不自行识别未来libc临时屏蔽的语义快照，原生fork/Rctx集成仍须提供正确调用时机。

活动层最多两份：完整核验父/孩子用户Sigframe的magic/bytes/depth/previous/当前顶层cookie/保留位和合法返回mask，保持相同frame地址及previous链。重新验证孩子候选TF/FP的用户权限/诊断字段、PC执行映射、SP逻辑写权限，并预检全部帧写范围及COW别名；任一后续帧不合法时不先写前一帧。允许用户已合法编辑的寄存器和restore_mask保留在孩子快照，不用内核旧TF覆盖这些可见编辑。

每层从孩子预付运行槽的持久sequence取得新cookie（低6位仍编码银行槽）；只向孩子用户帧的cookie字段写8字节，COW保证父帧不变。父cookie不能在孩子恢复，即使frame VA、TF/FP及depth均相同。银行sequence在取消/释放/换PID或镜像时仍不重置，预检本次所需cookie数量，耗尽OVERFLOW而不回绕；晚期失败消耗过的序列也不倒退。初次结构赋值曾生成外部memcpy导致freestanding内核链接失败，现只写必要cookie，没有为信号层引入libc依赖，失败日志保留。

孩子的内核Sigctx只复制原受信用户现场、动作快照、帧地址、已保存的mask/活动mask/返回mask和signum，并使用孩子cookie；父Rq/目标Image/等待边/重启责任不复制。当前已递送sigsuspend的结果与返回mask在原用户帧内，孩子恢复它时不接管父等待。尚半附着而未形成活动帧的wait_kind、未完成Rq及未支持重启状态明确NOTSUP；不将这项配置复制冒充已完成POSIX Rctx/_Fork所有外层操作修复。

Signal银行新增forked标记，成功复制后普通siginitial/sigbaseline及重复sigfork拒绝覆盖这份快照；银行正常sigdiscard/release仍按目标Image结束及cookie持久规则收尾。Sigctx单份大小及用户帧布局不变，Signal因对齐每槽增加16字节，默认64槽总BSS增加1024字节：ARM数组356352→357376，RV323584→324608。编译器sigfork静态帧ARM2608/RV2112字节，布局/.su证据见本轮；这不是完整最坏栈深、IRQ延迟或性能保证。

sigrestore的SP检查现使用spacewritable逻辑预检，允许原合法用户SP落在仍共享的R/COW页上，不为了返回就预先复制整页。真正R/代码/设备仍不可当可写栈；该预检只验证逻辑W和全部别名，不消耗承诺/改PTE。TF/FP的HAL受信边界及先验证后写上下文规则保持。未来实际用户向该栈页store时仍走已验证的COW故障路径。

10项两种页表编码host专项覆盖动作/mask/CHLD策略继承且pending不复制、两层cookie重绑定、父帧不变、父cookie重放拒绝且孩子层保持、合法TF/返回mask编辑、COW SP不复制恢复、坏后续父帧/目标代码/父子关系/缺失映射拒绝、RESETHAND当前DEFAULT与活动handler快照分离、半附着等待拒绝，以及将cookie上限缩至2后运行槽跨PID复用仍耗尽。最初host构造器在Life已经LIVE后才bind Acct，已按真实构造顺序改为发布前绑定，未放宽生产生命周期检查。

sigframe两架构在原11个真实用户处理场景之外，新增两组可信银行/真实Space组合检查：无活动层继承及两层活动帧继承。空间先按完整snapshot得到COW栈/immutable代码，孩子prefix换cookie后父prefix仍原值；父cookie重放返回INVALID，换回孩子cookie后两次sigrestore回到原PC/SP/参数及mask，原SP所在页仍R/COW。父两层/pending保持，可独立恢复，全部银行、页、费用和预留最终回基线，主机独立核对SIGFORKCHECK。

新增银行组合检查在内核中调用生产sigdeliver/sigfork/sigrestore/HAL转换，不把仿真的子返回Hctx交给CPU执行，也不声称已有handler中真实_Fork返回0的端到端路径；实际信号入口/handler/sigreturn继续由原11个用户场景验证。统一Space/Local/Signal事务、forkprep/commit/abort、用户返回续点、运行库Fix及真实信号来源仍需后续接通。失败后的未暴露孩子须由上层取消；只读cookie写或银行复制并不替代整个fork回滚。

本地make unit全部516项、make fmt-check及make test-qemu全部110个两架构场景通过；证据build/host/stage73-signal-fork/，含所有Signal/Space源码/契约头哈希、两架构SIGFORKCHECK、新旧银行对象尺寸及静态帧。freestanding结构赋值链接失败保留arm64-before-copy-fix.log及原link-kernel.log，改为8字节cookie写后全量链接/运行通过。无CI、真实硬件、性能或handler中原生_Fork返回0/完整Fix验收。

fork快照接续（2026-11-17）：sigfork现由可信forksnapshot在Space/Local/保底已准备后作为最后可失败阶段调用；失败清空孩子视图并恢复父页日志，不回绕已消耗cookie。两层帧/孩子cookie的双架构组合证据归03末尾和stage75，尚无原生fork/Prep发布及运行库Fix联动。

### 串口SPSC字节辅助层（2026-11-18）

已接include/proto/serial.h布局及libsys私有serialinit/read/write，SDK导出同源协议/私有头并记录哈希；无libc、libatomic或用户态HAL依赖。16 KiB映射的头、数据与剩余零填充由布局断言锁定，初始化完整清零；运行读写仅触及有效数据和各自位置，每次最多32字节。

8项host测试各在O0/O2运行：零填充/布局、坏输入及缓冲别名、满环不覆盖与跨尾数据顺序、损坏头/位置不提交、EOF/ERROR保留待消费数据、u64计数耗尽不回绕、5000步独立FIFO对照，以及真正并发的SPSC线程传输131072字节。两架构CASE=runtime在真实用户态调用同一生产辅助代码，实际检查填满/背压、跨尾排空、旧epoch/别名/坏位置拒绝及UINT64_MAX终点；无条件CHECK失败会真实_exit，非打印常量冒充成功。

目标用例使用测试进程自己的普通内存，尚非serial/tty跨进程SHARED映射或设备收发。正常init链接不含测试ringusercheck/serialring或这些未使用辅助函数；SDK对象无未定义符号。实际UART后端、通知/IRQ主循环、服务装载与READY、端点退出/重连及epoch握手仍待接，不能将本点称为实际serial服务已交付。完整回归及产物核对证据归build/host/stage103-serial-ring/。

最终完整make test-all通过689项host、fmt-check及167次两架构QEMU，含12次内存容量和8次四核/真实IRQ/停放场景。独立核对全部成功guest结果的源码/共享头哈希、两架构SDK源/头/归档/实际runtime ELF及导出内容，检查测试符号确实存在于runtime而不进入生产init；证据evidence.json和完整test-all.log。无CI、板端、性能或实际serial READY验收。

serial推进接续（2026-11-18）：sbin/serial/io.c已连接现有UART与两个SPSC方向，满RX屏蔽设备接收中断并保留数据，TX部分发送由私有32字节缓冲持有，提示与again供外层推进；具体步骤、失败副作用及专项/两架构验证见09末尾。反向RX_ROOM/TX_READY与精确PID1/image控制晚绑定方向已由用户确认，尚非已交付的生产serial/tty循环。

该接续完整753项host/格式及167次双架构QEMU已通过，含两架构实际UART字节的journal/串口独立核对；默认生产init ELF保持不变，未声明外部RX、实际IRQ服务循环或READY/tty上线。证据stage111-serial-io/，详细验收归09。

独立serial接续（2026-11-19）：上述首次控制协议、异步READY和实际IRQ循环已落生产程序；原生测试PID1加载同一ELF并核对晚绑定/反例、阻塞后两批UART输入、实际TX与超时退出。测试同伴不是tty，不授MMIO/IRQ，不以它代替行规程、终端或默认init上线。PL011判空/ICR漏唤醒已通过旧C反例修复为清后重查；详细执行/完整回归状态见09末尾及stage112-serial-service/。

该接续完整763项host/格式及171次双架构QEMU通过；实际服务每架构分别验证阻塞后双批输入/IRQ重新启用与READY超时退出，144份journal及来源/ELF/SDK哈希另核对。生产init装载/监督及实际tty仍是后续，不以测试字节同伴替代它们。

### CHLD真实退出来源（2026-11-19）

sigchild从可信已提交孩子身份和退出状态构造完整64字节Siginfo，code=SOURCE_CHILD、signum=CHLD、pid=完整PID、uid=ruid、reason/status=原生WAIT kind/code，所有无关字段清零。只接受当前已实现的WAIT_EXITED与合法退出/信号/故障状态，不用非法输入补造停止/继续通知。sigqueue继续负责IGNORE、掩码、首份信息合并及层数限制，构造器不绕过这些规则或自行递送。

Proc.end在lifeend已提交结果/当时父策略后，按Life的实际逻辑父身份向其当前镜像排队；显式IGNORECHLD不产生通知，NOCLDWAIT未IGNORE仍允许通知，不消费/重建wait记录。目标Proc必须仍可运行或等待，已退出父不会因旧PID收到通知；逻辑孤儿归属按Life既有规则，不为历史退出重新生成信号。普通wait/recv的中断规则保持原边界，CHILD事件仍独立唤醒相应重查；信号递送只在完成用户返回点，不能在孩子退出的内核C续点直接进入父handler。

两项新增host在两架构现场布局直接读取实际用户帧，验证CHILD字段与零填充、同号首份元数据合并、信号/故障退出状态、非法PID/原因/状态不修改及IGNORE后不复活旧信息。原生sigconfig的父安装自定义CHLD handler/trampoline，真实孩子以31退出，handler逐字段核对PID/ruid/code/reason/status与零字段，再经原生sigreturn恢复，父仍按正常wait取得唯一结果。主机另核对用户trace及child_info=1并保留全部原策略/清理检查；没有由可信fixture调用sigqueue伪造此CHLD。

两架构专项及完整make test-all已通过：782项host、格式检查、179次双架构QEMU；独立核对152份journal、最终源码/头/SDK哈希与两份真实孩子退出handler记录，证据build/host/stage115-child-signal/。无CI或板端结果。USER sigsend、同步FAULT生产/归一化、STOP/CONT、普通阻塞中断、libc标准siginfo转换仍未实现，本次不把共享常量或可信code0测试当作它们的交付。

### tty参数与有界输出转换（2026-11-19）

include/proto/tty.h作为Termarg唯一C布局来源，80字节及各字段偏移由断言锁定；固定标志/cc索引对应本篇既定协议，SDK自动导出同源头。sbin/tty/term.c校验独占普通内存中的参数快照：拒绝未知位、非CS8、未用cc及与后端已选速率不一致的配置，不读设备或更改模式。后端实际速率发现和SETATTR事务仍待接，不能将传入校验器的数值视为硬件设置证明。

输出转换独立于serial/PTY：每次至多32输入字节，输出至多64字节，仅同时启用OPOST/ONLCR时将LF转换为CRLF，其他字节保持原值，包括NUL、CR和高位字节。返回实际输出量并单独报告已消费输入前缀；容不下整个CRLF时不消费该LF。未知标志、超批次、坏几何及输入/输出/计数重叠在写出前拒绝，失败保持原输出/计数。编码到私有staging后先排空该段，再接下一输入或按新模式编码，不能在部分后端发送后重新编码原输入。

例子：输入A、LF、B，输出容量2时只消费A；下一次容量4才消费LF、B并产出CR、LF、B。若serial只收下staging的CR，tty保留LF、B并从该后缀重试，既不丢LF也不再补CR。后续WRITE结果须按原输入字节计数，不能返回转换后的设备字节数；服务请求提交/取消及模式变更排序另按10/11既定事务规则实现。

6项host专项在O0/O2验证全部字节透传、全部输出容量的最长合法前缀、成对换行背压、随机部分后端发送、参数拒绝与输出不变。真实runtime用户载荷另调用同一生产模块，并以实际SPSC TX满环/腾出7字节/分段发送验证staging后缀不重复不丢失。完整make test-all通过788项host、格式检查、179次双架构QEMU；152份journal、最终源码/SDK哈希、真实用户调用及生产init/serial无fixture均另核对，clangd编译数据库包含新增源码。此点不是tty服务、行规程、read计时/回显、前台信号或可用终端，不提前创建占位main或将fixture放入默认包；无CI/板端/性能结论。证据build/host/stage116-tty-output/。

### 有界输入行规程核心（2026-11-19）

sbin/tty/input.c现将原始输入和规范行放在同一固定8192项私有队列，每项u16保存一字节及行结束/空EOF边界；没有额外复制整条4096行的提交步骤。head/used/ready均为有界索引/数量，不用会溢出的单调队列位置；未提交编辑尾长最多4095。逐字节入队、删尾、删行、标记EOF、信号清空均O(1)，每次读最多32字节；仅初始化清零整个私有缓冲，运行中不分配、调用后端或执行syscall。

按用户本次确认的规则先ICRNL映射，再识别已启用控制字符，ISIG优先，随后规范模式的VERASE/VKILL/VEOF；用户将多个cc配置成同值时按该顺序，信号内部为VINTR/VQUIT/VSUSP。普通字节只有成功保留才产生回显，丢弃字节不补假回显；^U回显LF，删除非空编辑尾回显BS/空格/BS。信号控制字符回显其caret表示及LF（自定义非控制字节则回显原字节及LF），逻辑回显仍交ttyencode按当前OPOST/ONLCR转换，不混进输入数据。该删字按字节处理，不承诺UTF-8字形宽度或完整屏幕光标编辑。

规范LF保留为行数据；非空VEOF只结束已有编辑尾，不将^D追加为字节，也不再追加第二份空EOF；空VEOF占一个私有边界项，下一次非零长度读成功返回0一次，随后仍可继续接收新输入。零长度读不消费EOF；空/未成行返回WOULD_BLOCK。短读只消费当前行前缀；即使行末恰好落在32字节边界，独立ended仍为1，服务的较大READ不能继续拼下一行。原始模式按字节提供已有输入，VMIN/VTIME由后续请求状态机决定何时提交出队，本点不偷换成完整非规范read语义。

Ttyeffect为私有一次性推进结果，含逻辑回显、请求信号及丢弃尚未提交输出的标志，不是新线上结构或授权。ISIG在本地先丢未消费输入，再交服务处理输出/前台信号动作；服务必须处理该动作并持有回显staging后才取下一输入字节。这里尚未执行后端输出丢弃、前台组授权或真实INT/QUIT/TSTP投递，也不写共享环对端拥有的位置。新参数模式须经独立SETATTR事务接续，当前core显式拒绝输入队列与ICANON不匹配的快照，不能借重新初始化活队列掩盖模式切换责任。

例子：收到ab、DEL、c、CR时，未提交尾依次ab→a→ac，ICRNL把CR映为LF后发布ac+LF；read(1)取a，下一读取c+LF并结束本行。随后空^D使下一读返回0一次，后续x+^D只返回x，不再次零读。若完成行q+LF之后还在编辑p，^C同时丢q+LF及p，产生INT/输出丢弃动作；已交给后端的旧输出不能因此撤回，后续服务须在真正交付读前重查前台资格。

13项host专项在O0/O2覆盖全部原始字节、CR/禁用cc、编辑与行隔离、空/非空EOF/短读/零长、精确批末边界、满行/满队列/回绕、饱和丢失、损坏几何/token拒绝、ISIG动作及15000步独立记录队列对照。两架构真实runtime用户载荷从同一SPSC字节辅助层取数据，调用生产输入核心并检查回显输出转换、4096字节完整行/超限、EOF、信号动作和原始字节，失败真实_exit；并非实际tty进程或已授权信号递送。最终完整make test-all通过805项host、格式检查、189次双架构QEMU；162份journal、最终源码/头/SDK/配置/包哈希、实际用户调用与默认服务无fixture另核对，clangd数据库含新增源。证据build/host/stage118-tty-input/，无CI/板端/性能结果；尚无VMIN/VTIME、读请求提交/取消、活模式变更、真实输出flush、tty服务/控制终端或可用sh。

首次全量在ARM64 CASE=elf发现测试载荷额外预留16KiB环，使共享runtime ELF装载超出原64页额度；现将顺序运行且独占的输入/输出fixture复用同一私有环，移除重复空间，未调大孩子额度或修改装载器。测试环重新初始化只在相邻独占测试之间，不是生产共享活环重置。两架构elf专项再次验证成功发布和四类失败回滚，旧失败日志保留；最终完整回归另核对默认init/serial ELF与stage117逐字节哈希相同。

### VMIN/VTIME读等待与独立tty测试（2026-11-19）

sbin/tty/wait.c为一个已接受READ保存私有参数/期限，按固定四种组合判断是否可完成；长度上限4096，MIN阈值夹到请求长度，TIME单位0.1秒。MIN=0/TIME=0即使无数据也完成；MIN>0/TIME=0等待阈值；MIN=0/TIME>0从接受时启动总期限，收到字节/重复检查不顺延；MIN>0/TIME>0先不设期限，已有原始数据在接受时视为首字节，后续每个真正保留的新字节才重置间隔，到阈值或间隔到期即可结束。规范模式只看已有完整行/EOF边界，忽略MIN/TIME；O_NONBLOCK优先以现有就绪输入或WOULD_BLOCK完成，零长读不设期限也不消费EOF。

全部时间采用服务传入的单调绝对纳秒，乘加先检查溢出；时间倒退、坏快照/状态失败不部分改写。只在确实等待时输出最近绝对alarm期限，重复检查不延长等待；不把deadline=0当成已到期。服务每轮在提交下一输入字节之前先结算已到期READ；ttywaitbyte在now>=旧期限时返回“先到期”且不修改状态，不能让本轮新字节把旧期限无限延后。按服务处理点排序，不假称能从无时间戳的共享环恢复设备上每个字节的物理到达时间。

该模块不consume输入、不调用alarm/recv、不持IPC或授权；取消尚未提交的READ只需退出对应等待责任，队列保持原值。真正出队仍须在服务核对前台关系、取消状态和稳定响应空间后完成，期限就绪不是字节预留或跨请求提交证明。活SETATTR及等待中模式变更仍须按服务事务接续，不凭纯等待状态绕过它们。

新增CASE=tty及独立ttyuser.elf，原输入/输出核心专项移入该载荷，不继续将tty测试增长计入通用runtimeuser/ELF加载器的64页样例。测试ELF只链接已存在的生产tty模块及tests/kernel载荷，不创建生产tty main或加入默认包。可信测试装载器按自身逐页保留Local的已有契约供足测试资源槽，初次误配8槽在发布前被拒，已修正fixture而未放宽原生装载器权限/容量检查。

12项host在O0/O2覆盖四组合、已有输入、MIN/长度夹取、规范EOF、非阻塞/零长优先、重复检查不延时、到期前后字节顺序、绝对时间倒退/溢出及随机期限模型。两架构独立原生场景通过总期限available=0与间隔期限available=1两种快照，各一次0.1秒真实alarm+recv阻塞/唤醒并再检查等待判定，保留原输入/输出全部断言及退出/费用归零核对；不是向客户端交付完整READ的证明。完整make test-all通过817项host、格式检查和191次双架构QEMU；164份journal、最终源码/头/SDK/配置/包哈希及独立tty实际用户调用另核对，默认init/serial ELF与stage117相同，通用runtime ELF不再链接tty fixture；新增源均进入clangd数据库。证据build/host/stage119-tty-wait/；尚非生产tty进程、完整read/取消事务、实际前台信号或可用sh，无CI/板端/性能结果。
