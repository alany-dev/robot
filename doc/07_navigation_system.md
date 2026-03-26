# 机器人导航系统

## 1. 这个功能到底解决什么问题

导航模块解决的是机器人如何基于地图、定位和底盘控制，从当前位置稳定移动到目标位置。它不是一个单节点功能，而是地图、定位、全局规划、局部规划、避障、TF、底盘控制等多个模块的组合。这个仓库的价值在于它已经把导航栈和整机传感器、底盘、Agent 指令衔接起来了。

## 2. 先看哪些文件

- `src/robot_navigation/launch/robot_navigation.launch`
- `src/robot_navigation/launch/robot_slam.launch`
- `src/robot_navigation/launch/move_base.launch`
- `src/robot_navigation/scripts/move_test.py`
- `src/robot_navigation/config/robot/*.yaml`
- `src/agent/navigation_controller.py`

## 3. 启动路径 / 数据流 / 控制流

仓库里有两种典型模式：

1. 已有地图的定位与导航模式。
2. 建图与导航一体模式。

常用命令如下：

```bash
roslaunch robot_navigation robot_navigation.launch
roslaunch robot_navigation robot_slam.launch
rosrun robot_navigation move_test.py
```

`robot_navigation.launch` 会把 URDF、雷达、地图服务器、AMCL、`move_base` 等关键部分串起来。控制链路最终仍然会落到 `/cmd_vel`，再由底盘控制模块执行。

## 4. 亮点实现细节

### 4.1 入口被分成“已有地图”和“SLAM”两套模式

这是很实用的组织方式。很多项目把建图和导航糊在一起，导致调试困难。这里通过不同 launch 明确区分使用场景，方便你分别排查定位问题和建图问题。

### 4.2 `move_base` 规划器可切换

`move_base.launch` 支持 `dwa` 或 `teb`，并配合 `global_planner`。这说明作者不是只把导航栈跑起来，而是预留了不同局部规划策略的比较空间。学习时要关注参数文件怎样影响机器人的转向、避障和速度行为。

### 4.3 导航链路与整机 TF 强绑定

导航不是一个纯算法问题，它强依赖正确的 TF 树，包括 `base_footprint`、`laser_link`、`camera_link` 等坐标关系。这也是为什么阅读导航模块时必须同时配合 URDF 文档一起看。

### 4.4 Agent 可以直接桥接导航能力

`navigation_controller.py` 提供了预定义地点和导航控制接口，说明导航能力已经不只是给 RViz 点击目标点使用，而是被纳入 Agent 的函数调用体系。这样用户说“带我去客厅”之类的自然语言指令，才能真正落到导航执行层。

### 4.5 `move_test.py` 是最好的最小验证入口

不要一上来就从 Agent 触发导航。先用 `move_test.py` 通过 ActionLib 发送目标，确认地图、定位、规划和底盘控制这条经典链路完整可用，再把它接进更复杂的上层系统。

## 5. 调试与验证方法

- 先分开验证雷达、里程计、TF、地图，再启动完整导航。
- 定位不稳时优先检查 AMCL 参数、雷达数据质量和里程计漂移。
- 规划不合理时检查全局/局部规划器参数，而不是只盯着地图。
- 机器人能规划但不动时，回头检查 `/cmd_vel` 是否成功下发到底盘控制模块。
- 从 Agent 触发导航前，先确认 `move_test.py` 可以独立成功到达目标点。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先读三个 launch：`robot_navigation.launch`、`robot_slam.launch`、`move_base.launch`，画出导航系统组成图。

### 2 小时

再读配置 YAML 和 `move_test.py`，弄清楚参数、Action 接口和底盘执行之间的关系。到这一步，你应该能独立描述“导航从发目标到发 `/cmd_vel`”的完整路径。

### 1 天

亲自完成一次全流程验证：加载地图定位、发送目标、观察路径规划、记录是否成功到达，并分析失败时究竟是定位、规划还是底盘执行出了问题。

## 7. 你学完后应该能回答什么问题

- 为什么导航必须作为一套系统看，而不是单独看 `move_base`。
- 什么时候该用已有地图模式，什么时候该用 SLAM 模式。
- `dwa` 和 `teb` 的切换对机器人行为意味着什么。
- 导航为什么强依赖 URDF 和 TF 正确性。
- Agent 是怎样把自然语言地点指令落到 ROS 导航栈上的。
