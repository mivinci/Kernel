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

CALL_ASYNC=1、CALL_RECV_MEM=2、CALL_RECV_CALL=4，后两位互斥；REPLY_RESOURCE=1且仅reply支持。RECV_NONBLOCK=1、RECV_EVENTS_ONLY=2；reply_recv只解释这两位接收选项，不支持资源混合交付。RQ_FINISH=1；ACK_RESULT=1、ACK_RELEASE=2，组合先整体检查，失败不部分修改。inherit FORK=1、EXEC=2；资源GRANT为bit63，其他低位按03子类型解释。sigmask how BLOCK=0、UNBLOCK=1、SET=2。READ=1、WRITE=2、EXEC=4。wait WNOHANG=1、WUNTRACED=2、WCONTINUED=4；结果EXITED=1、STOPPED=2、CONTINUED=3，code低8位为正常退出码或信号号，bit8=SIGNALLED、bit9=CORE（首版恒0）、bit10=FAULT；STOPPED为停止信号号，CONTINUED=0。完整故障原因在query保存；libc按kind/位编码标准wait status。

rqpoll/rqwait非消费；rqack只操作本主体RPC。rqcheck仅服务当前持有REQUEST，返回1（正常ACCEPTED）、3（ACCEPTED且cancel_requested）或4（强制终结/DRAINING）；不能读取他人请求。rqclean要求CONTROL覆盖已死亡服务且完成安全隔离责任已由可信恢复路径接管；不能将活服务或未知DMA直接标finished。即使调用者死代消费/释放，server_finished仍必须真实成立。

query kind固定PROC=1、RQ=2、ACCT=3、LIFE=4、RESOURCE=5、BOOT_RESOURCE=6，最多16条，每条Queryrec；cursor为索引+代次快照，0为首次输入及遍历结束输出；调用者依据next_cursor=0结束本轮，不将其再次当继续位置，不承诺跨条全局原子。主体无QUERY只可查自身非敏感统计；管理查询从CONTROL/SYSTEM授权范围枚举。kind选择仅查询记录，不是对象任意操作码。data字含义：PROC=[parent,pgid,sid,state,waitpid,cpu_ns,used,reserved,resource_count,map_count,rq_count,tty,...]；RQ=[caller,callerimage,server,serverimage,phase,result,flags,...]；ACCT=[set,limit,exec_limit,used,reserved,state,...]；LIFE=[parent,reason,code,wait_pending,observe_refs,cleanup_refs,...]；RESOURCE=[local,subtype,rights,offset,length,inherit,target_pid,target_image,...]；BOOT_RESOURCE仅init可读受信设备/启动镜像记录，其data=[resource,kind,start,length,irq,flags,...]。都不含内核指针/完整他人寄存器。

### query的主体选择（交叉复核修订）

query(0,kind,cursor,outptr,count)为明确的本主体查询：PROC/ACCT/LIFE只返回当前调用者自己的记录，RESOURCE只枚举当前镜像本地授权，RQ只枚举本主体作为caller或receiver实际持有的请求；不按cursor中的索引扩大到其他主体。BOOT_RESOURCE不接受authority=0。管理查询仍要求显式CONTROL QUERY或SYSTEM既定权限，只枚举其授权范围；普通CALL、PID、REQUEST等整数不能作为管理authority来补权。

cursor是相应kind/授权范围内的有界遍历位置，0开始，返回next_cursor=0表示本轮结束；不是目标PID或资源权限。count必须1..16（0或更大值INVALID），先整体验证count/输出范围，逐项保存稳定身份快照；不承诺跨项原子。库查新授CALL时用本主体RESOURCE路径，按完整local Handle匹配记录，再取target_pid/image；未找到或代次不符明确失败，不拿同槽新授权替代。此规则只补齐既有query的authority=0输入，不新增调用或放宽观察/安装权限。

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

grant的a0/x0必须是本次有效CONSTRUCT/EXEC_VIEW CONFIG，或绑定单一进程Life的CONTROL INSTALL。CONTROL QUERY=0x01、TERMINATE=0x02、BUDGET=0x04、IRQ=0x08、REVOKE=0x10、CONNECT=0x20、INSTALL=0x40；已有低位不移动，资源自身GRANT仍为bit63、只检查source的转授。revoke固定进程目标要求REVOKE，不能用INSTALL/QUERY替代。SERVICE创建返回的持续CONTROL包含INSTALL；普通create不多返回管理权。集合/域CONTROL不直接指定grant/固定监督目标，SYSTEM POLICY不替代所需目标句柄。

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

当前native profile仅开放自身ALLOC及普通MEM/SPACE，尚未预付完整Rq/准备期限，故CONSTRUCT/临时加载MEM即使来自可信引导安装，也不借这些普通内存调用绕过准入。0x40..管理流程和exec/IPC等仍按已知未实现机制NOTSUP。构造期ALLOC=0x10的已确认协议和可信实现见02/03，不等于0x40 create已可用。

syscalls场景使用三个独立U/EL0空间，通过真实ecall/svc运行，无测试陷阱替身：检查NOSYS/NOTSUP/INVALID/FAULT、失效Handle、未用输出清零、free后已有映射仍有效、unmap、CODE写别名阻止seal、封存后RW拒绝、实际执行动态生成代码；协作yield后核对各进程不同PC哨兵对应的FP/SIMD及控制状态。六次FIFO运行后两个exit42，第三个在unmap后真实访问旧VA产生FAULT/SEGV，三者均由CPU清理并领取各自Life结果。

尚无SDK/libsys桩或通用ELF装载，测试载荷是tests/kernel中的独立汇编。IRQ仍关闭、ARM缓存仍关闭，不将这些QEMU行为称为真实硬件缓存、性能、完整信号或时间片调度验收。
