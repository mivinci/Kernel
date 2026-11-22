# POSIX 源码兼容范围

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：设计规格已逐项修订；基础C运行库、身份/wait包装的局部实现与双架构验证见文末及status。其余范围继续执行，不声称完整POSIX、性能或CI验收。

## POSIX 接口范围与验收清单（设计目标，尚未实现）

本节是明确列举的 POSIX 源码兼容子集，不是完整接口目录或合规声明。对照基准为 POSIX.1-2024；实现每项前须核对规范原文的原型、选项、错误与边界行为。未列出的接口和选项不自动纳入本阶段。
下表是目标清单，逐项实际交付以文末和status为准，不能将部分实现视作整行完成。“本阶段”表示计划交付，“暂不支持”表示当前兼容范围之外，“原生扩展”不属于 POSIX 承诺。
软件需使用本系统 SDK 重新编译；不承诺 Linux 二进制兼容。基础静态单线程子集先验收，网络接口随 net 阶段交付，不阻塞串口启动。

| 状态 | 接口/头文件范围 | 语义与边界 | 测试与验收方法 |
| --- | --- | --- | --- |
| 本阶段 | main、exit、_exit、atexit；stdlib.h/unistd.h | csu 构造 argc/argv/environ；退出码、清理与直接退出分开；退出不隐含持久化 | 参数可修改、回调逆序、正常/直接退出差异、后端失联及退出资源守恒 |
| 本阶段 | errno.h：errno、错误常量；string.h：strerror | 逐接口使用规定返回方式；不是所有接口都返回 -1；成功不保证清零 errno | 错误值/返回值表驱动测试，信号处理保存恢复 errno；posix_spawn 等直接返回错误编号 |
| 本阶段 | string.h、stdlib.h、stdio.h 的基础子集 | memcpy/memmove/memset、strlen/strcmp/strchr、malloc/calloc/realloc/free、strtol/strtoul、整数 printf/snprintf 为起始集合；不是完整 ISO C 库 | 重叠拷贝、零长度、算术溢出、分配失败保留旧块、格式长度/截断和有界输出；补充函数逐项登记 |
| 本阶段 | getenv/setenv/unsetenv、environ、getopt | 环境存于进程自身，spawn 显式传递；参数解析不依赖 fs | 空值、覆盖/删除、子进程环境隔离及非法名称 |
| 本阶段 | open/openat/creat/close；fcntl.h | 基础访问模式、O_CREAT/O_EXCL/O_TRUNC/O_APPEND/O_NONBLOCK/O_CLOEXEC；mode 与 umask；目录 fd 相对解析 | 并发创建排他、截断权限、符号链接与相对目录、错误/取消/服务故障回滚 |
| 本阶段 | read/write/pread/pwrite/lseek | 短读写、EOF、偏移只推进实际完成量；pread/pwrite 不改偏移；追加定位和写入须原子提交；不可定位后端报错 | 两进程共享打开状态、独立打开、并发追加、部分完成后信号、负偏移及溢出 |
| 本阶段 | dup/dup2、fcntl 的 F_DUPFD/F_GETFD/F_SETFD/F_GETFL/F_SETFL | fd 标志独立，文件状态标志及偏移随 File 共享；dup2 同号校验后不变，替换不暴露中间空槽；FD_CLOEXEC 与打开标志分开 | 同号/无效源、目标已开、表满、信号重入、共享偏移/非阻塞状态及 spawn 后引用计数 |
| 本阶段 | stat/fstat/fstatat/lstat、access、umask、chmod/fchmod | 定义 mode/uid/gid/nlink/size/时间；真实与有效凭据检查按接口区别；access 不能代替实际 open 授权 | 链接本身/目标差异、权限位、拒绝后无副作用、超限大小/时间表示 |
| 本阶段 | mkdir/mkdirat/rmdir、unlink/unlinkat、rename/renameat、link/linkat、symlink/readlink | 原子命名修改、硬链接引用计数；禁止目录硬链接；跨文件系统操作按接口报错；已打开被删除文件仍可用 | 覆盖重命名、非空目录、跨挂载、环形符号链接、最后链接删除及崩溃恢复 |
| 本阶段 | opendir/fdopendir/readdir/closedir；chdir/fchdir/getcwd | 目录流自身状态和 fd 生命周期明确；cwd 为节点引用；readdir EOF 与错误可区分 | 目录增长/删除、重命名 cwd、缓冲不足、目录流关闭及引用回收 |
| 本阶段 | ftruncate/truncate、fsync/fdatasync | 文件长度、扩展零读、截断后偏移规则；区分数据同步与完整文件同步，不能把 write/close 当同步 | 扩展/截断/覆盖、脏页错误、断电模拟及已同步范围恢复 |
| 本阶段 | pipe、poll；poll.h | 有界字节流、PIPE_BUF 内写入原子性、EOF/EPIPE/SIGPIPE、非阻塞；poll 为状态观察而非资源预留 | 满/空管道、最后读写引用关闭、多写者、短写、订阅竞态、HUP/ERR/NVAL 与 fd 复用 |
| 本阶段 | posix_spawn/posix_spawnp 及 file_actions/attr 基础接口；spawn.h | 文件 open/close/dup2 动作按顺序执行，PATH 搜索、信号掩码/默认动作及进程组属性；支持标志逐项公布；不等同原生 spawn | 重定向/管道、CLOEXEC 处理顺序、PATH/环境、每一步失败、退出状态与资源回滚；不支持属性明确拒绝 |
| 本阶段 | wait/waitpid、sys/wait.h 状态宏 | 等待子进程/指定组，WNOHANG/WUNTRACED/WCONTINUED；区分正常退出、信号终止和停止继续 | 多子进程、重复等待、事件先后/确认、组长先退出、无匹配子进程及信号中断 |
| 本阶段 | getpid/getppid/getpgrp/getpgid/getsid、setpgid/setsid、kill/raise | pid_t 与 Handle 同为有符号 64 位，正 PID 沿用 PROCESS_ID 身份；0、负组 ID 和特殊值仅按接口解释；组/会话对外编号与内部 Handle 分开，授权核对完整实例 | PID 分配/复用与陈旧内部引用、组信号、权限拒绝、会话边界、kill(pid,0) 存在/权限检查 |
| 本阶段 | sigaction/sigprocmask/sigpending/sigsuspend、sigemptyset/sigfillset/sigaddset/sigdelset/sigismember | 支持信号和 SA_* 标志逐项冻结；KILL/STOP 特例、pending 合并、原子换掩码并等待、EINTR/重启按接口处理；不宣称全部信号功能 | pending 与睡眠竞态、屏蔽/解除、默认动作、嵌套及上下文恢复、异步安全允许表、SA_RESTART 场景 |
| 本阶段 | tcgetattr/tcsetattr/tcflush/tcdrain、isatty/ttyname、tcgetpgrp/tcsetpgrp；termios.h | 行/原始模式、VMIN/VTIME、回显和特殊字符；前后台组检查；支持 termios 字段逐项列举 | 阻塞组合、模式切换、排空/刷新、后台读写、Ctrl-C/Z、前台切换与挂断 |
| 本阶段 | clock_gettime/clock_getres、nanosleep；time.h | CLOCK_MONOTONIC 与 CLOCK_REALTIME 分开；明确实时时钟来源/未校时策略、分辨率、溢出与剩余睡眠时间 | 两架构频率换算、单调性、信号中断剩余时间、校时不改变单调截止时间 |
| 本阶段 | sysconf/pathconf/fpathconf、limits.h、sys/types.h | 报告实际容量/选项和规定的“不确定/不支持”结果；统一 off_t/time_t/ssize_t/pid_t 等 ABI，pid_t 沿用已定 Handle 表示，其他类型不借用其布局 | 宏与运行查询一致、边界溢出、两架构布局断言；不虚报 _POSIX_* 功能版本 |
| 本阶段（net 阶段） | socket/bind/connect/listen/accept/send/recv/sendto/recvfrom/shutdown、getsockname/getpeername、getsockopt/setsockopt、getaddrinfo/freeaddrinfo；sys/socket.h 等 | 先 AF_INET 的 TCP/UDP 与明确选项，阻塞/非阻塞/poll 一致；DNS 错误使用 EAI_*；不承诺 AF_UNIX、IPv6 或所有 socket 选项 | 回环/双端、连接失败、部分发送、半关闭、UDP 边界、名称解析、超时取消和服务重启 |
| 本阶段设计已冻结，未实现 | fork、_Fork、execve 及基于它的 exec 包装 | fork 父返回子PID/子返回0、私有内存隔离与共享File；_Fork跳过注册钩子并提供信号安全路径；exec保留PID替换静态ELF、成功不返回、失败保留旧镜像；继承/IPC按03/04/08冻结规格 | 父子隔离/共享偏移、handler内_Fork、在途结果、逐步失败、身份不变及资源守恒；未实现不得宣称兼容 |
| 暂不支持 | vfork、system/popen | vfork 有共享地址空间/父暂停专门语义，不因fork纳入而自动支持；system/popen 待fork/exec及shell具体契约成立后另行列入 | SDK 准确报告缺失，不用同名桩冒充可用，也不把vfork实现为未经说明的别名 |
| 暂不支持 | pthread_*、线程 TLS、线程同步接口 | 当前每进程单线程；errno 访问方式为将来隔离留边界不代表已有线程 | 多线程配置在构建/移植检查中明确拒绝 |
| 暂不支持 | POSIX mmap/munmap/mprotect/msync、shm_open、命名信号量、文件/记录锁 | 原生整段 map/unmap 不满足文件映射、部分解除/保护修改等语义；共享内存机制不是这些接口的替代实现 | 原生测试与兼容测试分开，不暴露不完整同名实现来通过探测 |
| 暂不支持 | 完整实时调度/优先级、实时信号排队、POSIX timer_*、完整 locale/宽字符/浮点数学集合、动态加载 | 不在本阶段完整承诺内；具体增加项须单独列语义和测试；静态链接不是 POSIX 接口支持的替代指标 | 选项宏/SDK 清单准确，软件依赖审计能报告缺口 |
| 原生扩展 | Boot、Handle、sys/channel.h 的 ipc_*、__sys_* 私有入口、资源授权/IRQ/DMA/管理接口 | 不属于 POSIX；Handle 保持完整代次身份，__sys_* 不作为应用稳定 API | 独立运行时无需自研 libc 可启动/IPC/退出；服务权限、取消收尾和陈旧 Handle 验证 |

