# 系统调用 ABI 与模块边界

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：2026-11-15设计规格已逐项修订；Handle/错误和Config/Allocarg/Maparg/Boot等共享定义已落头文件，可信内核调用局部通过两架构QEMU；原生exit/yield/getid及自身内存调用已接通并局部验证，其余入口未接，未运行CI。当前接口选择及交叉复核记录以正文/status为准，不把“已选定”当作全部验收，不保留旧ABI兼容层。

## 模块边界

内核提供保护、页/身份/调度、短IPC、信号及设备控制机制；不解析fd、ELF、FS/TTY操作码。fork/execve/posix_spawn、open/read/write/close/poll由libc/libsys和资源所属服务组合，普通操作不经init代办。__sys_*是libsys私有桩，Unix名字由libc导出，原生IPC为sys/channel.h中的ipc_*；不增加裸create/start、通用invoke/kctl/OP、waitack/reap、jobstart、内核poll或睡眠对象。

### 共享定义与头文件边界（已确定）

include/abi按base/syscall/ipc/proc/mem/event/signal/boot.h分；include/proto按用户服务分；include/hal是内核硬件契约。内核不包含proto，用户态不包含hal。ABI结构用u64/s64/u32显式布局、little endian、8字节自然对齐（TF/信号现场16字节）；无packed、long、enum存储或嵌套指针。reserved输入必须0，未知位/调用参数拒绝，不维护多个版本兼容分支。原生地址字段为受检查u64用户VA。

## 编码与寄存器（已冻结）

Handle=s64，符号位0、类别4位、代次43位、索引16位；有效值>0，0无效，初始init PID=1是唯一例外。类别：PROCESS_ID=1、RESOURCE=2、REQUEST=3、RPC=4、SESSION_ID=5、GROUP_ID=6、TERMINAL_ID=7，其余保留。位运算使用u64，代次从1起，耗尽拒绝不回绕。PUBLIC PID/PGID/SID规则见03；OBS cookie不是Handle类别或授权对象。

RV64调用号a7，输入依次a0..a6；ARM64调用号x8，输入x0..x6。表内未列输入为0，RV64 a6/ARM64 x6仅reply_recv使用，其他为0；ARM64 x7输入为0。返回status在a0/x0，负数为原生错误，非负结果按表解释。IPC结果固定：kind=a1/x1、source=a2/x2、request=a3/x3、words[4]=a4..a7/x4..x7、image=t0/x9。recv的event_mask放a1/x1；reply_recv放a6/x6。a7既是调用号又是输出最后业务字，桩必须声明读写。

普通成功/错误未使用输出全部清零，image除recv/reply_recv的CALL/SEND外为0。错误附带输出限定为：已入队call被信号中断等待，request返回同一RPC（QUEUED/ACCEPTED均保留）；同步call因PEER等机制错误终结但server_finished尚未成立时，也在request返回原RPC供结果确认/安全收尾。其他错误输出0；send从不产生RPC。所有其他GPR保存，ARM64 x8不承诺值。入口先保存全部GPR/必要扩展状态，后改输出；handler的完整保存不因寄存器为caller-saved而省略。

kind：NONE=0、CALL=1、SEND=2、EVENT=3、RESOURCE_REPLY=4。普通同步call回复kind/source/request/image为0；显式资源回复kind=4、words[0]为安装的本地Handle，其他3字业务定义；异步提交只在request返回RPC。业务错误仍由服务字段解释，内核status=0不等于业务成功。

## 系统调用号与完整输入表（已冻结）

表内参数按寄存器位置顺序，显式0不可省为任意值。0及表外号返回NOSYS；配置禁用但已知的调用返回NOTSUP，编号不移动。创建/分配/准备失败不发布Handle；本表不要求第一阶段把全部入口实现为空桩。

