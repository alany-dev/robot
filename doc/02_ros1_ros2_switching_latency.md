# ROS1 / ROS2 通信切换与时延性能分析

## 1. 这个功能到底解决什么问题

这个仓库不是单纯“把 ROS1 工程迁到 ROS2”，而是保留了两套低时延实现，用于回答一个更实际的问题：同样的机器人感知链路，在 ROS1 和 ROS2 上分别怎么调到更低时延，差异在哪里，如何做公平对比。换句话说，这部分的价值不在 API 迁移本身，而在低时延工程方法的可迁移性。

## 2. 先看哪些文件

- ROS1 版本：`src/usb_camera/src/usb_camera.cpp`
- ROS1 版本：`src/img_decode/src/img_decode.cpp`
- ROS2 版本：`ros2_ws/src/usb_camera/src/usb_camera.cpp`
- ROS2 版本：`ros2_ws/src/img_decode/src/img_decode.cpp`
- 测试节点：`ros2_ws/src/usb_camera/src/test_sub.cc`
- 环境说明：`ros2_ws/env.md`
- Fast DDS 配置：`ros2_ws/shm_fastdds.xml`

## 3. 启动路径 / 数据流 / 控制流

建议先把 ROS1 和 ROS2 的同名节点对照阅读。最简单的路径是“相机采集 -> 图像解码 -> 测试订阅”。

ROS2 侧的典型流程：

```bash
cd ros2_ws
colcon build --symlink-install
source install/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export FASTRTPS_DEFAULT_PROFILES_FILE=$PWD/shm_fastdds.xml
```

然后运行相机、解码和 `test_sub.cc`，测平均帧率和消息延迟。README 里说的是“时延性能分析”，仓库里最完整的其实是“可对照实现 + 测量手段”，而不是一份已经整理好的终版 benchmark 报告。

## 4. 亮点实现细节

### 4.1 迁移时保留了链路结构一致性

ROS2 代码并不是完全换一套业务逻辑，而是尽量保留 ROS1 的节点职责、消息路径和懒订阅思路。这样做的好处是：你比较的是通信框架和 QoS 调优，而不是两套完全不同的业务实现。

### 4.2 ROS2 的 QoS 选择明显偏向实时性

ROS2 侧显式使用浅队列、`best_effort()`、`durability_volatile()`。这说明作者明确接受“新鲜数据优先、旧数据可丢”的策略，避免视觉链路产生积压。对于机器人摄像头、雷达这类连续流数据，这往往比追求绝对不丢包更合理。

### 4.3 Fast DDS 被配置成尽量走共享内存

`shm_fastdds.xml` 的意义很大。它不仅是“调个参数”，而是直接约束 Fast DDS 传输方式，尽量避免走默认 UDP/TCP。也就是说，作者在 ROS2 下依然把低时延优化重点放在数据搬运路径上，而不是只满足于功能可用。

### 4.4 懒订阅思路在 ROS2 里被延续

ROS1 的 `img_decode` 已经有根据下游订阅情况控制上游开销的思路，ROS2 版本继续保留，只是实现形式更符合 `rclcpp` 的写法。这说明“低时延”在这里不是某个单点技巧，而是一条贯穿各节点的共同设计原则。

### 4.5 `test_sub.cc` 是复现实验的抓手

它负责统计平均帧率和消息延迟，是你把“感觉更快”变成“能量化验证”的关键。真正要做性能分析，不要停留在代码阅读，必须从这个测试点出发补齐自己的测量表。

## 5. 调试与验证方法

- 保持 ROS1 与 ROS2 使用尽可能相同的输入源、分辨率、压缩格式和机器负载。
- ROS2 侧先验证环境变量是否正确导出，否则你测到的可能不是共享内存优化路径。
- 重点记录平均延迟、P95 延迟、帧率稳定性，而不是只看平均值。
- 如果 ROS2 延迟异常，优先检查 QoS 不匹配、DDS 传输方式和队列深度。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

把 ROS1/ROS2 两套 `usb_camera.cpp`、`img_decode.cpp` 对照一遍，先抓住“哪些业务逻辑没变，哪些通信层写法变了”。

### 2 小时

把 `env.md`、`shm_fastdds.xml` 和 `test_sub.cc` 一起读完，再自己列一个表：QoS、线程模型、懒订阅、时间戳处理分别在 ROS1/ROS2 怎么做。

### 1 天

亲自跑一次对照实验。控制输入和环境一致，记录 ROS1 普通链路、ROS2 默认链路、ROS2 共享内存优化链路三组数据。到这一步你才真正掌握这部分亮点。

## 7. 你学完后应该能回答什么问题

- 这套仓库为什么没有简单地“只保留 ROS2”。
- ROS2 里哪些 QoS 选择是在主动换取低延迟。
- Fast DDS 共享内存配置为什么会显著影响结果。
- 如何设计一组相对公平的 ROS1 / ROS2 时延对比实验。
- 为什么 README 里的“性能分析”需要你自己补完实验数据，而不是只看现有代码。