上述组内列举为第一批接口；尤其 stdio、termios、信号集合、spawn 属性和 socket 选项不得以一行分组声称整套完成。实现前为每个符号/标志补充原型、责任模块、支持状态及对应测试用例，记录在本节或其紧邻表格，不另建重复权威清单。

错误与重试的权威边界见 [分层错误与映射](06-abi.md#错误层次与重试判定语义收口)：原生机制、业务结果与标准返回形式分别处理；poll控制故障EIO仍为本项目方案，不属于已完成的标准审计。每个接口须记录部分进展与副作用是否可确认，不能全局遇EINTR/EAGAIN就重放。

补充本阶段接口：utimensat/futimens（sys/stat.h），语义与测试见 [文件时间戳](10-filesystem.md)。支持显式 atime/mtime、UTIME_NOW/UTIME_OMIT 和对应权限检查，ctime 不由应用指定；符号链接及 flags 按接口逐项冻结。当前未实现。

补充本阶段接口：clock_settime（time.h），仅支持受明确校时授权限制的 CLOCK_REALTIME；clock_getres 返回实际分辨率。时间来源/可信校准状态属于原生扩展，不因成功设置时间自动成立。具体契约及用例见 [时间接口](05-events.md)，当前未实现。

`unistd.h` 的 `long sysconf(int name)` 首先支持 `_SC_PAGESIZE` 与同义 `_SC_PAGE_SIZE`。返回目标内核构建采用的基础虚拟内存页字节数（RV64、ARM64 均为 4096），成功保留 `errno`。未知选择器返回 `-1/EINVAL`；其他配置名暂不导出，不报告尚未交付的功能。`pathconf` 和 `fpathconf` 待接。

stage282：两架构 QEMU `runtime` 的五个正常启动用例均验证两个名称的返回值、成功时 `errno` 保持不变以及未知选择器的 `EINVAL`；格式检查通过。

stage286：`string.h` 提供 `strdup` 和 `strndup`。返回可独立修改、由 `free` 释放的 NUL 结尾副本；`strndup` 最多读取指定字节数，长度为零时返回空字符串的副本。无法分配时返回 `NULL` 并设置 `ENOMEM`。两架构 QEMU `runtime` 验证原串修改不影响副本、复制无 NUL 的有界输入和零长度，格式检查通过。
libc 导出标准 API，libsys 实现 fd/连接/服务适配和负错误码协议；POSIX 层按每个接口转换错误，原生 UNKNOWN 等结果必须定义 POSIX 可表达的失败映射，不能泄漏私有返回值。共享缓冲仍待真实完成后复用。

基础文本补充登记（2026-11-21，责任模块均为libc/string.c、公开于string.h；代码已接入，验收见文末）。以下原型采用项目C89声明形式；复制/拼接的非重叠、有效存储及容量前提仍按规范，不因省略restrict拼写变成重叠复制接口。所有函数在有效输入上保持errno，无分配、I/O或库全局状态；不是检查任意应用指针并返回EFAULT的系统接口。

| 符号与原型 | 返回/边界 | POSIX.1-2024原文 |
| --- | --- | --- |
| `void *memchr(const void *, int, size_t)` | unsigned char比较；首匹配即停止，可查NUL后的二进制数据 | [memchr](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memchr.html) |
| `size_t strnlen(const char *, size_t)` | 至多读取maxlen，未见NUL返回maxlen | [strnlen](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strnlen.html) |
| `int strncmp(const char *, const char *, size_t)` | 最多n字节、NUL处停止；首不同字节按unsigned char决定符号 | [strncmp](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncmp.html) |
| `int strcoll(const char *, const char *)` | 当前仅支持 C locale，排序与 `strcmp` 一致 | [strcoll](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcoll.html) |
| `size_t strxfrm(char *, const char *, size_t)` | C locale 的排序键为原串；返回不含 NUL 的长度，有空间容纳完整结果时才写入；容量为零可传 NULL | [strxfrm](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strxfrm.html) |
| `char *strrchr(const char *, int)` | 最后匹配，可返回结尾NUL，未找到NULL | [strrchr](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strrchr.html) |
| `char *strstr(const char *, const char *)` | 第一个完整子串，空pattern返回原基址 | [strstr](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strstr.html) |
| `size_t strspn(const char *, const char *)` | 初始全在接受字节集合中的长度 | [strspn](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strspn.html) |
| `size_t strcspn(const char *, const char *)` | 初始全不在拒绝集合中的长度 | [strcspn](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcspn.html) |
| `char *strpbrk(const char *, const char *)` | 第一个属于集合的源字节，未找到NULL | [strpbrk](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strpbrk.html) |
| `char *strcpy(char *, const char *)` | 复制含NUL，返回目标原基址 | [strcpy](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcpy.html) |
| `char *strncpy(char *, const char *, size_t)` | 写满n字节，不足补NUL；截断时不保证NUL，返回原基址 | [strncpy](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncpy.html) |
| `char *strcat(char *, const char *)` | 从目标NUL追加含NUL源串，返回原基址 | [strcat](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcat.html) |
| `char *strncat(char *, const char *, size_t)` | 最多追加n个源字节，另写NUL，返回原基址 | [strncat](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncat.html) |

环境修改接口登记（2026-11-21，libc/envset.c，stdlib.h；实现/验收见文末）：`int setenv(const char *name,const char *value,int overwrite)`复制参数，非零overwrite替换、零overwrite且已存在时保持环境和指针不变；无名/含`=`为EINVAL，分配失败ENOMEM且环境不变。`int unsetenv(const char *name)`删除匹配，未找到成功且环境不变，非法名称EINVAL；实现不分配，不产生额外ENOMEM路径。二者会更新environ并可能使先前getenv结果失效，属于单线程、非异步信号安全接口；有效输入成功时本实现保持errno。直接NULL名称/值额外拒绝为EINVAL，不把这些无效C输入当作规范有效用例。原文：[setenv](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setenv.html)、[unsetenv](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unsetenv.html)。
新增硬链接、追加原子性、PID 选择器与组号协议、实时时钟与标准属性等是待补齐设计，不因本表存在而视为原状态机已覆盖；阶段 00 冻结具体契约后进入对应实现阶段。

验收分三层：host 测试检查纯库逻辑/布局/错误转换；RV64 与 ARM64 QEMU 测试真实服务和故障路径；选取有明确版本的外部单线程静态程序重新编译运行，登记补丁与缺失接口。三层结果分别记入 status.md，不以自编小程序替代外部软件兼容结论。
每个纳入接口至少覆盖正常、边界、权限/无效参数、资源耗尽、信号/取消及适用的服务故障路径；不适用项标明原因。系统容量限制通过公开查询或明确配置报告，超限失败不得泄漏资源。

规范参考：[POSIX.1-2024](https://pubs.opengroup.org/onlinepubs/9799919799/)、[函数索引](https://pubs.opengroup.org/onlinepubs/9799919799/functions/contents.html)。初次在线正文访问返回403；后续成功取得的逐符号原文与核对范围登记在对应条目/验收中，不能将本表视为整套标准已审计。基础机制实现或部分函数通过不等于完整标准兼容。

## 中断规则的审查索引

以下仅索引主题权威规格。首版决策与数值已冻结，接口全部未实现；标准核对、host实现测试和两架构QEMU属于后续验收，不再把已选方案列为等待用户决定的问题。

| 接口组 | 权威位置 | 固定选择/实际待验收 |
| --- | --- | --- |
| read/write | [信号](11-terminal.md)、[文件协议](10-filesystem.md) | 零进展才按RESTART继续，部分量保留，未知结果EIO；需测服务结果快照与取消交错 |
| open/close | [fd/关闭](10-filesystem.md) | 发布前关闭预留，旧代次变化EBADF，摘除后不恢复；需测池满/重入/失联 |
| waitpid | [进程](03-process.md) | 原接受时固定PGID，未收取重查、已收取只输出；需核对外部标准及坏输出 |
| poll | [分组查询](10-filesystem.md) | 1秒控制查询、整体EIO扩展、不自动RESTART；需测timeout0调度延迟及外部事件循环 |
| nanosleep/sigsuspend | [时间](05-events.md)、[信号](11-terminal.md) | I/D固定rem、EFAULT优先，mask按活动帧恢复；需测嵌套和FP现场 |
| fork/_Fork、execve、posix_spawn | [进程](03-process.md)、[运行库](08-runtime.md)、[启动](07-boot.md) | CPU克隆完整端点递送handler，嵌套exec EAGAIN，exec后端独立关闭、子返回/新csu不批量接入；PREP_NS仅约束未发布准备，spawn发布后故障不重建；需测长路径/期限/输出FAULT |
| 原生 IPC | [Rq矩阵](04-ipc.md)、[ABI](06-abi.md) | call/rqwait中断均只撤等待，已提交call返回原RPC；rqcancel接收后协作取消不丢实际结果，recv已交付不重取；需验证完整引用/硬件入口 |

执行与反例验收留在各权威主题末尾；本索引不重复维护待办。Austin Group #529最终接受文本只提供close局部证据，不能据此宣称整套POSIX通过。

## 首版兼容取舍与errno全集（已冻结）

本轮授权后，之前待选行为均选为当前首版目标：poll控制故障整体EIO且timeout0可有有界控制延迟；close采用明确fd状态的EINTR/EINPROGRESS；exec用后端独立逻辑关闭+新csu本地初始化、连接按需接入，存在跨服务调度窗口；SIGCHLD只影响后续退出，不追溯删除历史结果；嵌套exec准备冲突EAGAIN；spawn发布后用户PID输出复制FAULT保留孩子。它们是准确公布的行为/限制，不等于POSIX.1-2024合规认证，不将无法取得标准正文当作实现时随意猜测的理由。标准核对与外部软件场景列为交付验收，基础硬件/内存实现不等待网络/图形接口审计。

第5项审查修订：fork/_Fork子返回前只做本地修复，exec/spawn启动不等待全部后端接入，首次实际操作领取已有目标引用的连接；接入失败是当前操作的明确错误，不因STOP或原10秒准备期限杀死已发布进程。继承文件引用与CLOEXEC独立清理仍须保持，完整标准逐符号核对尚未完成。

第7项容量边界：每客户端镜像/后端镜像共用一条连接，普通/两层信号各有独立页对。库内页被旧请求占用、或handler遇外层接入中时，普通read/write/open/poll可报告暂时EAGAIN；不等待被中断外层或增建连接。close不以这类资源不足返回EAGAIN：摘除后按EIO及预付后台责任收尾，不恢复fd或假报后端成功。实际页/映射不足仍按ENOMEM，本项不构成完整POSIX错误合规证明。

首版SDK errno固定：EPERM=1、ENOENT=2、ESRCH=3、EINTR=4、EIO=5、ENXIO=6、E2BIG=7、ENOEXEC=8、EBADF=9、ECHILD=10、EAGAIN=11、ENOMEM=12、EACCES=13、EFAULT=14、EBUSY=16、EEXIST=17、EXDEV=18、ENODEV=19、ENOTDIR=20、EISDIR=21、EINVAL=22、ENFILE=23、EMFILE=24、ENOTTY=25、ETXTBSY=26、EFBIG=27、ENOSPC=28、ESPIPE=29、EROFS=30、EMLINK=31、EPIPE=32、EDOM=33、ERANGE=34、EDEADLK=35、ENAMETOOLONG=36、ENOLCK=37、ENOSYS=38、ENOTEMPTY=39、ELOOP=40、EOVERFLOW=75、ENOTSOCK=88、EDESTADDRREQ=89、EMSGSIZE=90、EPROTOTYPE=91、ENOPROTOOPT=92、EPROTONOSUPPORT=93、EOPNOTSUPP=95、EAFNOSUPPORT=97、EADDRINUSE=98、EADDRNOTAVAIL=99、ENETDOWN=100、ENETUNREACH=101、ENETRESET=102、ECONNABORTED=103、ECONNRESET=104、ENOBUFS=105、EISCONN=106、ENOTCONN=107、ETIMEDOUT=110、ECONNREFUSED=111、EHOSTUNREACH=113、EALREADY=114、EINPROGRESS=115、ESTALE=116、ECANCELED=125；EWOULDBLOCK=EAGAIN、ENOTSUP=EOPNOTSUPP。数值不作为Linux二进制兼容承诺；定义网络错误常量不代表网络已经实现。

Unix公开整数类型：pid_t/off_t/time_t/ssize_t为s64，size_t/uintptr_t为u64，uid_t/gid_t/mode_t/socklen_t为u32，sigset_t为u64；两架构一致。struct timespec为s64 tv_sec、s64 tv_nsec（16字节），纳秒规范0..999999999；stat由10的128字节固定字段适配。termios公开结构与80字节Termarg同宽，flag命名映射到协议位，不直接把其他系统头的数值带入。

open标志首版采用O_RDONLY=0、O_WRONLY=1、O_RDWR=2、O_CREAT=0x40、O_EXCL=0x80、O_TRUNC=0x200、O_APPEND=0x400、O_NONBLOCK=0x800、O_CLOEXEC=0x80000；未列标志EINVAL。FD_CLOEXEC=1。poll POLLIN=1、POLLPRI=2（首版无紧急数据源）、POLLOUT=4、POLLERR=8、POLLHUP=16、POLLNVAL=32；负fd忽略，ERR/HUP/NVAL即使events未请求也报告。文件权限/时间/路径符号使用本表类型和10明确错误，不将只定义名字当已支持选项。

扩展阶段边界：net仍选lwIP NO_SYS raw模型、input/win/dis/PTY仍按12/13分工，串口MVP不需要冻结它们的每个socket选项/窗口消息。首版ABI中已为这些服务保留普通用户态协议演进空间，不提前添加内核网络/窗口对象；其实现前按同一设计验收流程冻结扩展协议，不以占位接口冒充本轮交付。

本地 `fcntl` 命令采用 `F_DUPFD=0`、`F_GETFD=1`、`F_SETFD=2`，只用于 libc 调用，不进入文件协议。`F_DUPFD` 从指定下限选最低可用编号，清除新 fd 的 `FD_CLOEXEC`；下限越界返回 `EINVAL`，范围内无空位返回 `EMFILE`。`F_GETFD` / `F_SETFD` 只操作当前有效的 fd，`F_SETFD` 只保留本 SDK 支持的 `FD_CLOEXEC`。

## sh 对齐范围与缺口（2026-11-17）

T4 的 sh 以 POSIX.1-2024 sh 与 dash 行为为准，至少覆盖：引号与转义、参数/变量展开（含默认值与长度）、算术展开、命令替换、here-doc、函数、`case`/`if`/`while`/`for`、管道、输入输出与 fd 重定向、命令分组与子 shell、背景任务、通配展开、内建命令（`cd`/`echo`/`export`/`set`/`unset`/`read`/`eval`/`exec`/`trap`/`umask`/`type`/`wait`）、`$?`/`$!`/`$$`/`$#`/`$@`/`$*`，以及作业控制（`jobs`/`fg`/`bg`、作业号与进程组）。

当前全部未实现，所需宿主能力缺口如下：

| 能力 | 归属 | 状态 |
| --- | --- | --- |
| 原生 fork / exec | 03 | fork 三调用 NOTSUP；exec 未实现 |
| pipe / dup2 / 完整 fd 表 | 10 | 仅设计 |
| 文件与目录（含通配所需目录枚举） | 10 | 未实现 |
| 作业控制 STOP/CONT、前台组、控制终端 | 11 | 部分设计，STOP/CONT 未接 |
| 真实信号来源（SIGINT/SIGQUIT/SIGTSTP/SIGCHLD） | 11 | 来源编码待确认，生产未接 |
| 环境与 argv 传递 | 08 | 已实现 |

不以本里程碑声明 POSIX shell 或完整 POSIX 已达标。

## 执行与验收

stage447：`int isatty(int fd)` 由 libc 提供，声明在 `unistd.h`。按 [POSIX.1-2024 isatty](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isatty.html) 核对返回约定：以原 fd 的 `GETATTR` 查询判定，成功返回 1 并保留 `errno`，失败返回 0 并沿用 `tcgetattr` 错误。管道返回 ENOTTY，关闭后的 fd 返回 EBADF。信号中断沿原请求取消和收尾；未自动重启时返回 0/EINTR。

ARM64、RV64 QEMU `pipeio` 经真实 IPC 验证成功判定、管道、已关闭 fd，以及服务挂起查询时捕获信号，原请求取消后可再次查询。格式检查通过。成功判定仍由测试服务提供属性。

stage446：`int tcgetattr(int fd, struct termios *attributes)` 由 libc 提供，头文件为 `termios.h`。按 [POSIX.1-2024 tcgetattr](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetattr.html) 核对原型、成功返回 0、无效 fd 的 EBADF 和非终端的 ENOTTY。首版公开字段对应 11 篇的标志、控制字符和输入/输出速率，布局与 `Termarg` 一致。libsys 固定原 File，通过同一连接发送 `TTYCTL/GETATTR`，只在原请求完成且序号、返回值和 80 字节长度匹配后写入应用对象。管道后端经会话校验后返回 ENOTTY；坏输出地址返回 EFAULT，格式错误的回复返回 EIO。请求沿原账本处理取消与回收。

ARM64、RV64 QEMU `pipeio` 经真实 IPC 验证夹具返回属性、坏长度保留输出、坏输出地址后可再次查询，以及管道 ENOTTY 和无效 fd EBADF。17 项请求账本主机测试和格式检查通过。成功属性由测试服务提供，生产 TTY 文件连接仍待接入。

全部为待执行项；通用测试纪律见 [总览](00-overview.md#执行与验收)，实际结果记录在 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 兼容清单执行项

依赖：各接口对应的内核、运行库和服务能力；按接口逐项验收，不等待全表一次完成。

- [ ] 逐符号核对规范，补原型、标志、错误、责任模块及用例；先细化 PID 选择器/组号协议、追加写、spawn 属性和时钟缺口。
- [ ] 建立目标 SDK 和能力报告，不虚报 _POSIX_*；按本篇表格运行 host 与两架构 QEMU 测试。
- [ ] 选择明确版本的外部单线程静态软件，登记构建参数、依赖、补丁和实际场景结果。

验收：每项分别记录设计/实现/本地验证/CI 状态；支持项有正向与失败路径证据，缺失项可明确识别，不以同名桩或自编探针替代外部兼容结论。

### C入口与基础库本地交付（2026-11-16）

已接csu→libc→libsys的main、可修改argv/environ、exit/_exit/atexit/getenv；errno访问器、冻结错误常量/别名、strerror及基础内存/字节字符串函数已实现。atexit预付32个用户槽；退出期新增登记失败EBUSY、满表ENOMEM，递归exit走直接退出作为首版防重入行为，不据此承诺可移植递归退出语义。没有stdio/文件后端时退出不伪造刷新/fsync；它们的完整故障收尾仍待实现。

CASE=runtime的双架构真实main、构造/清理次序、参数/环境副本、启动拒绝与资源守恒及host证据见08末尾。这里仅是本表C入口/基础库切片；POSIX I/O、逐接口原生错误转换、fork/exec、信号安全、分配器/stdio及完整合规均未因此完成。

### libc进程身份与wait包装（2026-11-16）

新增sys/types.h的pid_t=s64（保留完整PROCESS_ID及特殊选择器），unistd.h的getpid/getppid和sys/wait.h的wait/waitpid及状态宏。每次身份查询都调用原生getid，不缓存父PID；真实PID1返回getpid=1/getppid=0，其他实例退出后被init接管的活孩子可立即读到父1。原生当前实例身份查询不应失败，包装不创造一个标准未规定的普通错误返回；不可达内核错误以运行时trap结束。

waitpid将0/正完整PID/负组/-1选择器直接交原生wait，先拒绝未知options，返回正PID/0/-1分别保持原语义。ERR_CHILD、无匹配的BAD_HANDLE/STALE映射ECHILD，INTERRUPTED映射EINTR，INVALID映射EINVAL，NOTSUP/NOSYS映射ENOSYS，VM冲突BUSY映射EAGAIN，内部意外状态EIO；不把原生负编号直接写errno。wait等价于waitpid(-1,status,0)。成功不清errno，WNOHANG无结果不读取或写入status，即使status本身无效也不因未执行复制而失败；NULL status正常收取并忽略状态。

原生寄存器结果一旦交付已消费，包装先在局部固定该结果并编码，再用__sys_copy写4字节status，非法/只读输出返回-1/EFAULT。绝不重试wait、恢复旧结果或领取另一个孩子以补偿输出；其他内核/服务旧引用继续独立存活。正常退出采用code低8位放status高8位；信号终止取信号号和CORE位，FAULT不泄漏为标准status额外位；STOPPED/CONTINUED的编码和宏已实现，但当前内核没有真实STOP/CONT事件源，不宣称已支持其端到端行为。

WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG/WIFSTOPPED/WSTOPSIG/WIFCONTINUED按同一状态编码定义，输入表达式各求值一次。这些状态数值属于本SDK，不承诺Linux二进制兼容。POSIX官方wait页面本次访问未成功取得内容；此交付依据已冻结接口和项目状态编码，不声称已完成POSIX原文逐条审计或标准合规。完整信号中断/重启、errno保存恢复和异步安全验收仍待后续真实信号路径。

验证：elf两架构真实用户父以常规waitpid检查WNOHANG的status不变与errno保留，收取真实csu/main孩子的退出42，再以只读Boot作为另一孩子status输出得到EFAULT；下一次waitpid/ECHILD，证实已收取结果不复活。init场景使用常规getpid/getppid核对PID1/父0和活孤儿父1。6项新增host覆盖全部状态编码（含信号/FAULT/CORE及模拟STOP/CONT）、NULL status、输出故障后的消费、错误转换、选择器/选项及重复父查询；模拟结果不冒充内核未实现的信号/STOP事件。本地完整253项host、格式及92个两架构QEMU通过；最后宽度断言后两架构elf和6项host再验通过，日志build/host/stage38-libc-wait/。未执行CI或真实硬件标准验收。

管理强杀状态验证接续（2026-11-16）：init真实用户场景新增两次CONTROL terminate阻塞/已唤醒未恢复的组wait父进程，常规wait取得SIGNALLED结果并核对WIFSIGNALED/WTERMSIG=9；其活孩子/已结束未收取孩子均由init正常wait退出22/11。此前信号状态编码的host证据增加了这条真实管理强杀结果，但sigsend、捕获信号及STOP/CONT仍未实现；详细窗口/引用守恒见03末尾。

整数转换子集接续（2026-11-17）：stdlib.h现提供C locale的strtol/strtoul，错误/范围/endptr及已验证子集见08末尾。292项完整host和两架构runtime专项通过，每架构5个真实main共275组/550次转换；内核、locale、分配器、stdio、fork/exec和文件/网络接口的完成度不由此推定。证据build/host/stage47-integer-conversion/，未执行CI/完整标准合规验收。

内存格式化子集接续（2026-11-17）：stdio.h现声明已实现的snprintf/vsnprintf，支持整数/指针/字节字符串与字符，格式/范围/截断/错误边界及证据见08末尾。300项完整host、格式及双架构runtime通过，每架构5个真实main共120项格式检查；浮点、宽字符、位置参数、%n、流printf/FILE和完整stdio未实现，不能从内存输出推定文件/网络接口可用。

组/会话用户接口接续（2026-11-17）：getpgrp/getpgid/getsid/setpgid/setsid已接，保留完整64位PID及原生自/子同会话限制，逐操作错误和验证见08末尾。getpid/getppid/wait/waitpid仍沿既有路径；kill/raise、真实信号源/作业控制、fork/exec与完整POSIX未由本次补齐。

上述组/会话切片最终完整回归通过306项host、格式及98个两架构QEMU，runtime每架构105项用户检查和CPU独立关系核验通过；证据build/host/stage49-libc-groups/。没有把尚未接通的exec/信号分支称为端到端验收。

### 基础分配器子集（2026-11-17）

stdlib.h现声明并实现malloc/calloc/realloc/free，16字节对齐、溢出/后端失败为NULL及ENOMEM，realloc失败保留旧块；成功分配/free保留errno。零长采用最小分配，realloc(p,0)也不隐式释放，调用者仍须free返回指针；不提供零长字节访问承诺。构造函数可用，不要求文件/网络服务在线；不是异步信号安全接口。批量/多Mem映射、失败隔离、主机与真实双架构验收边界见08末尾；未接fork/_Fork Fix，不把此子集称为完整ISO C/POSIX、第三方分配器兼容或mmap支持。

### 信号集合与等待前端（2026-11-17）

新增signal.h及64位sigset_t，实际提供sigemptyset/sigfillset/sigaddset/sigdelset/sigismember、sigprocmask/sigpending/sigsuspend。set=NULL查询忽略how、KILL/STOP不可屏蔽、集合编号错误EINVAL、原生/指针错误转errno，实际捕获等待返回EINTR；对象助手要求有效C存储，调用路径不依赖分配器或stdio。mask旧输出FAULT保留已提交变更，不逆向覆盖handler后续状态。原生HANDLER已接，公开POSIX sigaction/trampoline、来源code转换、sigsend/kill/raise及普通等待重启仍未完成；Native/可信生产者验证边界与证据见11末尾，不将这些子集叫完整POSIX信号。

### 基础字节串扩展（2026-11-21）

stage166按正文逐符号登记接入memchr、strnlen、strncmp、strrchr、strstr、strspn、strcspn、strpbrk、strcpy、strncpy、strcat、strncat。实现只访问调用者有效C对象，按字节/NUL工作，不解释Unicode，不分配或引用库全局状态，有效输入保持errno。比较首不同字节按unsigned char决定符号；memchr遇首匹配即停止，strnlen不越maxlen；strncpy写满n且截断时可无NUL，strncat最多复制n个源字节再另写NUL，复制/拼接返回原目标基址。重叠复制、容量不足或任意非法指针不属于这些函数的有效输入，不能把它们当受检查I/O copy使用。

规范证据已实际取得上述12份Open Group Issue8/IEEE1003.1-2024原文，核对SYNOPSIS、DESCRIPTION、RETURN VALUE与ERRORS，包括Issue8新增的有效输入errno保持要求。web读取被403拒绝后，公开页面经正常浏览器UA直接GET成功；URL/原始HTML SHA256及文本保存在build/host/stage166-string/standards/，没有把2017重印冒充本点2024逐项核对。原文同页的stpcpy/stpncpy等其他符号不因此列为已实现。

新增6项host在O0/O2以符号重命名避免宿主libc替代被测函数，用确定性随机字节参考覆盖无符号顺序、有界/零长、首/末位置、空/重复集合与子串、截断/补零及两侧哨兵。runtimeuser每个普通有效main执行24组断言，两架构各5次、每架构120组，含一页真实映射后紧邻未映射页的边界读写：无NUL的strnlen/strncmp界限、memchr首字节已匹配而n=SIZE_MAX、NUL处停止、精确末页复制/追加与errno保持。私有测试trace扩大到128字节，读取/SDK导出/观测一致更新；受限CREATE/3槽装载孩子只执行不分配部分，原资源槽满的检查保持。

新增测试和函数使ARM runtimeuser需37镜像页，RV为33页。旧ELF成功fixture只给64页且还承担16页栈、Boot与内核元数据，ARM实际返回LIMIT；诊断已保存原loader_error。成功fixture现按plan.pages+64提供明确测试余量，exec上限128和elfcheck最多64镜像页保持，专门的16页不足预算场景不变；修正没有放宽内核预算，也没有吞掉装载错误。最终两架构各2次装载成功和4种失败回滚通过，原失败串口/日志保留。

1095项完整host、格式、28次RV64/ARM64 QEMU通过，其中runtime/elf/heap/normal共8次、启动包布局20次，12份journal核对。实际运行的runtime载荷与当前SDK逐字节一致，string.o无未定义依赖、12符号/头文件导出和原生调用证明已审计。ARM init及两架构serial完整ELF与stage165相同；RV init完整哈希不同，但用上轮保留启动包提取的原ELF核对，入口、PT_LOAD地址/权限/文件与内存大小/全部载荷字节完全一致，差别仅在非装载内容。clangd切回RV64/runtime。证据build/host/stage166-string/的unit、focused-initial、场景-final、elf-arm64-observe/budget-reviewed、standards、原RV init提取件、verify.py和evidence.json。未重跑完整195矩阵（最近stage165），无CI/板端/性能或外部程序兼容结论；文件/终端/公开信号/Fix、sh/net/图形GPU及最终GUI仍待继续。

### 环境变量修改与快照所有权（2026-11-21）

stage167在独立envset.c接setenv/unsetenv，未调用的程序不因CSU/getenv绑定而被拉入环境分配路径。setenv先校验名称，再为完整新指针表/字符串快照计算有溢出检查的空间；复制全部保留条目及name/value后才发布environ，再释放上一份自有快照。输入可借旧getenv值，应用直接改写environ也可借先前自有字符串，不在复制前free。overwrite=0且存在时不分配、环境指针不变，失败ENOMEM不改指针/值。人为重复名称在覆盖时保留首出现位置并合为一条；不引入新的环境总长/条数限制，受实际内存容量约束。

unsetenv不分配，从首个匹配洞向前压缩可写指针表，移除全部同名项，缺失时原指针不变；不free借用字符串。自有快照可包含被删字符串，剩余值继续有效，下一次setenv复制后整体替换，环境全空时释放自有块。malloc/free失败语义沿既有分配器，不通过I/O、RPC或新的内核接口修改环境。接口非异步信号安全、不承诺线程安全或旧getenv指针跨修改继续可用；NULL名/值的EINVAL是额外无效输入拒绝，有效输入的标准错误与行为仍按已登记原文。

实际取得setenv/unsetenv两份POSIX.1-2024原文并核对参数复制、覆盖0、无匹配删除、错误和环境保持。8项host用受控真实宿主分配包装验证调用参数副本、ENOMEM时精确旧环境不变、旧块参数别名、外部environ借旧块值、删除不分配、非法名/重复及1024次覆盖仅一份当前块（构建时峰值两份），最后空环境归零。首次shim缺宿主abort声明导致0测试编译失败，补声明后专项及完整host通过，未放宽被测实现，原失败日志保留。

两架构各5次runtime main执行20组、每架构100组环境断言，实际malloc/free支持覆盖/删除/空值/非零overwrite/旧值别名/借用environ/64次覆盖和恢复启动环境。3槽CREATE孩子先由原资源检查证明无可用MEM管理槽，再调用setenv实际取得ENOMEM且环境保持，未为测试补发授权或改资源上限。原文本120组/页边界、ELF两次成功/四种失败、堆/启动包和文件创建死亡恢复都继续通过；这里没有实际fork继承环境或外部shell兼容结论。

1103项完整host、格式、28次双架构QEMU通过：runtime/elf/heap/normal共8次、启动包布局20次，12份journal已核对。SDK/头/库/实际嵌入载荷、setenv/unsetenv原生调用及envset.o仅errno/environ/malloc/free和纯文本依赖已审计；默认程序入口/所有装载段与stage166一致，两架构serial完整ELF保持。clangd切回RV64/runtime。证据build/host/stage167-environment/的unit、focused-reviewed、场景-final、standards及sources.json、上轮init提取件、verify.py和evidence.json。最近完整195矩阵仍为stage165，无CI、板端或性能结果；getopt、文件/终端/公开信号/Fix及sh/net/图形GPU/最终GUI目标继续。

### raise与kill边界（2026-11-21）

stage171已接公开raise：当前单线程运行库按完整自身PID发送支持的非零信号；捕获同步返回、屏蔽合并、忽略、两层嵌套、自我KILL及STOP/实际另一进程CONT已在双架构验证。raise(0)选择Issue 8 pthread_kill允许的EINVAL分支，不改变kill的空信号语义。4项O0/O2主机专项、1135项完整host、格式、8次QEMU及8份内核诊断journal、SDK/真实载荷/生产隔离证据见11末尾及build/host/stage171-libc-raise/。

这不是完整公开信号层：原生安装的测试handler/trampoline不替代公开sigaction、标准siginfo转换、Rctx/Fix或SA_RESTART。公开kill未接：原生仅存活Run的范围与POSIX尚未收取僵尸的存在/权限语义冲突，扩大到僵尸Life及256项快照的候选已提出待用户确认，未实现受限临时封装或静默改变既定授权。原文与候选见11基础语义；整体POSIX/sh/net目标未完成。

stage306：`string.h` 与 libc 提供 C locale 的 `strcoll`、`strxfrm`，可用于后续路径展开结果排序。RV64、ARM64 QEMU `runtime` 验证高位字节的比较和排序键结果一致、空串、零容量查询、恰好容纳终止符，以及容量不足时保持缓冲和 errno；格式检查通过。路径通配及其他 locale 仍待实现。

stage307：libc 的 `<fnmatch.h>` 提供 C locale 字节模式匹配，供后续 `sh` 的 `case` 和路径展开使用。支持 `*`、`?`、反斜杠、方括号集合、范围和字符类，以及单字节排序元素和等价类。`FNM_PATHNAME` 限制通配符跨越斜杠，`FNM_PERIOD` 要求显式匹配组件首点，`FNM_NOESCAPE` 保留反斜杠字面含义。多星号匹配使用迭代回退，无需递归、分配或文件服务。RV64、ARM64 QEMU `runtime` 覆盖这些边界，主机对四组标志进行了 365040 次组合比对，格式检查通过。目录枚举、展开结果收集和 shell 引号处理仍待接入。