| 号 | 私有桩 __sys_ 后缀 | 输入 | 成功结果/边界 |
| --- | --- | --- | --- |
| 0x01 | exit | code | 不返回 |
| 0x02 | yield | 无 | status |
| 0x03 | copy | dst,src,bytes | status |
| 0x10 | call | target,w0,w1,w2,w3,options | 同步回复或异步RPC |
| 0x11 | send | target,w0,w1,w2,w3,0 | status |
| 0x12 | recv | 0,event_mask,0,0,0,options | 接收消息 |
| 0x13 | reply | request,w0,w1,w2,w3,options | status |
| 0x14 | reply_recv | request,w0,w1,w2,w3,recv_options,event_mask | 接收消息；reply失败不接收 |
| 0x15 | defer | request | status |
| 0x16 | rqpoll | rpc,options | 保存的结果或WOULD_BLOCK |
| 0x17 | rqwait | rpc,options | 保存的结果 |
| 0x18 | rqcancel | rpc | status |
| 0x19 | rqack | rpc,options | status |
| 0x1a | rqfinish | request | status |
| 0x1b | rqclean | control,rpc | status |
| 0x1c | rqcheck | request | a1=LIVE(1)、LIVE+CANCEL_REQUESTED(3)或DRAINING(4)；只允许接收者 |
| 0x20 | notify | target,bits | status |
| 0x21 | clock_gettime | clockid | a1=秒,a2=纳秒 |
| 0x22 | clock_getres | clockid | a1=秒,a2=纳秒 |
| 0x23 | clock_settime | system,clockid,seconds,nanos | status |
| 0x24 | alarmset | deadline_ns | a1=代次 |
| 0x25 | alarmcancel | 无 | a1=代次 |
| 0x30 | alloc | alloccap,argptr | a1=MEM |
| 0x31 | free | resource | status |
| 0x32 | map | space,argptr | status |
| 0x33 | unmap | space,address | status |
| 0x34 | seal | mem | status |
| 0x35 | dmaaddr | mem,offset,length | a1=设备地址 |
| 0x36 | dma_sync | mem,offset,length,phase,direction | status |
| 0x40 | create | createcap,attrptr | a1=CONSTRUCT,a2=PID；SERVICE权创建另a3=CONTROL，普通为0 |
| 0x41 | configure | construct,configptr,reserve_resources | status |
| 0x42 | start | construct | status |
| 0x43 | destroy | construct | status |
| 0x44 | terminate | control | status；并非已退出 |
| 0x45 | wait | selector,options | a1=PID,a2=kind,a3=code,a4=状态序号 |
| 0x46 | grant | target,source,rights,offset,length,inherit | a1=目标本地Handle |
| 0x47 | revoke | target,target_local_handle | status |
| 0x48 | budget | control,argptr | status |
| 0x49 | forkprep | createcap,reserve_resources | 父a1=CONSTRUCT,a2=PID；子见03 |
| 0x4a | forkcommit | construct,fixva,fixbytes | a1=PID |
| 0x4b | forkabort | construct | status |
| 0x4c | execprep | self_space,reserve_resources | a1=EXEC_VIEW,a2=目标image |
| 0x4d | execcommit | view,configptr | 成功进入新镜像，不返回 |
| 0x4e | execabort | view | status |
| 0x4f | getid | 无 | a1=PID,a2=image,a3=父PID |
| 0x50 | observe | permit,basis,relation,pid,image | a1=cookie,a2=state,a3=reason；basis类型由relation决定 |
| 0x51 | obsread | cookie | a1=state,a2=reason,a3=PID,a4=image |
| 0x52 | obsnext | cursor | a1=cookie,a2=next_cursor,a3=state,a4=reason |
| 0x53 | obsack | cookie | status；仅ENDED |
| 0x54 | obsdrop | cookie | status |
| 0x55 | connect | authority,kind,rights,bits | a1=固定目标当前image的本地CALL/SEND/NOTIFY |
| 0x56 | inherit | resource,flags | status；仅本主体允许类型的保留标记 |
| 0x60 | sigaction | signum,newptr,oldptr | status |
| 0x61 | sigmask | how,mask | a1=oldmask |
| 0x62 | sigpending | 无 | a1=mask |
| 0x63 | sigsuspend | mask | 仅中断/退出 |
| 0x64 | sigsend | selector,signum | status；含signum=0检查 |
| 0x65 | sigreturn | frameptr | 成功恢复现场，不普通返回 |
| 0x66 | getpg | pid | a1=PGID,a2=SID |
| 0x67 | setpg | pid,pgid | status |
| 0x68 | setsid | 无 | a1=SID |
| 0x69 | ttybind | terminal,sid | status |
| 0x6a | getfg | terminal | a1=PGID |
| 0x6b | setfg | terminal,pgid | status |
| 0x6c | ttysignal | terminal,signum | status；限定前台信号 |
| 0x6d | getcred | subject,pid,image | 0/0/0为自身；真实REQUEST的source或合法CHILD/FUTURE，或固定CONTROL QUERY目标；a1..a4=ruid/euid/rgid/egid |
| 0x6e | ttycreate | system,owner_control | a1=本地TERMINAL授权，a2=TERMINAL_ID；绑定TTY完整PID/image |
| 0x70 | irqbind | target,source,bit | a1=驱动本地IRQ_BIND |
| 0x71 | irqack | binding | status |
| 0x72 | irqunbind | target,binding | status |
| 0x73 | query | authority,kind,cursor,outptr,count | a1=条数,a2=next_cursor |
| 0x74 | shutdown | system,mode | 固件成功不返回 |
| 0x75 | reboot | system,mode | 固件成功不返回 |

