# 端侧监控系统：深度版学习结果

## 1. 30 分钟路线的实际收获：先把“监控什么”分成两层

我替你把 `src/monitor/src/main.cpp`、`process_monitor.cpp`、`monitor_helper.cpp` 串起来之后，最应该先记住的是：这套监控系统不是只看整机 CPU，也不是只盯某个进程，而是同时做两层监控。

- 系统级监控：CPU softirq、CPU load、CPU stat、内存、网络。
- 进程级监控：某个 ROS 节点对应 PID 的 CPU、内存、线程数、磁盘 I/O。

对小白来说，这很关键。因为机器人性能问题经常不是“CPU 高”这么简单，而是“某个进程突然占满 CPU”“内存缓慢泄漏”“I/O 抖动拖慢感知链路”这类具体问题。

## 2. 2 小时路线的实际收获：框架是怎么组织的

### 2.1 `main.cpp` 把系统监控和进程监控统一装到同一条消息里

`MonitorNode` 启动时会：

- 提供 `/monitor/register_node` 和 `/monitor/unregister_node` 两个服务。
- 发布 `/monitor_info`。
- 打开 rosbag 文件持续写历史数据。
- 创建多个 runner：`CpuSoftIrqMonitor`、`CpuLoadMonitor`、`CpuStatMonitor`、`MemMonitor`、`NetMonitor`。
- 创建一个 `ProcessMonitor` 负责维护注册进程列表。

`Run()` 循环里每次都会：

1. 先 `process_monitor_->UpdateAll()`。
2. 再构造 `monitor::MonitorInfo`。
3. 让每个系统级 runner 往这个消息里填数据。
4. 再把所有已注册进程的 `NodeMonitorInfo` 填进去。
5. 如果有人订阅就发布，同时无论有没有订阅都写 rosbag。

也就是说，系统级和进程级数据最后汇总到同一个话题和同一份 bag 里，方便统一分析。

### 2.2 `ProcessMonitor` 的关键是“按 PID 做真实进程观察”

`RegisterNode()` 并不是只记一个名字，而是记：

- `node_name`
- `process_name`
- `pid`
- 上次更新时间
- 上次 CPU 时间
- 上次磁盘读写字节数

之后 `UpdateAll()` 会遍历已注册节点，检查 `/proc/<pid>` 是否还存在。若进程不存在，就标记 invalid；若还存在，就 `UpdateProcess()` 刷新：

- CPU 百分比
- CPU 总时间
- 常驻内存 MB 和占比
- 线程数
- 磁盘读写速率和总量

这说明监控对象不是抽象的“ROS 节点概念”，而是最终落到 Linux 进程层面去观察。

### 2.3 为什么 `/proc` 方案很适合边缘设备

`ReadProcessCpuTime()`、`ReadProcessMemory()`、`ReadProcessDiskIO()` 都直接读 `/proc` 文件系统。这种方案的优点是：

- 不依赖额外重型监控服务。
- 资源占用小。
- 在嵌入式 Linux 上普遍可用。

对机器人这种边缘设备来说，这比引入完整外部监控平台更轻量。

### 2.4 `MonitorHelper` 解决的是“谁来告诉监控系统该盯谁”

`MonitorHelper::AutoRegister()` 的做法是：

- 等待注册服务上线。
- 自动获取当前 PID。
- 自动获取当前进程名。
- 调服务把 `node_name + process_name + pid` 注册给 `monitor_node`。

`AutoUnregister()` 则负责退出时注销。

`src/shm_transport/test/talker.cpp` 已经示范了这个流程：程序启动时注册，收到 SIGINT/SIGTERM 时自动注销。这说明辅助类不是摆设，而是已经被实际用上了。

### 2.5 离线 Agent 还有一条独立监控线

`offline-agent/monitor_processes.py` 不是 ROS 监控框架的一部分，而是用 `psutil + matplotlib` 单独监控：

- `sherpa-onnx-microphone`
- `llamacpp-ros`
- `tts_server`

它更像一个面向离线大模型服务的专用分析工具，适合快速看 CPU 和内存曲线。

## 3. 1 天路线的实际收获：你亲手验证时应该怎么用它定位问题

如果你花一天真正使用监控系统，最值得做的是：

1. 同时跑视觉、导航、Agent，观察系统级 CPU 和单进程 CPU 是否一致。
2. 让某个节点高频运行，看线程数和内存是否异常增长。
3. 检查 bag 文件，确认即使现场没人在看界面，也能事后回放资源变化。
4. 把一个节点故意不注册，看看监控视角里会缺什么。

做完这些，你会明白：监控的真正价值不是“实时看个数字”，而是帮助你把系统异常和具体进程关联起来。

## 4. 原文问题逐一回答

### 4.1 为什么机器人系统必须同时做系统级和进程级监控

因为只看系统级，你知道“机器变慢了”，但不知道是谁拖慢的；只看进程级，你又可能漏掉系统整体资源竞争、软中断、网络抖动等问题。机器人是多进程多设备系统，两层视角缺一不可。

### 4.2 `ProcessMonitor` 通过 `/proc` 能拿到哪些关键指标

从代码看，至少包括：CPU 百分比、CPU 累计时间、常驻内存 MB、内存占比、线程数、磁盘读速率、写速率、累计读写字节数和操作次数。这已经足够覆盖大多数节点级资源问题定位。

### 4.3 自动注册机制为什么比手工维护 PID 列表更可靠

因为 PID 会变、进程名可能相似、系统重启后 PID 会重新分配。让节点在启动时自己注册、退出时自己注销，能让监控系统始终盯住真实运行中的那个进程，维护成本也更低。

### 4.4 监控数据为什么要写 rosbag，而不是只在控制台打印

因为很多机器人问题是现场偶发的，等你连上设备时已经过去了。写 rosbag 后，你可以事后回看当时 CPU、内存、进程状态，做真正的复盘，而不是只靠现场口述。

### 4.5 离线 Agent 的进程监控为什么需要补一个独立脚本

因为离线 ASR / LLM / TTS 往往是独立本地进程，开发时最常见的问题就是哪一个服务突然占满 CPU 或内存。用一个轻量图形脚本直接盯这三个进程，比每次都接 ROS 监控消息更直接。

## 5. 给小白的补充背景

- softirq 可以粗略理解成内核处理网络等软中断的消耗，很多网络或驱动问题会反映在这里。
- rosbag 不只是录传感器，也可以录监控消息，便于事后分析。
- 节点名字和 Linux 进程不是完全一回事，所以监控最终还是要落到 PID。
- 大型机器人系统里，“定位问题”本身就是一项核心能力，监控框架是这项能力的基础设施。
