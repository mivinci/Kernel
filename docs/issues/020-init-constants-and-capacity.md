# 020 init 的常量、资源 tag 与启动包容量靠约定维持

状态：待处理　优先级：P2　关联：014、015、016

## 现象

`sbin/init` 有三处相邻问题，本质相同：**靠约定而不是靠结构保证一致性**。

1. **常量一半宏、一半字面量**：`SERVICE_RING_LO`、`SERVICE_STRIDE`、`SERVICE_TX_OFF` 定义在 `service.h`，但 ELF 装载 scratch 地址 `0x2000000` 是 `service.c` 里的**字面量**。
2. **资源 tag 用算术生成**：`packet->resources[2 + i].tag = tty ? ttytags[i] : SERIAL_INIT_CALL + i;` —— tty 用显式数组，serial 用"起始值 + 序号"，同一个概念两种表达。
3. **启动包容量是当前最大值的巧合**：`struct Packet { Boot header; Resentry resources[7]; u64 arguments[25]; }`，其中 `7 = 2 个固定 + max(serial 5, tty 4)`，`7` 与 `25` 都没有名字。

## 影响

- 调整地址布局时，宏改一处、字面量漏一处，两块区域重叠；表现为加载后随机错乱，极难定位。
- tag 用算术生成意味着**新增或重排资源会让编号随位置漂移**，对端必须复刻起始值与对齐规则。
- 增加第三种服务（net 需要 mmio、irq、rx 环、tx 环、notify 等）时，`resources[7]` 会越界；越界方式是**结构体溢出**而非编译错误。
- `STATIC_ASSERT(sizeof(Packet) == 568)` 只锁住总大小，不锁住"每种服务的资源数不超过容量"。

## 建议方案

1. 所有地址布局常量集中到 `service.h`（含 scratch 地址），`service.c` 中不保留字面量；并加静态断言确认环区与 scratch 区不重叠。
2. serial 也建显式 tag 数组（与 `ttytags[]` 同形），删掉 `SERIAL_INIT_CALL + i` 的算术；两种服务使用同一种表达。
3. 定义具名容量 `SERVICE_MAX_RESOURCES`（及 `SERVICE_MAX_ARGUMENTS`），`Packet` 用它定长；并对每种服务加静态断言"该服务资源数 ≤ 容量"，使超限在**编译期**失败。
4. 若某种服务的资源数确实会超过当前容量，应在同一次改动中把容量提上去，而不是让结构体在运行期溢出。

## 验收

- 全文件检索不再出现地址字面量；人为交换环区与 scratch 区的定义顺序后，静态断言能报错。
- serial 与 tty 的资源 tag 都由显式数组给出，新增一个资源只需改数组，不影响其他编号。
- 人为把某种服务的资源数加到超过 `SERVICE_MAX_RESOURCES`，构建即失败（不是运行期越界）。
- 双架构 QEMU 的 serial/tty 启动与 `packageboot` 场景不回归。

## 备注

- 三处改动互不依赖，可分别提交；合计工作量小于半天。
- 与 014 相关但不同：014 讨论"发现与授权不应单例化"，本条只处理常量、tag 与容量的表达一致性。