getpg/setpg/setsid的选择器和授权以[会话与进程组操作](03-process.md#会话与进程组操作2026-11-16用户确认)为准。getpg仅使用a0，setpg仅使用a0/a1，setsid无参数；未使用输入必须为0。成功getpg只返回a1/a2，setsid只返回a1，setpg无额外输出；失败输出清零。负选择器为INVALID，非进程类别为BAD_HANDLE，消失/已收取目标为STALE，关系越权或会话首进程操作为DENIED，孩子已成功exec为STATE，关系池满为CAPACITY；失败不改变成员关系。

### 固定参数结构（已冻结）

所有下面未注明字段均u64/8字节对齐，输入一次复制后执行，不反复读用户参数。configure的第三寄存器reserve_resources=0..8，与Config整体验证后同时提交，镜像内资源回复保底槽不足时保留上一完整配置；不让内核解析fd表。保底槽按08用于发布后的按需接入，未用部分留至镜像结束，不是待execready清除的启动标志。

| 结构/大小 | 字段与字节偏移 |
| --- | --- |
| Createarg / 48 | memlimit@0、exec_limit@8、resources@16(u32)、pending@20(u32)、ruid@24(u32)、euid@28(u32)、rgid@32(u32)、egid@36(u32)、reserved@40 |
| Config / 48 | entry@0、stack@8、boot@16、bootsz@24、sigmask@32、sigdefault@40 |
| Allocarg / 48 | bytes@0、kind@8、align@16、minaddr@24、maxaddr@32、reserved@40 |
| Maparg / 48 | mem@0、offset@8、address@16、length@24、prot@32、reserved@40 |
| Resreply / 56 | source@0、rights@8、offset@16、length@24、business[3]@32 |
| Budgetarg / 32 | memlimit@0、exec_limit@8、max_instances@16、reserved@24；Acct调整时max_instances要求0 |
| Sigact / 48 | kind@0、handler@8、trampoline@16、mask@24、flags@32、reserved@40；DEFAULT=0、IGNORE=1、HANDLER=2 |
| Queryrec / 192 | type@0、pid@8、image@16、stamp@24、data[20]@32；所有未用字0 |

Budget CONTROL只改绑定目标Acct的limit/exec_limit，SYSTEM POLICY通过绑定集合的CONTROL改集合上限；降到当前charged以下返回BUSY，不强制回收。资源槽/请求预付数量创建后不在线扩表，不能借budget改变ABI容量。管理者不得凭数字构造新设备/集合身份。

### 标志、状态与原子性（已冻结）

CREATE_USE=1、CREATE_CREDENTIAL=2、CREATE_SERVICE=4；普通孩子清除后两位及GRANT，四上限夹到其Createarg（03）。资源槽至少3以容纳SELF_SPACE/SELF_ALLOC/CREATE，pending为1..编译接收上限，无0默认。

CALL_ASYNC=1、CALL_RECV_MEM=2、CALL_RECV_CALL=4、CALL_CONTROL=8，RECV_MEM与RECV_CALL互斥；REPLY_RESOURCE=1且仅reply支持。RECV_NONBLOCK=1、RECV_EVENTS_ONLY=2；reply_recv只解释这两位接收选项，不支持资源混合交付。RQ_FINISH=1；ACK_RESULT=1、ACK_RELEASE=2，组合先整体检查，失败不部分修改。inherit FORK=1、EXEC=2；资源GRANT为bit63，其他低位按03子类型解释。sigmask how BLOCK=0、UNBLOCK=1、SET=2。READ=1、WRITE=2、EXEC=4。wait WNOHANG=1、WUNTRACED=2、WCONTINUED=4；结果EXITED=1、STOPPED=2、CONTINUED=3，code低8位为正常退出码或信号号，bit8=SIGNALLED、bit9=CORE（首版恒0）、bit10=FAULT；STOPPED为停止信号号，CONTINUED=0。完整故障原因在query保存；libc按kind/位编码标准wait status。

rqpoll/rqwait非消费；rqack只操作本主体RPC。rqcheck仅服务当前持有REQUEST，返回1（正常ACCEPTED）、3（ACCEPTED且cancel_requested）或4（强制终结/DRAINING）；不能读取他人请求。rqclean要求CONTROL覆盖已死亡服务且完成安全隔离责任已由可信恢复路径接管；不能将活服务或未知DMA直接标finished。即使调用者死代消费/释放，server_finished仍必须真实成立。

query kind固定PROC=1、RQ=2、ACCT=3、LIFE=4、RESOURCE=5、BOOT_RESOURCE=6，最多16条，每条Queryrec；cursor为索引+代次快照，0为首次输入及遍历结束输出；调用者依据next_cursor=0结束本轮，不将其再次当继续位置，不承诺跨条全局原子。主体无QUERY只可查自身非敏感统计；管理查询从CONTROL/SYSTEM授权范围枚举。kind选择仅查询记录，不是对象任意操作码。data字含义：PROC=[parent,pgid,sid,state,waitpid,cpu_ns,used,reserved,resource_count,map_count,rq_count,tty,wait_kind,wait_selector_kind,wait_selector,...]；RQ=[caller,callerimage,server,serverimage,phase,result,flags,...]；ACCT=[set,limit,exec_limit,used,reserved,state,...]；LIFE=[parent,reason,code,wait_pending,observe_refs,cleanup_refs,...]；RESOURCE=[local,subtype,rights,offset,length,inherit,target_pid,target_image,...]；BOOT_RESOURCE仅init可读受信设备/启动镜像记录，其data=[resource,kind,start,length,irq,flags,...]。都不含内核指针/完整他人寄存器。

PROC的state固定CONSTRUCT=1、READY=2、RUNNING=3、BLOCKED=4、EXITING=5、STOPPED=6；STOPPED仅待停止机制接入后产生，不输出内部BUILDING。仅返回仍有运行记录的目标；原运行记录回收后为0条，旧CONTROL不会读到同槽新PID。PROC为整实例单条，cursor只接受0，next=0，image为当前/清理中的镜像，stamp为运行身份代次而非内容版本。

cpu_ns为该进程实际被调度选中期间的累计单调纳秒，包含它的用户/内核工作与该窗口内IRQ开销，不包含阻塞、其他进程及CPU空闲时间；不拆user/sys，也不表示硬件性能。当前进程查询包含尚未切走的本次片段，运行槽复用重置为0。waitpid仅在强结果/收尾阻塞且仍有可调度强等待边时为该边对端完整PID；对端已结束而仅等待独立收尾、recv/就绪/运行/退出为0，原请求目标仍从RQ记录查询。resource_count为本地尚占用授权槽数，map_count为成功提交且尚未解除的完整Mmap条数，rq_count为当前PID/image仍实际保有caller/receiver责任的请求数，同一自请求只计一次，不计空闲预付槽。tty=0表示未关联终端。wait_kind为0无等待、1接收、2请求结果、3独立收尾、4构造撤销续点、5父wait、6信号等待；READY但尚未恢复的续点仍显示其等待种类。父wait的wait_selector_kind为0任意孩子、1完整PID、2冻结组，wait_selector对应0/PID/公开PGID；不暴露内部GROUP Handle。其他等待的selector两字0，其余保留字0。

### query的主体选择（交叉复核修订）

query(0,kind,cursor,outptr,count)为明确的本主体查询：PROC/ACCT/LIFE只返回当前调用者自己的记录，RESOURCE只枚举当前镜像本地授权，RQ只枚举本主体作为caller或receiver实际持有的请求；不按cursor中的索引扩大到其他主体。BOOT_RESOURCE不接受authority=0。管理查询仍要求显式CONTROL QUERY或SYSTEM既定权限，只枚举其授权范围；普通CALL、PID、REQUEST等整数不能作为管理authority来补权。

cursor是相应kind/授权范围内的有界遍历位置，0开始，返回next_cursor=0表示本轮结束；不是目标PID或资源权限。count必须1..16（0或更大值INVALID），先整体验证count/输出范围，逐项保存稳定身份快照；不承诺跨项原子。库查新授CALL时用本主体RESOURCE路径，按完整local Handle匹配记录，再取target_pid/image；未找到或代次不符明确失败，不拿同槽新授权替代。此规则只补齐既有query的authority=0输入，不新增调用或放宽观察/安装权限。

有效CONSTRUCT/EXEC_VIEW CONFIG是仅限RESOURCE的准备表查询入口：只枚举本次精确目标表，不接受裸PID选择，不接受其他kind；提交/取消/超期即失效，源与目标PID/image/表epoch每次重新核验。枚举结果属于目标本地表，不是调用者的新授权。

Sigact仍48字节，handler@8、trampoline@16分别为用户处理函数和前置/返回入口；HANDLER时都须用户可执行。信号原生入口先到trampoline，六参数及动作快照规则见11，TF/FP及Sigframe布局不变。

### 连接缓存的固定目标查询（第7项审查修订）

本主体RESOURCE查询中，自己持有的CALL/SEND/NOTIFY在data[6]/data[7]返回该入口绑定的完整target_pid/target_image，其余子类型这两字为0；不随目标exec重定向，不泄漏内核指针或枚举他人授权。通过query既有本主体查询路径只读核验自己的入口目标，不授予CONTROL/连接创建权。VFS OPEN新授CALL后，libsys在冷路径按这组真实固定目标查找缓存，不能将两个不同本地Handle误当不同后端，也不按后端名字重绑定；热I/O沿缓存的合法CALL，不逐次查询。

文件共享协议的36864字节连接、128字节槽描述符/两页span归include/proto，不是内核解码的ABI；Msg仍四u64，CALL资源交付仍一个资源，调用表仍73项。详情与容量错误见10。

### call中断与协作取消（第6项审查修订）

调用号及参数个数保持73项。已入队call遇捕获信号先退出等待时status=-INTERRUPTED、request为原RPC，其他输出0；不代消费/释放，不摘队或终结请求。入队前信号先提交则request=0；回复先提交则保持原正常同步返回。中断之后的原reply由rqpoll/rqwait取得并显式rqack，不直接写handler现场。同步机制失败若仍有服务/下游收尾责任，同样交付原RPC；已安全完成且无需跟踪的同步失败由内核代确认，不凭错误码猜槽可复用。

rqcancel(0x18)对QUEUED摘队形成CANCELLED且保证未交付，对ACCEPTED仅置cancel_requested（不终结、不归还接收槽），对TERMINAL幂等不改结果。rqcheck(0x1c)只有1/3/4三种成功位值，已协作取消仍可普通/资源reply；rqfinish只接受4。逻辑超时不自动变内核终结，退出/exec等强制终结才进入DRAINING；详细转换及三责任回收见04。未新增文件业务结果查询syscall或扩宽Msg。

### 发布与按需接入（第5项审查修订）

当前表共73个调用；删除execready，0x57未分配并按未知号返回NOSYS，其他号不移动，不保留旧实现兼容桩。forkprep/execprep/configure的reserve_resources保持0..8，预付镜像资源回复保底槽；同意资源接收的CALL先取保底槽，没有保底再取普通空槽，QUEUED取消/机制强制终结或普通回复归还原槽，ACCEPTED协作取消保留预留，资源安装后转普通已占槽；未用保底不能被alloc/grant占用，到镜像结束才回收，计入既有资源槽上限。

PREP_NS只管未发布准备，成功start/forkcommit/execcommit不延续该期限到ACTIVE镜像。子返回及新csu进入应用只需本地修复，首次需要后端时按08/10领取已准备连接；内核不等待服务接入或READY，不提供通用启动完成确认。init清单服务READY仍是独立用户态策略。

### dma_sync的方向与完成边界（第4项审查修订）

dma_sync保留0x36及五参数；phase BEFORE=0、AFTER=1，direction DEVICE_READ=1、DEVICE_WRITE=2、BIDIRECTIONAL=3，均从设备看。同请求两阶段的窗口/方向一致；非零范围及按HAL粒度扩展后的授权边界在维护前整体核验，未知值拒绝。非一致可缓存映射的三个方向在BEFORE均需处理旧脏行，AFTER对设备写/双向invalidate；一致/不可缓存映射仍须满足发布/获取顺序，完整要求见[设备同步契约](09-devices.md#同步矩阵与缓冲所有权)。

成功仅表示本次受检缓存维护及必要完成屏障已执行，不等待设备、不建立内核逐请求所有权状态，也不证明DMA停止。驱动核验真实完成后才交回请求缓冲；持续共享的队列观测按09单独的字段规则执行，不凭AFTER返回成功解除固定。

### grant和observe的授权输入（第2项审查修订）

grant的a0/x0必须是本次有效CONSTRUCT/EXEC_VIEW CONFIG，或绑定单一进程Life的CONTROL INSTALL。CONTROL QUERY=0x01、TERMINATE=0x02、BUDGET=0x04、IRQ=0x08、REVOKE=0x10、CONNECT=0x20、INSTALL=0x40；已有低位不移动，资源自身GRANT仍为bit63；grant检查source的转授，connect另要求输入CONTROL CONNECT+GRANT才能生成可转授入口。revoke固定进程目标要求REVOKE，不能用INSTALL/QUERY替代。SERVICE创建返回的持续CONTROL包含INSTALL；普通create不多返回管理权。集合/域CONTROL不直接指定grant/固定监督目标，SYSTEM POLICY不替代所需目标句柄。

observe仍为0x50、五个输入，不增加结构或消息字段：a0/x0=OBSERVE permit；a1/x1=basis；a2/x2=relation；a3/x3=pid；a4/x4=image；a5/a6、x5/x6输入为0。relation SOURCE=0、CHILD=1、FUTURE=2、MONITOR=3，对应permit权位分别1、2、4、8。SOURCE/CHILD/FUTURE的basis必须是当前服务仍持有的REQUEST（合法DRAINING REQUEST也可用于接管终结）；MONITOR的basis必须是本地固定进程CONTROL并含QUERY。MONITOR的basis不得为0或REQUEST，init也不能省略它。

SOURCE要求pid=image=0，由REQUEST取得目标；CHILD/FUTURE参数及构造关系见10。MONITOR只从basis解析目标Life：pid=0不额外选择身份，非0仅核对它等于该Life完整PID；image=0原子捕获该Life当前/未发布初始镜像，非0只查该Life实际存在且保留的对应Image。一个cookie只观察这一个Image，目标exec后不跟随新镜像。不存在的旧记录返回STALE，不为猜测的image创建记录。

permit和MONITOR的CONTROL basis在调用者本地资源表解析；其他模式的REQUEST basis按全局Rq槽/代次解析，并校验接收者PID/image等于当前服务，不作为本地RESOURCE查找。先校验各自类别、所属、权限、代次，再验证目标关系/可选PID断言；不扫描本主体资源表寻找另一个CONTROL来补权。类型/本地句柄非法为BAD_HANDLE、缺权为DENIED、合法basis与非零pid不匹配为INVALID；关系阶段不接受为STATE或已失效记录STALE。失败输出清零且不扣观察配额。已ENDED返回cookie=0和真实终态、不新登记。成功登记的cookie独立持有该固定镜像的观察责任；之后revoke basis/permit只阻止新登记，不自动删除既有cookie，与已有Mmap/Rq不因源授权失效而追溯回收的原则一致；obsack/drop或观察者镜像终结才解除该责任。getcred的CONTROL分支复用同一固定Life解析/QUERY检查，不凭裸pid或进程名豁免。

### 错误层次与重试判定（语义收口）

原生错误独立于errno，status为下列正编号的负值；不机械把所有池/状态映射同一个POSIX错误。

| 编号 | 名字 | 精确边界 |
| --- | --- | --- |
| 1 | INVALID | 参数/保留位/类别组合非法 |
| 2 | FAULT | 用户VA/访问/受检查复制失败 |
| 3 | BAD_HANDLE | 本主体无合法该类表项；无权区分他人身份时同此 |
| 4 | STALE | 已获权范围内原实例/代次已终结或复用 |
| 5 | DENIED | 有效授权范围不足 |
| 6 | STATE | 该阶段不接受操作 |
| 7 | BUSY | 有效对象暂被相冲准备/VM事务占用 |
| 8 | LIMIT | Acct/Set页或实例上限 |
| 9 | CAPACITY | 指定专用记录池/主体预付槽不足 |
| 10 | NOMEM | 实际物理页/连续块不能满足 |
| 11 | WOULD_BLOCK | 非阻塞查询条件未满足 |
| 12 | INTERRUPTED | 捕获信号只结束当前等待，已入队call保留原Rq/RPC |
| 13 | CANCELLED | rqcancel在QUEUED摘队，保证未交付；ACCEPTED协作取消不立即产生该终态 |
| 14 | PEER | 固定对端实例/镜像失效 |
| 15 | REVOKED | 精确来源授权被撤销 |
| 16 | DEADLOCK | 当前强等待边确认形成环 |
| 17 | DEPTH | 等待检测上界不足，未证明有环 |
| 18 | NOTSUP | 已知机制/配置不支持 |
| 19 | NOSYS | 未知调用号 |
| 20 | OVERFLOW | 地址/长度/计数/代次耗尽，不允许回绕 |
| 21 | TIMEDOUT | 内核准备固定期限已过；普通call没有内置超时 |
| 22 | CHILD | wait没有匹配且可由调用者收取的孩子；libc映射ECHILD |

服务回复w0采用s64业务状态：0成功、负的SDK errno为业务失败，w1..w3是该操作结果；资源reply保留w0给Handle，业务成功类型由kind确认，其余业务字按proto解释。libsys保留机制/业务/执行阶段三类信息，libc才写errno。标准errno数值由首版SDK固定为常见Linux编号集合以便移植，但不承诺Linux二进制ABI；EWOULDBLOCK=EAGAIN=11、EINTR=4、EIO=5、EBADF=9、ENOMEM=12、EACCES=13、EFAULT=14、EBUSY=16、EEXIST=17、ENFILE=23、EMFILE=24、EINVAL=22、EPIPE=32、ENOSYS=38、EOVERFLOW=75、ETIMEDOUT=110、EINPROGRESS=115；其余已纳入errno见15的固定全集，不能把原生负编号直接当errno。

| POSIX场景 | 固定映射/副作用 |
| --- | --- |
| fork/spawn实例/请求/准备记录或预算不足 | EAGAIN；实际页不足ENOMEM，权限EACCES；spawn直接返回正错误号 |
| exec预算/元数据/页不足 | ENOMEM；嵌套替换准备BUSY为EAGAIN扩展；格式ENOEXEC，参数E2BIG |
| open/dup本地fd满、FS File满、本地关闭记录/映射元数据满 | 分别EMFILE、ENFILE、ENOMEM；均在发布前失败 |
| read/write已确认部分量 | 复制交付成功则短返回；未知进展/对端失效EIO，不自动重放；非法本地fd EBADF |
| close摘除前原表项被handler替换 | EBADF，指原代次已无效，不关闭替代项 |
| close摘除后对端无法确认 | EIO且fd不恢复；被捕获信号且未完成EINPROGRESS，继续后台收尾 |
| poll控制查询故障/底层断连未能形成可信项 | EIO扩展；资源准备不足EAGAIN，用户缓冲EFAULT；不伪造正常0 |
| wait无匹配/暂时无结果 | ECHILD / WNOHANG返回0；已消费status输出失败EFAULT，不重取 |
| nanosleep剩余量输出失败 | EFAULT优先于EINTR；睡眠已经中断，不重建；成功不写rem |
| 原生NOTSUP/NOSYS/DEPTH/DEADLOCK | ENOSYS或接口规定的ENOTSUP / ENOSYS / EAGAIN / EDEADLK；不全称死锁 |

成功不保证清零errno。SA_RESTART逐接口按11/04处理；无未执行证明不重发，同一业务序号查询不等于重新执行业务。信号前已经提交的fork/PID、wait消费、资源安装、recv交付保持原结果；copy输出失败不承诺回滚这些副作用。

## 固定输出与复制失败

原生sigaction一次复制Sigact输入并固定可选oldptr输出页后，完整校验、交付旧动作并原子替换；原生坏输出不修改动作。libc公开struct的编码转换是另一层，应用oldact复制失败允许返回EFAULT且动作可能已经安装，须通过查询确认，不隐式回滚覆盖handler的新动作。这是明确取舍，修正旧文“任何POSIX坏输出都不会安装”的过强承诺；完整POSIX审计另列15。

create、forkcommit等关键结果走寄存器。posix_spawn先核对pid输出范围，成功发布后仍可能因用户映射变化复制失败；本系统返回EFAULT正错误并保留已发布孩子，可经wait收取，绝不重复创建或谎报不存在。SDK将此列为扩展失败边界；不强求通用用户内存固定事务来隐藏它。

## 执行与验收

- [ ] 对grant/observe输入建立正反授权表：SOURCE/CHILD/FUTURE只接受所持REQUEST，MONITOR只接受显式固定CONTROL QUERY；0、错误类别、其他主体同值整数、组/集合/域CONTROL及缺权均不扩大目标范围。
- [ ] observe固定目标/可选PID不匹配、目标exec/退出、授权撤销与登记的交错保持唯一固定Image；失败输出0且无观察占用，已登记cookie不自动跟随新image。

- [ ] 同类原生错误按接口转换：waitpid非阻塞/poll正常超时为0，fd表满EMFILE、spawn直接错误号；成功不强制清零errno。
- [ ] 提交前失败、交付后丢回复、部分进展、用户EFAULT和控制查询故障不自动重放未知副作用，不伪报0/字节数，公开失败后仍持清理责任。
- [ ] 最终errno表逐接口登记来源/阶段/副作用/返回形式/重试条件，未审计映射保持候选，不以资料或骨架检查宣称完整POSIX兼容。

- [ ] recv/reply_recv返回内核固定的source/image，提交后exec不重定向旧请求；同PID新镜像不能访问旧会话，SEND的身份数据不授予REQUEST观察权。
- [ ] 两架构t0/x9输出约束、全陷阱现场保存、阻塞恢复和信号嵌套覆盖；EVENT/回复/管理/错误的image为0，无旧值泄漏，Msg仍32字节。

- [ ] 两架构create返回status/Handle/PID，configure/start/destroy仅返回status；立即/阻塞完成一致，失败结果清零，IPC元数据解析不得用于管理结果。
- [ ] 固定结构坏地址/跨页失败/未知保留位在发布前拒绝；libsys无裸create/start导出及libc errno依赖，替换libc不重复定义桩符号。

- [ ] 逐入口形成名字、授权、合法状态、输入/输出、提交点、失败资源归属表后再分配编号；目标配置关闭功能返回不支持，不改变编号或布局。
- [ ] 普通fork/spawn/exec/wait及sh管道启动不要求init代办；内核不包含服务操作码/ELF解析/fd语义，不实现被删除的OP和退出二次确认入口。
- [ ] 检查Handle类别不含OP，内部记录不外泄为授权；身份/管理权/观察权/结果收取权分别验证，四字短消息不因增加身份元数据而隐式扩宽。

- [ ] 各接口按共享语义区分参数/状态/冲突/容量/物理页不足，未知调用与非法参数不同；普通错误不泄漏敏感状态。
- [ ] POSIX 错误映射覆盖接口特定错误和部分完成，不将业务错误当通信错误、不以失联证明未执行。

- [ ] 分别注入未交付失败、已交付中断、执行后回复前崩溃，核对通信状态、业务结果和收尾记录，不重复执行副作用。
- [ ] IPC 成功携带业务错误时正确转换 errno；结果不确定不伪报成功，正常错误清零和 EINTR 收尾 Handle 例外均符合 ABI。

全部为待执行项；通用测试纪律见 [总览](00-overview.md#执行与验收)，实际结果记录在 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 阶段 05：最小用户态与 ABI

依赖：04。产物：用户态启动入口、保存现场、系统调用桩、最小诊断及 exit。

- [ ] 固定寄存器 ABI、错误码和用户地址边界；内部指针不能作为返回值泄漏。
- [ ] 建立两架构最小 crt，暂用内嵌用户探针，不依赖 fs。

测试方法：用户程序反复设置全部通用寄存器哨兵并陷入/返回；测试未知 syscall、溢出参数和有指针接口的坏地址。
用户探针触发非法指令及非法访问，另一个探针或内核诊断仍可继续。

验收：返回值和保留寄存器符合 ABI，用户异常不会成为内核异常；非法请求不改变资源快照。


## 原生入口首个可运行切片（2026-11-15）

当前实现0x01 exit、0x02 yield、0x21 clock_gettime、0x22 clock_getres、0x30 alloc、0x31 free、0x32 map、0x33 unmap、0x34 seal和0x4f getid，编号在include/abi/syscall.h；这是当前profile，不是完整ABI实现。exit保存code低8位；其他已实现调用拒绝未用输入寄存器非0，ARM x7也须0。已知未接入口返回NOTSUP，0及表外号（包括未分配0x57）返回NOSYS，不通过一组空实现伪造服务。

入口以CPU当前Proc和其保留Hctx校验来源，不从用户参数选择资源表。Allocarg/Maparg先逐页预检并一次复制到可信内核快照，保留字段与未知映射位拒绝；用户不能传内核指针。成功alloc只输出本地MEM，其余普通返回字与image清零；错误所有输出清零。普通FP/SIMD和非指定整数输出保存，RV64 a7/ARM64 x8按既定规则解释。

当前native profile仅开放自身ALLOC及普通MEM/SPACE，已有可信Prep固定期限，但尚未预付完整Rq/Image准入，故CONSTRUCT/临时加载MEM即使来自可信引导安装，也不借这些普通内存调用绕过准入。0x40..管理流程和exec/IPC等仍按已知未实现机制NOTSUP。构造期ALLOC=0x10的已确认协议和可信实现见02/03，不等于0x40 create已可用。

syscalls场景使用三个独立U/EL0空间，通过真实ecall/svc运行，无测试陷阱替身：检查NOSYS/NOTSUP/INVALID/FAULT、失效Handle、未用输出清零、free后已有映射仍有效、unmap、CODE写别名阻止seal、封存后RW拒绝、实际执行动态生成代码；协作yield后核对各进程不同PC哨兵对应的FP/SIMD及控制状态。六次FIFO运行后两个exit42，第三个在unmap后真实访问旧VA产生FAULT/SEGV，三者均由CPU清理并领取各自Life结果。

尚无SDK/libsys桩或通用ELF装载，测试载荷是tests/kernel中的独立汇编。默认协作profile未开启timer；可信CPU入口可启用用户timer IRQ，内核C仅自身alloc受控窗口和CPU空闲开放IRQ，ARM缓存仍关闭，不将这些QEMU行为称为真实硬件缓存、性能、完整信号或时间片调度验收。


Rq可信核心补充（2026-11-16）：Msg和既定IPC常量已落头文件，私有Rqid/队列函数不作为用户接口；该可信核心切片当时0x10..0x1c仍NOTSUP，后续原生消息接续见下节。用户已确认CALL_CONTROL=0x08，只选自身控制清理两槽，不增加授权；无该位按内核信号深度选普通/对应信号槽，SEND拒绝该位，调用数保持73。


### 原生IPC寄存器接续（2026-11-16）

当前新增0x10..0x1a/0x1c，具体支持边界见04末尾；0x1b仍NOTSUP，connect/管理流程尚未接。共享SYS_*编号保持既定值；abi/ipc.h的C结构受__ASSEMBLER__保护，汇编载荷和C使用同一份标志常量。HAL IPC返回在普通7字之外处理t0/x9来源image及同步未收尾失败的原RPC，不扩Msg或更改调用寄存器。原生call的CALL_CONTROL=8已实测；高u64未知位拒绝，不截断。该消息切片当时事件mask为NOTSUP，后续事件支持见下节；资源接收/回复及尚未实现的信号路径仍明确NOTSUP，不把预装入口的真实IPC场景当作已有connect权限校验或SDK桩。


原生事件接续（2026-11-16）：新增notify(0x20)/alarmset(0x24)/alarmcancel(0x25)，recv/reply_recv支持匹配事件mask及EVENTS_ONLY；共享事件位见abi/event.h，EVENT返回按既定words/source/request/image布局。alarm当前要求可信CPU先启用timer profile，未启用为NOTSUP；call/send不会被普通事件中断。RQ_DONE/RQ_CANCEL为合并提示，不改变RPC查询/ACK协议。资源回复、CHILD/LIFE及原生管理路径仍待接，不改变其编号。


固定CONTROL/connect接续（2026-11-16）：0x55已接四输入authority/kind/rights/bits，成功a1/x1为本地Handle，其余输出0；未知/保留输入失败输出清零。kind CALL=5、SEND=6、NOTIFY=7。当前固定CONTROL CONNECT生成USE入口；产物带GRANT时还要求输入CONTROL的GRANT，缺位DENIED，CONSTRUCT仍NOTSUP；不接受PID，不增加系统调用编号。固定CONTROL可经free解除引用，不能据此销毁目标。再授予策略已由用户确认，见03末尾；固定CONTROL的grant/revoke及inherit接续见下文。


原生管理接续（2026-11-16）：0x46 grant六输入、0x47 revoke两输入及0x56 inherit两输入已接，均严格检查其余输入寄存器及未知高位；grant成功a1/x1为目标本地Handle，其他输出0，失败清零。当前仅固定进程CONTROL INSTALL/REVOKE选表，源另有GRANT；CONSTRUCT/EXEC_VIEW仍NOTSUP。inherit仅修改允许类型的本地保留标记，不新增GRANT或重绑定目标；NOTIFY可显式EXEC，私有MEM不行。当前实现与双架构反例详见03末尾，不能以这三个调用代表完整管理/构造准入或fork/exec已实现。


预算/终止接续（2026-11-16）：0x48 budget两个输入，32字节Budgetarg一次快照，max_instances/reserved为0，单位为页；固定CONTROL BUDGET只调整目标Acct，低于实占+预留BUSY。0x44 terminate一个输入，固定CONTROL TERMINATE提交强制退出；成功非自身时状态0且其他输出0，自身不再返回用户。同一合法已ENDED目标幂等0，失效句柄及未发布目标拒绝，既有首个终结原因保持。未用输入寄存器非0拒绝。当前无STOP/完整信号或集合POLICY分支，详见02/03末尾。


getcred接续（2026-11-16）：0x6d已接subject/pid/image，成功a1..a4/x1..x4分别为四个零扩展u32凭据，其他输出和t0/x9为0；失败输出清零。0/0/0自身、固定CONTROL QUERY及真正持有REQUEST的SOURCE/CHILD按03/10关系核验；FUTURE无实际exec准备时不成功，不从自报uid/PID补权。查询不消费REQUEST，DRAINING可读取，finish后旧句柄拒绝。可信测试构造凭据不代表原生CREATE身份策略已开放，细节见03末尾。


准备表查询（2026-11-16用户确认）：有效CONSTRUCT/EXEC_VIEW CONFIG允许query RESOURCE只读枚举它绑定的精确准备表，供父加载器将孩子实际SELF_SPACE/SELF_ALLOC Handle写入Boot。其他query kind拒绝，提交/取消/超期失效；仍核验源完整PID/image/表epoch及目标完整PID/image/表epoch，不增加调用号、不允许凭PID选表。返回的目标本地Handle只供其所属主体使用，父不能拿到自身表中操作；枚举不授予ALLOC/MAP/PUBLISH或CONTROL QUERY。代码接入与验收状态见03末尾。


RESOURCE query接续（2026-11-16）：0x73五输入已接RESOURCE=5，Queryrec公共定义/偏移断言及QUERY_*常量在abi/proc.h，汇编可包含常量。成功a1/x1为实际条数，a2/x2为下一游标，其他输出及t0/x9为0；失败清零。该切片当时仅自身0或固定CONTROL QUERY；后续CONSTRUCT CONFIG只读RESOURCE接续见03末尾。游标按下一Local索引16位+代次43位编码，stamp为该条Local代次；不含目标权限、不能从cursor中的索引选其他主体。输出区间按请求容量整体预检，最多16条，快照未用字段0。其他已冻结种类的接续及当前NOTSUP/DENIED边界见03末尾。


LIFE/ACCT query接续（2026-11-16）：已接kind=4/3的自身或固定CONTROL QUERY单条快照，image=0、next_cursor=0；LIFE stamp为状态序号，ACCT stamp为账目代次，二者均不作为目标授权或全内容版本。data前6字按已有表，未用字0；LIFE的wait_pending对应尚有父收取责任，不表示必有终态可收取。ACCT的set字为启动清单集合索引诊断标签，不能拿来管理/创建集合。旧账目已结清返回0条；读旧Life不会按复用Proc槽找到新账目。详细生命周期及验证见02/03末尾；已确认准备期CONFIG只允许RESOURCE，不允许ACCT/LIFE。


RQ query接续（2026-11-16）：kind=2已接，记录pid/image对应所查主体的匹配端，stamp为本条Rq索引/代次，next为下一可见条目；不授予RPC/REQUEST或跨主体权。data前7字仍按已有表，status以s64位模式存入data[5]，未终结不据0声称已成功；flags仅低4个既有责任/取消位，其余字0。RQ阶段/标志的唯一公共常量源为abi/ipc.h，所有内部通知/恢复/队列标志及消息内容均不输出。自身限定当前镜像且排除已完成的接收责任，固定CONTROL QUERY可看其Life保留历史，具体范围及验证见04末尾。

信号预付接续（2026-11-16）：共享Sigact48布局和已冻结kind/flags已落在abi/signal.h，不代表sigaction可用。Sigframe前缀offset48已由用户确认改为单调cookie，Sighead64布局已落地，见11末尾；实际用户帧核验及sigreturn仍未实现。


原生创建接续（2026-11-16）：0x40..0x43已接，create只用a0/a1，configure只用a0..a2，start/destroy只用a0，其他输入必须0；失败全部输出清零。Createarg/Config完整区间一次复制，create先核验本地CREATE类型再读属性，凭据与额度核验见03末尾。SERVICE能力决定额外a3 CONTROL，非SERVICE为0；PID不是目标操作权。CONSTRUCT/加载MEM的已冻结操作不再因旧profile一律NOTSUP，而按精确scope/rights/阶段检查；creds中仅CONFIG的ALLOC现在为DENIED。固定CREATE不属于free允许类型，返回BAD_HANDLE；尚未实现的CREATE通用grant在具体复制操作返回NOTSUP，不以泛化检查封锁已实现的create。EXEC_VIEW、非零reserve_resources及信号配置等未完成部分仍明确拒绝，不从调用号存在推断可用。

原生wait接续（2026-11-16）：用户确认新增ERR_CHILD=22。没有匹配且可收取的孩子返回-CHILD；存在匹配活孩子但尚无结果时，WNOHANG成功且全部输出0，否则阻塞。selector=0在受理时冻结当前组，不能编码为-PGID而与PGID1/任意孩子混淆；INT64_MIN返回INVALID。当前仅实现EXITED结果，WUNTRACED/WCONTINUED接受但尚无STOP/CONT结果源。接入及验证边界见03末尾。

观察原生入口接续（2026-11-16）：0x50..0x54已按既定五/一输入接通，未用寄存器仍必须0，失败输出全0。共享常量为abi/observe.h；cookie为u64，不能按Handle类别或有符号PID解释。obsread/ack/drop可操作本镜像既有cookie而不再要求旧permit/basis仍有效，登记仍先检查两者。RESOURCE查询已识别OBSERVE子类型9，只输出类型/业务权与本地Handle，不暴露内部预付区或Image索引。精确配额/退出清理及本地验证边界见10末尾。

libsys桩接续（2026-11-16）：用户态raw入口仅是私有汇编封装，不增加内核通用调用号。RV a7输入调用号与输出最后业务字均被正确保存，ARM x7输入归0、x9输出镜像；status按C返回、其余8字直接写64字节结果区。sys/channel.h的Ipcmsg以布局断言匹配，错误携带RPC也保留。__sys_enter和__sys_*声明只在SDK private/syscall.h；普通应用使用公开ipc_*或后续Unix API。完整库对象合并审计无外部运行时符号，两架构真实独立C ELF已验证部分原生接口；其余运行库/信号适配不因桩存在而自动完成，范围见08末尾。

用户态装载桩接续（2026-11-16）：libsys私有__sys_create/configure/start/destroy/getcred已复用既定寄存器ABI，返回仍是独立原生状态/输出，不增加内核ELF入口或公开裸创建API。普通用户装载器通过已有CONSTRUCT/CONFIG RESOURCE得到孩子真实SELF Handle，Boot补齐只用于目标自己的表；父临时MEM释放不改变孩子已持有映射。实际实现与失败/清理责任见08末尾，POSIX错误转换与FS稳定来源尚未接。

原生copy接续（2026-11-16）：0x03三输入已接，长度0成功且不检查地址；1..65536对本主体源READ/目标WRITE普通用户映射检查，地址/权限/设备FAULT、物理字节重叠INVALID、VM忙BUSY、超长INVALID。无句柄输出，未用输入必须0，失败输出0；共享上限在abi/mem.h。__sys_copy是私有libsys适配，不是memcpy替代或文件I/O协议，详细同步/别名和验证边界见08末尾。

资源回复前置接续（2026-11-16）：configure的第三参数0..8现已实际预付/替换镜像保底，超过8 INVALID，空间不足CAPACITY/代次耗尽OVERFLOW且原Config/现场不变。CALL_RECV_MEM/CALL_RECV_CALL现能在原调用者表预留一槽，两者同时置位INVALID，普通reply或终结按原槽来源归还，ACCEPTED协作取消不提前归还；来源/计数与验证见04末尾。

开放问题：Resreply字段/56字节布局已经冻结，但资源reply在a1..a4的指针位置正文未写定。已向用户提出REPLY_RESOURCE时a1/x1为descriptor_ptr、a2..a4=0、a5=REPLY_RESOURCE、a6=0，reply_recv仍不支持交付；尚未确认，不先实现。原生资源reply当前继续NOTSUP，不将可接受资源CALL选项误称为资源已安装。

类型化资源核心接续（2026-11-16）：Resreply的既定56字节字段/偏移已有共享结构与布局断言；内核内部提交可生成RESOURCE_REPLY kind=4，words[0]为已安装本地Handle、其余三业务字不解释，source/request/image均0。同步CALL和异步结果查询已能转出这个kind；RQ_FINISH不返回安装结果。用户服务仍不能通过原生reply发起资源交付，因为描述符指针寄存器位置未获确认，REPLY_RESOURCE仍NOTSUP。04末尾的可信桥只验证同一提交核心/真实接收者，不能当成已冻结候选输入位置。

信号配置原生接续（2026-11-16）：0x60三输入、0x61两输入、0x62零输入已接，未用输入仍必须0，成功action无额外寄存器输出，sigmask输出旧mask、sigpending输出真实pending；错误输出0。共享支持编号/掩码/方式常量在abi/signal.h。当前只安装DEFAULT/IGNORE和CHLD NOCLDWAIT，HANDLER与尚未实现语义的已知flags返回NOTSUP；KILL/STOP动作修改及未知位INVALID，KILL/STOP不进入有效掩码。旧动作输出FAULT时不安装，重叠new/old先快照输入。configure非零sigmask/sigdefault已支持并从创建基线计算；完整通知/递送与标准适配尚未完成，见11末尾。

原生sigreturn接续（2026-11-17）：0x65仅一个frame输入，其余输入必须0；成功直接恢复完整用户现场，不返回普通状态/不再次推进PC；坏输入或帧验证失败不可恢复SIGSEGV退出。私有libsys提供NORETURN __sys_sigreturn，实际帧/权限/cookie和验证见11末尾。HANDLER原生注册和信号来源/中断重启尚未开放，不将此入口称为完整信号ABI可用。

原生sigsuspend接续（2026-11-17）：0x63一个mask输入，其他输入必须0；未知mask位INVALID且原掩码不变，KILL/STOP位规范清除。保存原M后原子安装临时W、检查可递送pending并登记PROCESS_WAIT_SIGNAL=6；捕获后先固定INTERRUPTED返回现场，沿原C栈退至统一出口建立handler H帧，帧restore_mask取M。正常sigreturn恢复M后原调用返回-INTERRUPTED，不返回成功，不因普通IPC通知/事件假醒；默认终止不返回。两层满额时捕获信号仍pending，默认终止可越过满层的较低编号捕获信号；STOP/CONT真实源尚未接，不能由此声称已验其状态语义。
