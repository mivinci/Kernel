# 014 init 的设备发现与授权为单例硬编码

状态：待处理　优先级：P2　关联：07、09、12

## 现象

1. **单设备假设**：`serviceinit()` 只发一次 `QUERY_BOOT_RESOURCE` 取 UART 的 `mmio`/`irqsource`/`kind`/`offset`/`bytes`/`shift`/`width`，`mapbytes`、`uartkind`、`uartoffset` 等全部是文件级 `static` 单例。启动第二个设备（virtio-net、framebuffer）没有对应路径。
2. **授权靠扫表推断**：`listener` 与 `observer` 由 `serviceinit()` 遍历 init 自己的资源表取得——按 `RESOURCE_CALL + RESOURCE_USE|RESOURCE_GRANT + count 1 + image 匹配` 和 `RESOURCE_OBSERVE + OBSERVE_MONITOR` 做模式匹配。同类资源出现第二个时，`if (listener) return -ERR_STATE` 会直接报错。
3. **argv 硬编码 UART 语义**：`arguments()` 把 UART 参数格式化成十进制字符串固定放进 `argv[1..6]`，tty 靠覆盖 `argv[1]`/`argv[2]` 绕过。新增服务类型必须改这段代码。
4. **预留字段未生效**：`Servicecfg.creator` 解码后从未使用（`service.c` 零引用）；`flags` 恒等 `INITCFG_REQUIRED`。清单格式留了扩展位，实现没用到。

## 影响

- net 阶段拉起 virtio-net 服务、图形阶段拉起 dis，都卡在第 1 项——现在写不出"给第二个设备授权"的代码路径。
- 第 2 项的属性匹配在资源种类增加后会变成脆弱断言：正常配置可能被误判为错误，而错误配置可能恰好匹配上。
- 第 3 项使"加一个服务"从"改数据"退化成"改代码"，与清单驱动启动的初衷相反。

## 建议方案

1. **设备发现按清单条目查询**：需要硬件窗口的服务在清单里声明设备类别（UART / virtio-mmio 窗口 / framebuffer），init 按类取候选并逐个授权，不把实例固化在代码里。
2. **listener/observer 由启动包 tag 显式给出**（`BOOT_*` tag），不再靠属性扫描推断；扫描保留为**校验**手段而非发现手段——显式给出与扫描结果不一致时报错。
3. **argv 构造按服务类型分派**，或把参数放进 Boot 包的结构化字段而非 argv 字符串；argv 只保留可执行名与真正的运行时参数。
4. 若 `creator` 短期不用，在 `manifest.c` 中显式拒绝非零值，并在 07 记录"保留未用"，避免读代码的人误以为它生效。

## 验收

- 单 UART 场景行为不变（回归 serial/tty 双架构 QEMU）。
- 增加第二个同类设备或第二个 virtio-mmio 窗口时，**只改清单**即可完成发现与授权，`service.c` 无需修改。
- 显式 tag 与扫描候选一致时正常启动；不一致时报错而非静默选择其一。
- 清单给出未使用的 `creator` 非零值时，加载阶段明确拒绝。

## 备注

- 本条不涉及内核接口变更；设备能力位与窗口属性以 09 的 Device 租约为准。
- 与 009 相邻但不同：009 是内核侧"窗口类型不支持 RAM"，本条是 init 侧"发现与分发写死单实例"。
- 本条不阻塞 M3，但会阻塞 net/图形服务的拉起，故排在 013 之后。
