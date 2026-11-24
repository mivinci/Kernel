# 网络服务

[设计总览](00-overview.md) · [实现状态](../status.md) · [执行与验收](#执行与验收)

状态：lwIP 已固定版本并完成两架构交叉构建；net 服务和网络运行验收尚未完成。

## net 与网络 fd（已确定）

socket 创建无路径 File 会话，fd 后端为 net 端点，不新增内核网络 syscall；用户态库/服务协议提供 socket/bind/listen/accept/connect/sendto/recvfrom/shutdown。
通用 read/write/close、非阻塞及 poll 复用 fd 契约；网络不支持的 seek/fsync 等操作明确拒绝，不把普通文件语义强套到端点。
TCP 为字节流，读写边界不对应且可短结果；UDP 保留报文边界，缓冲不足明确报告截断，不伪装为完整报文。
发送成功仅表示本地栈接受，非远端应用已处理；正常接收 EOF、连接复位和传输错误分别报告。
shutdown 控制方向，close 释放打开引用；继承 fd 共享端点状态，最后引用及在途协议状态按网络规则收尾。
connect/accept/I/O 使用异步和延迟回复，不能阻塞整个 net；监听可读意味着可 accept，连接可读包括数据/EOF/错误。
可写表示可接受一定量数据或连接已完成待查询，客户端仍检查连接结果及非阻塞 WOULD_BLOCK；就绪不是容量预留。
连接数、监听队列、收发缓冲和请求均有编译上限，拥塞采用明确背压/丢包策略；端口/监听/接口配置按可信凭据和策略授权，不凭 net IPC 句柄取得全部网络管理权。
首轮网络范围为以太网、ARP、IPv4、ICMP、UDP、TCP、静态地址配置，DNS 解析为用户态功能；DHCP/IPv6 等另行排期。
协议栈选型见下节，不能用一次 echo 成功代替协议正确性、重传/关闭/流控和资源验收。

## 网络协议栈选型（已确定）

net 复用固定版本的 lwIP（2.x 大版本已冻结，首版取 2.2.0），采用 NO_SYS=1 回调式 raw API，不自行重写 TCP，不使用依赖线程模型的 lwIP BSD socket/netconn API。
用户态 socket/fd 协议由运行库和 net 会话层提供，内部适配 raw 回调；lwIP 指针不作为外部句柄。
收包、请求、协议定时任务都由单一 net 事件循环推进，回调不得阻塞；协议任务接入已有逻辑定时器，不引入线程/邮箱。
连接、包和协议记录使用有上限的用户态池并计入预算；DMA 缓冲与协议包显式复制或引用交接，不提前复用，不承诺初版零拷贝。
第三方源码保留上游风格并固定版本，我们的适配层遵循本项目命名；集成前实测两架构工具链和 C89 兼容性，不把选型当作编译通过。
依据上游 [NO_SYS 配置](https://github.com/lwip-tcpip/lwip/blob/master/src/include/lwip/opt.h) 与 [API 模型](https://github.com/lwip-tcpip/lwip/blob/master/doc/doxygen/main_page.h)。

- [ ] 验证 TCP 分段/合并、短读写、流控、重传、连接建立失败、半关闭、复位及共享端点引用。
- [ ] 验证 UDP 消息边界/截断、监听队列满、非阻塞/poll 交错及连接/缓冲耗尽回收。
- [ ] 在两架构目标网卡实际收发并核对内容；无权限接口配置/端口操作、进程退出和 net 实例重建均不串会话。

## 会话、fd 与调用边界（2026-11-18）

socket 不创建内核对象：fd 表归 libsys，socket fd 与 file fd 共用同一编号空间（POSIX 语义），网络会话只是该 fd 的无路径后端；内核不新增网络 syscall，与 [10](10-filesystem.md) 的 fd 契约完全一致。

每会话在建立时获得一块预先映射的双向环（net 出资 `SHARED`，以 RW 授予应用，默认 16 KiB/4 页）。此后所有数据搬运只传环内偏移与长度，不再随每次调用交付资源；控制面使用四字 `Msg`，字段按定宽打包，不使用 packed 结构。

| op | 名称 | w0 | w1 | w2 | w3 |
| --- | --- | --- | --- | --- | --- |
| 1 | SOCKET | op\|domain(8)\|type(8)\|proto(8) | — | — | — |
| 2 | BIND | op\|connid(32) | addr[0..1] | port(16)\|flags(16) | — |
| 3 | LISTEN | op\|connid | backlog(16) | — | — |
| 4 | ACCEPT | op\|connid | — | — | — |
| 5 | CONNECT | op\|connid | addr[0..1] | port(16) | — |
| 6 | SEND | op\|connid\|flags(16) | bufoff(32)\|len(32) | — | — |
| 7 | RECV | op\|connid\|flags(16) | maxlen(32) | — | — |
| 8 | SENDTO | op\|connid\|flags(16) | addr[0..1] | port(16)\|bufoff(32)\|len(32) | — |
| 9 | RECVFROM | op\|connid\|flags(16) | maxlen(32) | — | — |
| 10 | SHUTDOWN | op\|connid | how(8) | — | — |
| 11 | POLL | op\|connid | events(16) | — | — |

`ACCEPT`/`CONNECT`/阻塞式 `RECV` 使用异步延迟回复：net 记录待决请求并在事件循环推进后回复，不阻塞整个服务。等待期间收到信号时回复 `-INTERRUPTED`，由 libsys 按 [11](11-terminal.md) 的 `SA_RESTART` 规则决定重启或返回 `EINTR`；该规则对 `connect`/`accept`/`recv`/`send` 一致，不因操作类型不同而静默改变。

net 只返回既有 `ERR_*`，由 libsys 映射 errno（`ECONNREFUSED`/`ECONNRESET`/`ETIMEDOUT`/`ENOTCONN`/`EAGAIN`/`EMSGSIZE`/`ENETUNREACH`）；不为网络新增内核错误码。

## 缓冲所有权与接收零拷贝（2026-11-18）

接收路径采用**栈级零拷贝**：驱动把 rx 槽位包装成 `PBUF_REF` 指向的负载交给 lwIP，lwIP 原地解析，不再复制进 pbuf 池。以 1500 字节段为例：

| 路径 | 设备→rx 槽位 | rx 槽位→pbuf | lwIP→应用环 | 软件拷贝 |
| --- | --- | --- | --- | --- |
| 全复制（未采用） | DMA | 复制 | 复制 | 2 |
| **栈级零拷贝（采用）** | DMA | **不复制** | 复制 | **1** |
| 端到端零拷贝（不做） | DMA | 不复制 | 不复制 | 0 |

**不做端到端零拷贝**：POSIX 的 `read`/`recvfrom` 定义就是把数据复制进调用者缓冲，取消该复制需要非 POSIX 接口和每会话槽位映射；首版明确不提供，也不留半成品接口。

槽位状态机：`IDLE → FILLED（设备写入）→ PARSING（被 PBUF_REF 引用）→ RETURNED`。规则：

- 每槽一页（4 KiB），1500 MTU 足够，不协商 mergeable rx；默认 256 槽（1 MiB），计入 net 的 Acct。
- pbuf 的 `custom_free` 是**唯一**释放入口；槽位在 `PARSING` 期间不得回到设备队列。
- **强制复制模式（隔离兜底）**：若某会话未释放的 pbuf 超过 8 个或 rx 环可用低于 25%，net 对该会话退化为“复制进会话环并立即归还槽位”，保证单个慢应用不能占死 rx 环拖垮全机。
- 缓存与 DMA：槽位属获授权 DMA Mem，交接期由“解析完”延长到“pbuf 释放”；同一维护单元内不得再有其它 CPU 写者。ARM 缓存启用后，CPU 解析前 invalidate、归还设备前 clean，统一在 `rx_slot_release` 内执行，遵循 [09](09-devices.md) 的同步与停止契约。
- 发送路径仍全复制：从会话环复制进驱动 tx 缓冲，`send` 返回即可复用该段环内缓冲，但按 12:12 仅表示本地栈已接受，不表示对端收到。

## lwIP 集成参数（2026-11-18；版本已冻结，其余候选值须实测调整）

| 项 | 首版值 | 说明 |
| --- | --- | --- |
| 版本 | **冻结 lwIP 2.x 大版本**，首版取 2.2.0 上游 tag | 第三方源码保留上游风格与版本号，适配层按本项目命名；仅接受同一大版本内的修复级升级，跨大版本须重新评审 |
| 模型 | `NO_SYS=1` raw API | 不使用 `netconn`/BSD socket，不引入线程与邮箱 |
| TCP | `TCP_MSS=1460`、`TCP_WND=4*MSS`、`TCP_SND_BUF=4*MSS`、`TCP_SND_QUEUELEN=16` | 窗口与发送队列有界，满时按背压节处理 |
| PCB | `MEMP_NUM_TCP_PCB=16`、`MEMP_NUM_UDP_PCB=16`、`MEMP_NUM_TCP_SEG=64` | 与会话上限一致 |
| pbuf | `PBUF_POOL_SIZE=32`、`MEMP_NUM_PBUF=32` | 池只服务 tx/控制包；rx 走 `PBUF_REF` |
| 关闭项 | `LWIP_IPV6=0`、`LWIP_DHCP=0`、`LWIP_SOCKET=0`、`LWIP_NETCONN=0`、`LWIP_RAW=0`、`IP_REASSEMBLY=0` | 与 12:17 的范围一致；不分片重组 |
| 开启项 | `LWIP_ICMP=1`、`LWIP_UDP=1`、`LWIP_TCP=1`、`LWIP_NETIF_LOOPBACK=1` | loopback 复用上游能力 |

`sys_now` 取 `CLOCK_MONOTONIC`（[05](05-events.md)），`sys_check_timeouts` 由 net 的 `alarmset` 以 10 ms 周期驱动，并在每次收包批次后补调一次；不引入独立线程。

## 事件循环与有界进展（2026-11-18）

net 主循环是单一阻塞等待点：等待 IPC 请求或事件位（rx 队列、tx 完成、协议定时器）。每轮唤醒后的工作量有上限：最多处理 32 个接收包、32 个发送完成、16 个待决请求，然后重新检查事件位再决定是否继续。

virtio-net 队列中断经 `IRQ_BIND` 投递为事件位；中断不可用或未启用时由 10 ms 闹钟兜底轮询。回调只入队，不做阻塞工作；协议超时、重传与关闭收尾都在循环内推进，不在回调里执行。

该有界推进是 12:44“持续流量时 sh 仍有进展”的实现依据，验收时须实测而非声称。

## 授权与容量（2026-11-18）

| 项 | 首版上限 | 授权 |
| --- | --- | --- |
| 每进程 socket 会话 | 16 | 普通进程可创建 |
| 全局会话 | 256 | 计入 net 预算 |
| 监听 | 全局 16，backlog ≤ 8 | bind 端口 < 1024 需 CONTROL 或清单授权 |
| 会话环 | 16 KiB（4 页）/会话 | net 出资，RW 授予应用 |
| rx 槽位 | 256（1 MiB） | 设备域 DMA Mem |
| 接口配置 | `lo` + `eth0` 静态 IPv4 | 仅 init 经清单/CONTROL，应用只读查询 |

不做 raw socket，不做 `SO_REUSEADDR`；重复 bind 返回 `EADDRINUSE`，不静默共享端口。

## 接口、loopback 与地址（2026-11-18）

接口表固定两项：`lo`（`127.0.0.1/8`）与 `eth0`（virtio-net，静态 IPv4/掩码/网关来自清单）。`lo` 始终存在，即使没有网卡，也不占用 DMA 槽位、不产生设备中断；它使单机自测与“无网卡功能可用”成为可验收路径。

ARP 表 8 项、老化 10 分钟；邻居未解析时最多排队 8 个包，超出丢弃并计数。不实现 DHCP、IPv6、IP 分片重组。

## 设备衔接：virtio-net 最小特性（2026-11-18）

按 [09](09-devices.md) 的现代 MMIO 协商与队列契约接入：只协商 `VIRTIO_F_VERSION_1`、MAC 与 STATUS；**不协商** mergeable rx、GSO、checksum offload、多队列。

link 状态来自设备 STATUS 的 `LINK_UP`：down 时新 `connect` 返回 `ENETUNREACH`，已建立连接保留数据但不再有新收发，恢复后继续；不做自动重连重放。设备复位必须先确认 rx/tx DMA 已停止（[09:20](09-devices.md#L20)）才能归还槽位，旧会话标记失效并以 `POLLHUP|POLLERR` 通知应用。

## 服务重启与会话失效（2026-11-18）

net 实例带代次。进程死亡后：所有 socket fd 的后续操作返回 `ECONNRESET`，`poll` 报 `POLLHUP|POLLERR`；旧 `connid` 在新实例中一律 `STALE`，不复用会话。

fd 的本地收尾不依赖 net 存活：`close` 只释放本地引用，net 侧状态由 init 按 [07](07-boot.md) 的恢复模式重建；已失效的 fd 不自动重建，应用需重新 `socket`。

## 示例：一次 TCP 连接的生命周期（2026-11-18）

参与者：应用（sh）、libsys fd 层、net、lwIP、virtio-net。初始状态：net 已 READY，`lo`/`eth0` up，应用已取得 net 端点与一块 16 KiB 会话环。

1. `socket(AF_INET, SOCK_STREAM)` → libsys 分配 fd 7 并 `CALL SOCKET`，net 返回 `connid` 并完成环授权；提交点在此，失败则 fd 立即回收。
2. `connect(7, 10.0.2.2:80)` → net 记录待发 SYN 并延迟回复。收到 SYN-ACK 后回复成功；收到 RST 回复 `ECONNREFUSED`；10 秒无果回复 `ETIMEDOUT`，会话回 `IDLE`。
3. 等待中收到 `SIGINT` → net 回复 `-INTERRUPTED`；libsys 依 `SA_RESTART` 返回 `EINTR` 或重启等待，会话状态不变。
4. `send(7, 5 KiB)` → 环足够，net 分两批复制进 tx 缓冲，返回已接受字节数（允许短写）；对端 RST 后下一次 `send` 返回 `ECONNRESET`，环内未发数据按语义丢弃且不重发。
5. `recv(7)` → net 以 `PBUF_REF` 原地解析、ACK 后把 payload 复制进会话环并归还槽位；应用拿到长度。若应用长期不读导致 pbuf 未释放达上限，net 转入强制复制模式，保证槽位归还。
6. `shutdown(7, SHUT_WR)` → 发送 FIN，仍可 `recv`；对端 FIN 到达时 `read` 返回 0（EOF），与 `ECONNRESET` 明确区分。
7. `close(7)` → 释放本地引用；若为最后引用则 `CALL CLOSE`，net 进入协议收尾由定时器推进（首版上限 30 秒），`close` 本身不阻塞等待对端。
8. 应用退出而未 `close` → 经 LIFE/observe 通知 net，按 `CLOSE` 处理；会话环与槽位引用一并回落，net 的 Acct 回到基线。

## 验收方法与主机对端（2026-11-18）

QEMU 使用 `-netdev socket`（本机选定的后端）：主机侧进程作为对端，实现 QEMU socket 帧格式（4 字节大端长度前缀 + 原始以太帧），从而逐帧精确断言与注入。harness 需先扩展 `tools/smoke.py` 接入 netdev 与对端进程；当前只添加 `virtio-rng-device`，没有任何网络路径。

- `CASE=netlo`：只用 `lo`，覆盖 TCP 回环、半关闭、UDP 边界与截断、fd 收尾；不依赖任何网卡。
- `CASE=netpeer`：对接主机对端，覆盖 ARP 解析、ICMP echo、TCP 三次握手、乱序与丢包注入、RST、链路 down/up。
- 断言方式：主机侧记录帧序列并核对字段；guest 把收发字节数与校验和（FNV-1a64）打印到串口，由主机比对，不以“打印 PASS”代替内容核对。
- 背压：对端故意不回 ACK，验证 `send` 返回 `EAGAIN`、强制复制模式生效、且 sh 在持续流量下仍有进展。

## 明确不做（2026-11-18）

IPv6、DHCP、`AF_UNIX`、raw socket、`SO_REUSEADDR`、GSO/checksum offload、多队列、mergeable rx、端到端零拷贝、IP 分片重组、端口过滤与网络命名空间。

## 执行与验收

静态 musl 运行基线就绪后即可接 socket/fd、poll 和网络 applet；启动时动态链接和 pthread 是独立的第一期验收项，不阻塞 net。

通用测试纪律见 [总览](00-overview.md#执行与验收)，完整执行与证据记录在本主题末尾，当前接续摘要见 [status.md](../status.md)。阶段编号是执行依赖，不要求与文档编号相同。

### 网络执行项

依赖：[事件](05-events.md)、[设备驱动](09-devices.md)、[运行库](08-runtime.md)、[fd 契约](10-filesystem.md)；网络不作为串口 MVP 前置条件。

- [ ] 验证目标网卡，集成固定版本 lwIP，冻结网络协议、选项和权限编码。
- [ ] 按正文实现 socket 会话、非阻塞和 poll、DNS 及有界缓冲收尾。

测试方法：host 协议测试及两架构真实收发，覆盖 TCP 半关闭/部分发送、UDP 边界、DNS 失败、连接取消和服务重启；持续流量时检查 sh 仍有进展。

验收：核对实际数据而非假设备回包；记录硬件矩阵和不支持选项，配额有界、旧会话失效、无缓冲提前复用。

上游源码已固定为 `lib/lwip` 子模块。从官方仓库核对 `STABLE-2_2_0_RELEASE` 指向 `0a0452b2c39bdd91e252aef045c115f88f6ca773`，并核对检出版本为 2.2.0。上游源码未修改。需要此依赖时运行 `git submodule update --init -- lib/lwip`。

`make net-stack ARCH=arm64|riscv`（分别指定架构）把原版 lwIP 的 IPv4、ARP、ICMP、UDP、TCP 与本地时钟适配编为 `libnetstack.a`。端口使用单事件循环配置，内部内存、回环队列和协议池具有编译期上限；时钟使用单调毫秒并按 lwIP 的 32 位规则回绕。两架构归档均编译成功，并与项目 libc/libsys 链接成无未解析符号的用户态 ELF。此处未启动 ELF；定时器推进、回环收发、socket/fd 与网卡均待运行验收。
