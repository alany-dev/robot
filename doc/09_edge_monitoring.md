# 端侧监控系统

## 1. 这个功能到底解决什么问题

机器人整机一旦变复杂，问题往往不在单个算法，而在 CPU 抢占、内存泄漏、网络抖动、某个进程异常膨胀、系统资源互相干扰。监控模块解决的就是“让系统性能问题可见、可记录、可定位”。它的价值不只是展示指标，而是给长期运行和问题回溯提供依据。

## 2. 先看哪些文件

- `src/monitor/src/main.cpp`
- `src/monitor/src/monitor/process_monitor.cpp`
- `src/monitor/src/monitor/monitor_helper.cpp`
- `src/monitor/msg/*`
- `src/monitor/srv/*`
- `src/shm_transport/test/talker.cpp`
- `offline-agent/monitor_processes.py`

## 3. 启动路径 / 数据流 / 控制流

典型启动命令：

```bash
catkin_make
source devel/setup.bash
rosrun monitor monitor_node
```

主流程是：`monitor_node` 拉起后，周期性采集系统级指标和已注册进程指标，发布到 `/monitor_info`，同时写 rosbag 便于后续分析。某些业务节点可以通过辅助注册机制把自己的 PID 主动注册进去，监控节点就能把系统监控和进程监控合并到统一视角里。

## 4. 亮点实现细节

### 4.1 系统级监控和进程级监控被统一到一个框架

监控代码不是只看整机 CPU，也不是只看单进程，而是同时覆盖 CPU softirq、CPU load、CPU stat、内存、网络等系统级指标，以及进程 CPU 占用、内存、线程数、磁盘 I/O 等进程级指标。这种“双层观察”对机器人系统尤其重要，因为很多问题只看一层是定位不出来的。

### 4.2 `ProcessMonitor` 直接读 `/proc`

`process_monitor.cpp` 通过 `/proc` 获取 PID 级 CPU、内存、线程数和 I/O 速率，说明作者没有依赖重量级外部监控系统，而是自己在本机侧完成轻量采集。这种方式很适合资源受限的边缘设备。

### 4.3 `MonitorHelper::AutoRegister/AutoUnregister` 很有工程味

这是这套监控系统最值得学的细节之一。节点可以在生命周期里自动向监控服务注册和注销，降低人工维护 PID 列表的成本。`src/shm_transport/test/talker.cpp` 就展示了这种自动注册思路并非空设计，而是已经被真实代码引用。

### 4.4 监控数据不仅实时发布，还会落盘

监控节点会写 rosbag，这意味着它不是只给当前 UI 看一眼，而是支持事后复盘。对机器人这种现场复现困难的系统来说，能回看历史资源曲线非常重要。

### 4.5 离线 Agent 还有独立的 Python 监控脚本

`offline-agent/monitor_processes.py` 说明作者针对离线 ASR / LLM / TTS 进程还提供了更直接的进程监控手段。它和 ROS 监控框架不是同一层面的东西，但在实际排查本地大模型服务时会非常有用。

## 5. 调试与验证方法

- 先启动 `monitor_node`，确认 `/monitor_info` 有稳定输出。
- 让感知或 Agent 节点进入高负载状态，观察系统级和进程级指标是否同步变化。
- 如果某节点没有出现在监控结果里，检查是否完成注册或 PID 是否已经失效。
- 排查性能抖动时，不要只盯 CPU 平均值，要同时看 softirq、线程数和 I/O 速率。
- 对离线 Agent 重点结合 `monitor_processes.py` 观察 ASR、LLM、TTS 三类进程是否出现异常膨胀或阻塞。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先读 `main.cpp` 和消息/服务定义，搞清楚监控系统对外暴露了什么信息。

### 2 小时

继续读 `process_monitor.cpp` 和 `monitor_helper.cpp`，理解指标采集和进程注册机制。到这一步，你应该能说清楚这套监控系统如何同时覆盖系统和业务进程。

### 1 天

自己做一次性能压测：同时跑视觉链路、导航和 Agent，记录监控数据，再对照实际现象做问题定位。做完这一步，你才真正掌握监控框架的使用价值。

## 7. 你学完后应该能回答什么问题

- 为什么机器人系统必须同时做系统级和进程级监控。
- `ProcessMonitor` 通过 `/proc` 能拿到哪些关键指标。
- 自动注册机制为什么比手工维护 PID 列表更可靠。
- 监控数据为什么要写 rosbag，而不是只在控制台打印。
- 离线 Agent 的进程监控为什么需要补一个独立脚本。
