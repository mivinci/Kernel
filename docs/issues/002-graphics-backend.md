# 002 图形后端顺序与真机目标不一致

状态：已修复（13 已改为三后端与能力位；实现待 G2）　优先级：P1　关联约束：01#2、01#3、01#9

## 现象

13 的阶段表原把 G2 写成"ramfb/virtio-gpu 上屏"，两者都是模拟设备；真机唯一可行路径是固件预设的 `simple-framebuffer`。此外后端差异（能否翻页、几何、格式）没有被抽象，容易把某一后端的限制写进合成层。

## 影响

- 按原顺序推进会在真机阶段推翻后端选择；
- `ramfb` 依赖 fw_cfg、`virtio-gpu` 依赖尚未实现的 DMA 后端，都不是真机最短路径；
- 若只实现 `simple-framebuffer` 而不定义能力位，接口会固化"单缓冲 + 全帧 blit"，将来接 `virtio-gpu` 时上层要改。

## 已落地的修复（2026-11-18）

13 新增"显示后端与能力位"并重排阶段：

1. 能力位 `flip`/`cursor`/几何/格式；上层不按型号分支。
2. 三后端定位：`simple-framebuffer` = 生产路径（**不依赖 DMA**）；`ramfb` = 可选 QEMU 开发设施；`virtio-gpu` = DMA 就绪后的虚拟机与未来 GPU 入口。
3. 选择顺序固定、不热切换；不满足时报明确不可用原因，不静默降级。
4. 验证分级：像素回归只由 `simple-framebuffer` 承担，另两个仅冒烟/能力验证。

## 仍待接

- **跨主题**：`simple-framebuffer` 需要 Device 租约支持 **RAM 型窗口**（现仅 MMIO）。该项属 09 的待接内容，不在图形侧顺手改内核。
- 实现在 G2 阶段落地；`ramfb` 与 `virtio-gpu` 按 13 的阶段表分别排在 G4、G5。

## 验收

- QEMU：`dumpdtb` + `fdt` 注入 `reserved-memory` 与 `simple-framebuffer` 节点后以 `-dtb` 启动，`pmemsave` 像素与黄金图一致；
- 反例：不支持的 `format`、`stride < width*4`、显示面小于合成面，三者均报离线且不写帧缓冲；
- 仅启用 `ramfb` 时能出一帧并被人工确认，不做像素回归；
- 真机：由 U-Boot 提供节点，同一份 dis 代码产出相同像素摘要（板端验收见 14）。
