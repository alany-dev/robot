# 雷达传感器链路

## 1. 这个功能到底解决什么问题

雷达模块负责让机器人稳定获取二维激光扫描数据，并把它以可复用、可配置、可扩展的方式接入导航、跟踪和调试链路。这个仓库的亮点不只是“接了 YDLidar SDK”，而是把旧版直接驱动方式逐步抽象成接口、工厂、适配器和配置校验体系，明显带有工程化重构痕迹。

## 2. 先看哪些文件

- `src/ydlidar/src/lidar_interface.h`
- `src/ydlidar/src/lidar_factory.cpp`
- `src/ydlidar/src/lidar_config_loader.cpp`
- `src/ydlidar/src/ydlidar_adapter.cpp`
- `src/ydlidar/src/ydlidar_node_v2.cpp`
- `src/ydlidar/src/shm_ydlidar_node.cpp`
- `src/ydlidar/launch/ydlidar.launch`
- `src/ydlidar/launch/ydlidar_v2.launch`

## 3. 启动路径 / 数据流 / 控制流

仓库里能看到两条思路：

1. 旧版直接封装 YDLidar SDK 的节点链路。
2. 新版 `ydlidar_node_v2` 的抽象化链路。

典型命令：

```bash
roslaunch ydlidar ydlidar.launch
roslaunch ydlidar ydlidar_v2.launch
```

数据流总体是：ROS 参数加载 -> 配置校验 -> 工厂按类型创建适配器 -> 适配器调用具体 SDK 获取扫描数据 -> 转换为 ROS `LaserScan` 并发布。某些场景下还可以走共享内存版本，降低大数据传输成本。

## 4. 亮点实现细节

### 4.1 `LidarInterface` 先定义能力边界

新版结构里最值得学的是先抽象接口。`LidarInterface` 定义了雷达节点真正关心的共性能力，比如配置、启动、取数、停止，而不是让上层直接依赖某家 SDK 的细节。这一步为后面的多型号支持打下了基础。

### 4.2 `LidarFactory` 负责选择实现

工厂模式的价值不是“写法好看”，而是把“根据类型字符串选择哪种雷达实现”的逻辑集中起来。这样以后如果要接入更多雷达，调用方不需要重写节点主流程。

### 4.3 `LidarConfigLoader` 把参数合法性前移

很多机器人项目把参数错误留到运行中才暴露，这会让故障非常难查。这里专门用 `LidarConfigLoader` 读取 ROS 参数并校验量程、频率、采样率等范围，把错误尽量拦在初始化阶段，这是很成熟的工程做法。

### 4.4 `YdLidarAdapter` 隔离 SDK 细节

适配器的意义在于把统一配置映射到 SDK 的 `setlidaropt(...)` 等具体接口。也就是说，上层节点关心的是抽象雷达配置，下层适配器关心的是“这家 SDK 具体怎么喂参数”。这一层隔离能显著降低 SDK 变动对整机代码的冲击。

### 4.5 `ydlidar_node_v2.cpp` 负责生命周期和容错

主节点不只是循环取数发布，还承担初始化、重试、服务接口、状态切换和 ROS 消息转换。真正的工程价值就在这里：节点知道什么时候该重连、什么时候该报错退出、什么时候该把底层异常转换成上层可理解的状态。

### 4.6 共享内存版本说明雷达也被纳入低时延体系

`shm_ydlidar_node.cpp` 说明共享内存优化不是图像特例，作者也考虑了雷达链路的数据搬运成本。虽然雷达带宽不如图像高，但在多节点、多订阅者系统里，减少拷贝仍然有价值。

## 5. 调试与验证方法

- 先用旧版和 v2 版分别启动，确认参数、设备、话题输出是否一致。
- 故障排查先看参数加载与校验结果，再看串口/USB 设备和 SDK 初始化。
- 如果导航效果异常，先确认 `/scan` 时间戳和 TF 是否稳定，不要直接怀疑导航算法。
- 如果你准备扩展其他雷达型号，先验证接口与工厂是否足够抽象，再落具体适配器。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先比较 `ydlidar.launch` 和 `ydlidar_v2.launch`，建立“旧版直接驱动”和“新版抽象驱动”的整体认知。

### 2 小时

顺着 `lidar_interface.h`、`lidar_factory.cpp`、`lidar_config_loader.cpp`、`ydlidar_adapter.cpp`、`ydlidar_node_v2.cpp` 一口气读下来。到这一步，你应该能说清楚作者为什么把雷达节点重构成现在这种结构。

### 1 天

自己尝试增加一个假想雷达类型，哪怕只补接口和工厂分支，不真正接设备。做过这一步，你会对适配器模式和配置前置校验的价值有更深理解。

## 7. 你学完后应该能回答什么问题

- 为什么新版雷达节点要引入接口、工厂、适配器三层。
- 参数校验为什么应该发生在初始化，而不是运行时。
- `YdLidarAdapter` 究竟帮上层屏蔽了什么复杂度。
- 雷达节点的生命周期管理为什么比“能跑就行”更重要。
- 为什么共享内存思路也值得扩展到雷达链路。
