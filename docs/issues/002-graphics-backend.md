# 002 图形后端顺序与真机目标不一致

状态：待处理　优先级：P1　关联约束：01#3（QEMU 专属路径不得成为前提）

## 现象

`docs/design/13-graphics.md` 阶段表的 G2 写的是"ramfb/virtio-gpu 上屏"。两者都是模拟设备，真机上不存在。

## 影响

按现文档推进会在真机阶段推翻后端选择；且 ramfb 依赖 fw_cfg、virtio-gpu 依赖 DMA 后端，都不是真机最短路径。

## 建议修复

改为 **`simple-framebuffer` 优先**：由 U-Boot 等固件初始化显示并把 `simple-framebuffer` 节点写进 DTB（含物理地址、尺寸、stride、像素格式），内核只按该窗口做 tile 帧缓冲。

- QEMU 侧用注入 DTB 节点的方式验证同一代码路径，保留 `pmemsave`/`screendump` 交叉核对。
- ramfb/virtio-gpu 降级为"模拟环境手段"，不作为真机方案。
- 目标板固件不提供该节点时，按 14 的硬件矩阵记为"不可用"，不为此另写显示控制器驱动。

## 验收

同一份 dis 代码在"QEMU 注入节点"与"真机固件节点"两条路径下都能产出像素摘要。
