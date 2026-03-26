# 机器人 URDF 模型

## 1. 这个功能到底解决什么问题

URDF 模型不是为了“在 RViz 里好看”，而是为了定义机器人各部件的几何关系、传感器安装位姿、TF 变换树和仿真基础。导航、目标跟踪、雷达到相机投影、Gazebo 仿真都依赖这些坐标定义。如果 URDF 不准确，上层再好的算法也会建立在错误坐标关系上。

## 2. 先看哪些文件

- `src/pkg_launch/launch/urdf.launch`
- `src/test11_description/launch/display.launch`
- `src/test11_description/launch/gazebo.launch`
- `src/test11_description/urdf/test11.xacro`
- `src/test11_description/meshes/`

## 3. 启动路径 / 数据流 / 控制流

典型使用方式有三种：

```bash
roslaunch pkg_launch urdf.launch
roslaunch test11_description display.launch
roslaunch test11_description gazebo.launch
```

`urdf.launch` 会把 xacro 展开后的机器人描述写入参数服务器，并启动 `robot_state_publisher`。之后 TF 树中的 `base_footprint`、`base_link`、`camera_link`、`laser_link`、左右轮等关系就会成为整机的空间基准。

## 4. 亮点实现细节

### 4.1 模型结构直接服务于整机系统

`test11.xacro` 里定义的不只是底盘外形，还明确包含 `left_wheel`、`right_wheel`、`camera_link`、`laser_link` 等关键链路。这意味着 URDF 不是独立资产，而是已经和导航、感知、控制系统强绑定。

### 4.2 `base_footprint` 与 `base_link` 的分层很关键

根坐标系通常采用 `base_footprint`，而真正机体实体使用 `base_link`。这种分层是 ROS 移动机器人常见且合理的做法，有助于把地面投影关系与机体几何关系分开处理。

### 4.3 根节点刻意避免惯性参数是经验写法

文档中提到根 `base_footprint` 不放 inertial，是为了避免 KDL 对根链接惯性处理带来的问题。这不是语法技巧，而是长期做 ROS 机器人模型时常见的经验性规避手段。

### 4.4 相机和雷达安装位姿被明确写死在模型里

这对于目标跟踪和导航非常关键。相机识别结果要和雷达点云投影结合，导航又依赖雷达位姿和底盘中心关系，因此这些安装变换如果只是散落在代码里，会非常难维护。统一写进 URDF 才是正确工程做法。

### 4.5 RViz 和 Gazebo 两条验证路径都具备

`display.launch` 用于静态检查模型和 TF，`gazebo.launch` 用于仿真环境验证。这说明作者把 URDF 当作真实开发资产，而不是一次性展示文件。

## 5. 调试与验证方法

- 先在 RViz 中检查模型是否完整、关节方向是否合理、TF 树是否闭合。
- 再核对相机和雷达安装位姿是否与真实机器一致。
- 如果导航或跟踪出现系统性偏差，优先检查 URDF 和静态变换，而不是先怀疑算法。
- Gazebo 仿真时重点看模型尺度、碰撞体和传感器朝向是否正确。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先把 `test11.xacro` 中的 link 和 joint 关系画出来，弄清楚机器人 TF 树骨架。

### 2 小时

结合 `urdf.launch`、`display.launch`、`gazebo.launch` 看模型如何被加载到不同运行环境中。到这一步，你应该能解释 URDF 为什么会直接影响导航和感知结果。

### 1 天

亲自比对真实机器人尺寸和 URDF 参数，检查相机、雷达、轮子位姿是否一致，并在 RViz/Gazebo 里验证。做过这一步后，你对整机空间关系的理解会稳定很多。

## 7. 你学完后应该能回答什么问题

- 为什么 URDF 是导航、感知、控制的公共基础，而不是可有可无的描述文件。
- `base_footprint` 和 `base_link` 各自承担什么角色。
- 为什么相机和雷达安装位姿必须进入 URDF。
- 根节点不放 inertial 是为了解决什么实际问题。
- 为什么一个好用的 URDF 必须同时能支撑 RViz 检查和 Gazebo 仿真。
