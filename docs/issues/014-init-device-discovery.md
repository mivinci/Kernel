# 014 init 的设备发现与授权为单例硬编码

状态：待处理　优先级：P2　关联：07、09、12

## 现象

1. **设备发现仍限控制台**：init 已将 `QUERY_BOOT_RESOURCE` 返回的 UART 授权和参数绑定到清单中的 serial 实例，发权和参数构造共用该实例记录。查询仍只返回控制台 UART；第二个设备（如 virtio-net、framebuffer）尚无发现和发权路径。
2. **授权靠扫表推断**：`listener` 与 `observer` 由 `serviceinit()` 遍历 init 自己的资源表取得——按 `RESOURCE_CALL + RESOURCE_USE|RESOURCE_GRANT + count 1 + image 匹配` 和 `RESOURCE_OBSERVE + OBSERVE_MONITOR` 做模式匹配。同类资源出现第二个时，`if (listener) return -ERR_STATE` 会直接报错。
3. **argv 曾硬编码 UART 语义**：`arguments()` 现按服务类型构造参数；serial 和 tty 保留各自的原布局，新增类型须显式定义参数契约。10 项 init 主机测试及 ARM64、RV64 默认启动 smoke 已通过。
4. **创建授权已接通**：init 按 `Servicecfg.creator` 从真实 Boot 资源解析每个服务的 CREATE Handle，缺少时拒绝启动。当前清单仍只允许 `BOOT_CREATE` 和 `INITCFG_REQUIRED`，未扩展创建域或可选服务策略。

## 影响

- net 阶段拉起 virtio-net 服务、图形阶段拉起 dis，都卡在第 1 项——现在写不出"给第二个设备授权"的代码路径。
- 第 2 项的属性匹配在资源种类增加后会变成脆弱断言：正常配置可能被误判为错误，而错误配置可能恰好匹配上。
- 第 3 项使"加一个服务"从"改数据"退化成"改代码"，与清单驱动启动的初衷相反。

## 建议方案

1. **设备发现按清单条目查询**：需要硬件窗口的服务在清单里声明设备类别（UART / virtio-mmio 窗口 / framebuffer），init 按类取候选并逐个授权，不把实例固化在代码里。
2. **listener/observer 由启动包 tag 显式给出**（`BOOT_*` tag），不再靠属性扫描推断；扫描保留为**校验**手段而非发现手段——显式给出与扫描结果不一致时报错。
3. **argv 构造按服务类型分派**，或把参数放进 Boot 包的结构化字段而非 argv 字符串；argv 只保留可执行名与真正的运行时参数。
4. 保持 `creator` 的 `BOOT_CREATE` 白名单；新增创建域时同时扩展清单校验和真实 Boot 授权。

## 验收

- 单 UART 场景行为不变（回归 serial/tty 双架构 QEMU）。
- 增加第二个同类设备或第二个 virtio-mmio 窗口时，**只改清单**即可完成发现与授权，`service.c` 无需修改。
- 显式 tag 与扫描候选一致时正常启动；不一致时报错而非静默选择其一。
- 清单给出非 `BOOT_CREATE` 的 `creator` 时，加载阶段明确拒绝。

## 备注

- init 的实例归属和参数分派无需修改内核接口。但生产 `QUERY_BOOT_RESOURCE` 只发布一个 UART，且拒绝非零 cursor；仅修改清单或 init 无法获得 virtio 的真实授权。多窗口发布需要扩展既有查询 ABI，待确认后同步 06、07、09。
- 候选：保留 UART 记录布局和精确初始 SYSTEM QUERY 准入，扩展为分页启动库存。每条 virtio 记录区分实际设备窗口与覆盖它的完整 MMIO 页组，并关联自己的 IRQ_SOURCE；不把共享页内的字节偏移当作硬件隔离。块服务和网卡若处于同一页组，不默认把可写页同时授给两个独立驱动。DMA 发权与结束另行冻结，查询库存不能开启或结束 DMA 域。
- 与 009 相邻但不同：009 是内核侧"窗口类型不支持 RAM"，本条是 init 侧"发现与分发写死单实例"。
- 本条阻塞默认 init 拉起生产 `blk`，因此影响 M3 的根挂载；net 和图形服务也需要后续的设备发现路径。
