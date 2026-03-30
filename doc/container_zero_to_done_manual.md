# 容器内从零到完成联调手册

## 1. 这份手册解决什么问题

这份手册默认你已经完成了板端 Docker 部署，并且容器可以正常启动。它不再重复讲“怎么装环境”，而是专门回答下面这些问题：

- 进入容器后，第一步先查什么。
- 这个项目从零跑通，理论链路是什么。
- 每个关键节点分别怎么单测、怎么联调、怎么判断对错。
- ROS 常用调试命令该怎么用在这个项目上。
- 摄像头低时延链路怎么做对比测试。
- Agent 应该按什么顺序验证，避免一上来就卡在音频或硬件外设上。

如果你还没完成部署，先看 `doc/board_docker_deployment.md` 和 `doc/project_deployment_manual.md`。这份文档假设容器已经能起来。

## 2. 先把整套系统的理论链路看清楚

### 2.1 容器启动后的基础运行面

默认板端容器入口是 `docker/build/runtime/robot_board_entrypoint.sh`，会根据环境变量决定是否自动启动这些基础进程：

- `roscore`
- `web_controller.py`
- `src/agent/start_agent.sh`
- `roslaunch base_control base_control.launch`
- `roslaunch pkg_launch all.launch`，默认不开，通常通过 Web 手动启动

也就是说，容器起来以后，最小可用系统通常是：

```text
Docker 容器
  -> roscore
  -> Web 控制器
  -> Agent
  -> base_control
```

### 2.2 主视觉与跟随链路

`src/pkg_launch/launch/all.launch` 串起来的是这条链：

```text
/dev/video0
  -> usb_camera
  -> /image_raw/compressed
  -> img_decode
  -> /camera/image_raw
  -> rknn_yolov6
  -> /camera/image_det + /ai_msg_det
  -> object_track
  -> /camera/image_det_track + /cmd_vel
  -> img_encode
  -> /camera/image_det_track/compressed
```

其中几个关键参数已经在 launch 里对齐好了：

- `usb_camera`：`1280x720`
- `img_decode`：`scale=0.5`，也就是输出 `640x360`
- `object_track`：按 `640x360` 工作
- `img_encode`：回传 `640x360` JPEG

### 2.3 导航链路

当前仓库里和导航有关的链路主要有三条：

1. 已有地图导航

```text
roslaunch robot_navigation robot_navigation.launch
  -> URDF
  -> YDLidar
  -> map_server
  -> amcl
  -> move_base
```

2. 建图

```text
roslaunch robot_navigation robot_slam.launch
  -> URDF
  -> YDLidar
  -> gmapping
  -> move_base
```

3. 空白地图烟雾测试

```text
roslaunch robot_navigation blank_map_move_base.launch
  -> blank_map
  -> move_base
  -> map->odom 静态 TF
```

### 2.4 Agent 链路

在线 Agent 的主流程是：

```text
麦克风
  -> ASR
  -> 唤醒词
  -> main.py
  -> LLM
  -> FunctionExecutor
  -> ROS 话题 / 本地函数 / 导航动作 / 音乐 / 头部动作
  -> TTS
```

Agent 不是“自己直接控制电机”，而是主要通过这些接口驱动系统：

- `/enable_tracking`
- `/enable_lidar`
- `/head_control`
- `move_base` action
- 本地音乐、音量、表情、PWM、SPI 相关驱动

## 3. 推荐的联调顺序

不要一上来直接点 Web 页面里的所有按钮。建议按下面顺序推进：

1. 进入容器，确认 ROS 环境和日志目录。
2. 检查编译产物是否齐全。
3. 检查 `roscore / web_controller / base_control / agent` 这四类基础进程。
4. 单测底盘控制 `base_control`。
5. 单测摄像头 `usb_camera`。
6. 单测解码缩放 `img_decode`。
7. 单测检测 `rknn_yolov6`，优先做离线图片测试，再做在线话题测试。
8. 单测激光雷达 `ydlidar`。
9. 联调 `object_track`，先只看图和检测框，再开 `/enable_tracking` 和 `/enable_lidar`。
10. 联调 `img_encode` 回传链路。
11. 做低时延对比测试。
12. 再做导航链路：建图、存图、定位、导航。
13. 最后做 Agent：函数级测试、控制级测试、完整语音回路测试。

这个顺序的原则很简单：先把“硬件驱动”和“底层 ROS 数据链”跑通，再去测 Agent 和导航这种高层闭环。

## 4. 进入容器后的第一套动作

### 4.1 从宿主机进入容器

在仓库根目录执行：

```bash
bash docker/scripts/robot_board_into.sh
```

默认容器名是 `robot`。等价命令是：

```bash
docker exec -it robot /bin/bash
```

### 4.2 容器内准备 ROS 环境

进入容器后先执行：

```bash
source /opt/ros/noetic/setup.bash
cd /home/orangepi/robot
source devel/setup.bash
```

建议再检查一次关键环境变量：

```bash
echo "$ROS_MASTER_URI"
echo "$ROS_IP"
echo "$ROS_HOSTNAME"
pwd
```

### 4.3 先记住这几个日志目录

这套 Docker 入口现在只把 ROS 功能节点日志写回仓库目录：

```text
logs/nodes/
```

每个节点一个子目录，目录里的文件名就是时间戳。单个文件达到大小阈值后，会直接切到新的时间戳文件继续写。

容器级聚合输出不再额外映射到仓库，统一直接看：

```bash
docker logs -f robot
```

看日志的常用命令：

```bash
find logs/nodes -mindepth 2 -maxdepth 2 -type f | sort
tail -f "$(ls -1t logs/nodes/base_control/*.log | head -n 1)"
tail -f "$(ls -1t logs/nodes/usb_camera/*.log | head -n 1)"
docker logs -f robot
```

## 5. 第一个 10 分钟必须做的基础检查

### 5.1 进程检查

```bash
ps -ef | grep -E 'roscore|web_controller.py|main.py|base_control|all.launch|robot_navigation.launch' | grep -v grep
```

### 5.2 ROS 图谱检查

```bash
rosnode list
rostopic list
```

重点关注这些基础话题是否存在：

- `/cmd_vel`
- `/odom`
- `/robot_status`
- `/enable_tracking`
- `/enable_lidar`
- `/head_control`

### 5.3 编译产物检查

很多“`roslaunch` 起不来”的问题，本质上不是 launch 文件错了，而是目标可执行文件根本没生成。

先执行：

```bash
find devel/lib -maxdepth 2 -type f | sort
```

主链路至少应该能看到这些产物：

```text
devel/lib/base_control/base_control
devel/lib/usb_camera/usb_camera
devel/lib/img_decode/img_decode
devel/lib/rknn_yolov6/rknn_yolov6
devel/lib/img_encode/img_encode
devel/lib/object_track/object_track
devel/lib/ydlidar/ydlidar_node
```

如果缺某个目标，不要先怀疑 launch，先回去检查 `catkin_make` 是否真的编过并编过对应包。

重新编译的最短命令：

```bash
cd /home/orangepi/robot
catkin_make
```

如果你只想重编某几个包：

```bash
cd /home/orangepi/robot
catkin_make --pkg base_control usb_camera img_decode rknn_yolov6 img_encode object_track ydlidar
```

## 6. 这个项目里最常用的 ROS 调试命令

进入容器并 `source` 环境后，下面这些命令最常用：

### 6.1 节点

```bash
rosnode list
rosnode info /base_control
rosnode info /object_track
rosnode ping /base_control
```

### 6.2 话题

```bash
rostopic list
rostopic info /cmd_vel
rostopic info /camera/image_raw
rostopic echo -n 1 /odom
rostopic hz /scan
rostopic bw /image_raw/compressed
rostopic delay /camera/image_det
```

### 6.3 参数

```bash
rosparam list
rosparam get /usb_camera/width
rosparam get /img_decode/scale
rosparam get /object_track/tracking_class_name
```

### 6.4 TF

```bash
rosrun tf tf_echo camera_link laser_link
rosrun tf tf_echo odom base_footprint
```

### 6.5 手工发命令

```bash
rostopic pub -1 /enable_tracking std_msgs/Bool "data: true"
rostopic pub -1 /enable_lidar std_msgs/Bool "data: true"
rostopic pub -1 /cmd_vel geometry_msgs/Twist '{linear: {x: 0.05, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}'
rostopic pub -1 /head_control std_msgs/String "data: 'left'"
```

## 7. 分模块调试与测试

这一节按“节点目的 -> 启动方式 -> 验证命令 -> 预期现象 -> 常见问题”来写。

### 7.1 `base_control`

#### 7.1.1 节点作用

`base_control` 负责订阅 `/cmd_vel`，通过串口下发到底盘控制板，同时发布 `/odom`。

#### 7.1.2 启动方式

```bash
roslaunch base_control base_control.launch
```

#### 7.1.3 单测步骤

先确认节点已经起来：

```bash
rosnode info /base_control
rostopic info /cmd_vel
rostopic info /odom
```

再做一个最小动作测试。为了安全，建议把小车架空或者先断开电机负载：

```bash
rostopic pub -1 /cmd_vel geometry_msgs/Twist '{linear: {x: 0.05, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}'
sleep 1
rostopic pub -1 /cmd_vel geometry_msgs/Twist '{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}'
```

看里程计：

```bash
rostopic echo -n 1 /odom
rostopic hz /odom
```

#### 7.1.4 预期现象

- `base_control` 节点在线。
- `/cmd_vel` 有订阅者。
- `/odom` 能持续发布。
- 底盘对速度指令有响应。

#### 7.1.5 常见问题

- 日志里持续出现 `uart read no data!`
  说明串口打开了，但 MCU 没有回包，优先检查 `/dev/ttyS2`、波特率、接线、供电和下位机程序。

### 7.2 `usb_camera`

#### 7.2.1 节点作用

直接从 `/dev/video0` 采集 MJPEG，发布到 `/image_raw/compressed`。

#### 7.2.2 启动方式

```bash
roslaunch usb_camera usb_camera.launch
```

#### 7.2.3 单测步骤

先确认设备节点存在：

```bash
ls /dev/video*
```

再检查 ROS 侧：

```bash
rosnode info /usb_camera
rostopic info /image_raw/compressed
```

这个节点有一个重要特性：只有输出有订阅者时才真正发布。因此测试时要保持一个订阅者在线：

```bash
rostopic hz /image_raw/compressed
```

验证摄像头使能开关：

```bash
rostopic pub -1 /enable_camera std_msgs/Bool "data: false"
rostopic pub -1 /enable_camera std_msgs/Bool "data: true"
```

#### 7.2.4 预期现象

- `/image_raw/compressed` 有稳定频率。
- `frame_id` 为 `camera_link`。
- 关闭 `/enable_camera` 后，频率下降为 0。

#### 7.2.5 常见问题

- 节点启动报找不到设备：检查 `/dev/video0` 是否映射进容器。
- 节点在，但 `/image_raw/compressed` 没数据：先确认你真的开了订阅者。

### 7.3 `img_decode`

#### 7.3.1 节点作用

把 `/image_raw/compressed` 解码成 RGB 图，再缩到 `640x360` 后发布 `/camera/image_raw`。

#### 7.3.2 启动方式

```bash
roslaunch img_decode img_decode.launch
```

#### 7.3.3 单测步骤

先保证上游 `usb_camera` 有输出，然后启动解码节点。和摄像头一样，这个节点也带按需订阅逻辑，测试时必须保持下游订阅者在线：

```bash
rostopic hz /camera/image_raw
```

检查分辨率是否按预期缩小：

```bash
rostopic echo -n 1 /camera/image_raw/width
rostopic echo -n 1 /camera/image_raw/height
```

#### 7.3.4 预期现象

- `/camera/image_raw` 有稳定输出。
- 宽高应该接近 `640` 和 `360`。
- `rostopic delay /camera/image_raw` 可以用来观察从采集到解码输出的累积延时。

#### 7.3.5 常见问题

- 节点在线但没有输出：多数情况是没人订阅 `/camera/image_raw`，它主动把上游订阅关掉了。

### 7.4 `rknn_yolov6`

建议把检测分成“离线图片测试”和“在线话题测试”两步。

#### 7.4.1 节点作用

订阅 `/camera/image_raw`，做 RKNN 推理，发布：

- `/camera/image_det`
- `/ai_msg_det`

#### 7.4.2 离线图片测试

离线测试的意义是：先验证模型、NPU、后处理和结果落盘没问题，再去联调真实摄像头。

```bash
cd /home/orangepi/robot
source /opt/ros/noetic/setup.bash
source devel/setup.bash
PKG=$(rospack find rknn_yolov6)
rosrun rknn_yolov6 rknn_yolov6 \
  _is_offline_image_mode:=true \
  _model_file:=$PKG/config/yolov6n_85.rknn \
  _yaml_file:=$PKG/config/yolov6.yaml \
  _offline_images_path:=$PKG/config/images/*.jpg \
  _offline_output_path:=$PKG/config/images_output
```

结果检查：

```bash
ls src/rknn_yolov6/config/images_output
```

#### 7.4.3 在线话题测试

```bash
roslaunch rknn_yolov6 rknn_yolov6.launch
```

同样，测试时必须保持检测图像输出有订阅者：

```bash
rostopic hz /camera/image_det
```

再看结构化检测结果：

```bash
rostopic echo -n 1 /ai_msg_det
rostopic info /ai_msg_det
```

如果想看累积延时：

```bash
rostopic delay /camera/image_det
```

#### 7.4.4 预期现象

- `/camera/image_det` 有输出。
- `/ai_msg_det` 中能看到 `cls_name / conf / x1 / y1 / x2 / y2`。
- 有目标时，检测框数量随场景变化。

#### 7.4.5 常见问题

- 节点在但没开始推理：通常是因为没人订阅 `/camera/image_det`，它没有继续订阅 `/camera/image_raw`。
- 如果要做性能诊断，可以把 `print_perf_detail` 打开后重启节点。

### 7.5 `ydlidar`

#### 7.5.1 节点作用

发布 `/scan`，供 `object_track` 和导航系统使用。

#### 7.5.2 启动方式

```bash
roslaunch ydlidar ydlidar.launch
```

#### 7.5.3 单测步骤

```bash
rosnode info /ydlidar_node
rostopic info /scan
rostopic hz /scan
rostopic echo -n 1 /scan/header
```

检查 TF 是否可用：

```bash
rosrun tf tf_echo camera_link laser_link
```

#### 7.5.4 预期现象

- `/scan` 有稳定频率。
- `frame_id` 应该是 `laser_link`。
- `camera_link` 到 `laser_link` 的 TF 能查到。

#### 7.5.5 常见问题

- `/scan` 没有发布：检查 `ydlidar.launch` 里配置的串口 `/dev/ttyS9`。
- `object_track` 打印 `waitForTransform` 失败：说明 URDF、TF 或雷达坐标系有问题。

### 7.6 `object_track`

#### 7.6.1 节点作用

把检测框和激光投影融合，输出：

- `/camera/image_det_track`
- `/cmd_vel`

#### 7.6.2 启动方式

```bash
roslaunch object_track object_track.launch
```

#### 7.6.3 单测步骤

先保证上游都有：

- `/camera/image_det`
- `/ai_msg_det`
- `/scan`

再启动跟踪节点，并保持最终图像输出有订阅者：

```bash
rostopic hz /camera/image_det_track
```

默认它不会主动控制底盘，必须打开开关：

```bash
rostopic pub -1 /enable_tracking std_msgs/Bool "data: true"
rostopic pub -1 /enable_lidar std_msgs/Bool "data: true"
```

看控制输出：

```bash
rostopic echo /cmd_vel
```

#### 7.6.4 预期现象

- `/camera/image_det_track` 有叠加后的图像输出。
- 图上会显示目标框、距离、速度信息。
- 找到目标时 `/cmd_vel` 会变化。
- 没有目标时速度应回到 0。

#### 7.6.5 常见问题

- 节点在但不处理：多半是没人订阅 `/camera/image_det_track`，它主动取消了对 `/camera/image_det` 和 `/ai_msg_det` 的订阅。
- 开了 `/enable_tracking` 但车不动：先看 `/cmd_vel` 是否真的有输出，再看 `base_control` 是否消费到了。

### 7.7 `img_encode`

#### 7.7.1 节点作用

把 `/camera/image_det_track` 编成 JPEG，供 Web 或远端查看。

#### 7.7.2 启动方式

```bash
roslaunch img_encode img_encode.launch
```

#### 7.7.3 单测步骤

它和前面几个节点一样，也是按需订阅。测试时保持压缩输出的订阅者在线：

```bash
rostopic hz /camera/image_det_track/compressed
rostopic bw /camera/image_det_track/compressed
rostopic delay /camera/image_det_track/compressed
```

#### 7.7.4 预期现象

- `/camera/image_det_track/compressed` 有输出。
- 带宽明显低于原始 RGB 图像。

#### 7.7.5 常见问题

- 没人订阅时它会主动停止编码，这不是故障，是设计本身。

### 7.8 Web 控制器

#### 7.8.1 作用

Web 侧除了手动控制车和头部，还负责：

- 启动和停止 `all.launch`
- 启动和停止导航链路
- 重启 Agent
- 保存 Agent 配置

#### 7.8.2 验证方式

浏览器访问：

```text
http://板卡IP:5000
```

设置页：

```text
http://板卡IP:5000/settings
```

通过 Web 做的最小验收动作：

1. 手动前进 / 停止。
2. 手动头部左转 / 右转。
3. 启动 ROS 主链路。
4. 启动跟随模式。
5. 启动导航。
6. 重启 Agent。

#### 7.8.3 对应日志

```bash
docker logs -f robot
```

### 7.9 导航系统

导航不要直接一步到位，建议分三层测试。

#### 7.9.1 第一层：空白地图 smoke test

这一步只验证 `move_base` 能不能启动、动作链路是不是通的，不依赖真实地图。

```bash
roslaunch robot_navigation blank_map_move_base.launch
```

检查：

```bash
rosnode list | grep -E 'map_server|move_base'
rostopic list | grep move_base
```

#### 7.9.2 第二层：建图

当前仓库里建议直接用：

```bash
roslaunch robot_navigation robot_slam.launch
```

不要优先用 `complete_slam.launch`，因为它引用了仓库中不存在的 `gmapping_fixed.launch`。

建图时操作步骤：

1. 启动 `robot_slam.launch`。
2. 让小车缓慢移动，尽量覆盖完整区域。
3. 同时观察 `/map`、`/scan`、`/odom`、`/tf` 是否正常。
4. 地图基本成型后保存地图：

```bash
cd /home/orangepi/robot/src/robot_navigation/maps
rosrun map_server map_saver -f my_map
```

#### 7.9.3 第三层：已有地图导航

确认地图文件准备好以后启动：

```bash
roslaunch robot_navigation robot_navigation.launch
```

检查核心节点：

```bash
rosnode list | grep -E 'map_server|amcl|move_base'
rostopic list | grep -E 'move_base|amcl|map'
```

如果要做命令式导航测试，可以直接复用 Agent 里的导航控制器：

```bash
cd /home/orangepi/robot/src/agent
python3 navigation_controller.py
```

当前代码里只预置了一个位置：

- `厨房`

它定义在 `src/agent/navigation_controller.py` 里的 `LOCATIONS` 中。如果你的真实地图坐标不同，要先改这里。

### 7.10 Agent

Agent 建议拆成四层测试。

#### 7.10.1 第一步：配置文件

先确保配置文件存在：

```bash
cd /home/orangepi/robot
cp -n src/agent/config.json.example src/agent/config.json
cat src/agent/config.json
```

重点看这些字段：

- `api_key`
- `model`
- `base_url`
- `tts_model`
- `tts_voice`
- `enable_tts`

#### 7.10.2 第二步：最小启动检查

```bash
cd /home/orangepi/robot/src/agent
bash start_agent.sh
```

或者看容器默认启动后的日志：

```bash
docker logs -f robot
```

启动时应该至少看到这些阶段：

1. Python 依赖检查
2. 音频设备检查
3. PWM 权限修复
4. `main.py` 启动
5. ASR 连接建立

#### 7.10.3 第三步：功能级测试

1. 跟随控制器测试

```bash
cd /home/orangepi/robot/src/agent
python3 robot_following_controller.py
```

它会验证 `/enable_tracking` 和 `/enable_lidar` 的发布链路。

2. 导航控制器测试

先确保 `move_base` 已启动，再执行：

```bash
cd /home/orangepi/robot/src/agent
python3 navigation_controller.py
```

3. 函数执行器测试

```bash
cd /home/orangepi/robot/src/agent
python3 function_executor.py
```

这一步可以先验证：

- 函数注册是否完整
- 跟随 / 导航 / 头部动作函数是否能被调用

#### 7.10.4 第四步：完整语音回路测试

这一步才开始测：

- 唤醒词
- ASR
- LLM
- TTS
- Function Call

建议的最小测试脚本：

1. 先说唤醒词，例如“`小沫小沫`”。
2. 说一个纯对话指令，验证 ASR+LLM+TTS。
3. 说一个动作指令，例如“启动跟随”，同时观察：

```bash
rostopic echo /enable_tracking
rostopic echo /enable_lidar
```

4. 说一个导航指令，例如“去厨房”，同时观察：

```bash
rostopic echo /move_base/status
```

#### 7.10.5 常见问题

- 日志出现 `PulseAudio: Unable to connect`
  说明容器里音频设备不可用，先不要继续测语音链路，改做函数级测试。

- 日志出现 `Opening SPI device: No such file or directory: /dev/spidev3.0`
  说明 LCD/SPI 外设没准备好。表情显示会失败，但不一定阻塞 ROS 控制链。

- 日志出现 `PWM chip not found`
  说明头部舵机的 PWM 设备没准备好，头部动作相关功能会失败。

## 8. 摄像头低时延测试与对比

这一节不是泛泛而谈，而是给你一套可以在这个仓库里直接执行的对比方法。

### 8.1 先理解这个项目的低时延策略

这条链路低时延主要靠四件事：

1. `usb_camera` 源头采 MJPEG，而不是大带宽原始图。
2. `img_decode` 早期把图缩到 `640x360`。
3. `rknn_yolov6` 内部是短队列，只保留最新帧。
4. `img_encode` 和多个节点都带按需订阅，不需要时不做额外工作。

### 8.2 测试前准备

固定测试场景，避免对比时场景变化太大：

- 固定摄像头位置
- 固定光照
- 固定目标运动速度
- 每个测试持续至少 30 秒

建议同时保留这些观测命令：

```bash
rostopic hz /image_raw/compressed
rostopic bw /image_raw/compressed
rostopic delay /image_raw/compressed
```

```bash
rostopic hz /camera/image_raw
rostopic bw /camera/image_raw
rostopic delay /camera/image_raw
```

```bash
rostopic hz /camera/image_det
rostopic delay /camera/image_det
```

```bash
rostopic hz /camera/image_det_track
rostopic delay /camera/image_det_track
```

```bash
rostopic hz /camera/image_det_track/compressed
rostopic bw /camera/image_det_track/compressed
rostopic delay /camera/image_det_track/compressed
```

### 8.3 对比维度 1：链路各阶段累积延时

记录以下话题的 `rostopic delay`：

- `/image_raw/compressed`
- `/camera/image_raw`
- `/camera/image_det`
- `/camera/image_det_track`
- `/camera/image_det_track/compressed`

结论应该这样理解：

- 越靠后的话题，`delay` 通常越大。
- 如果某一段突然增加很多，说明瓶颈就在那里。

### 8.4 对比维度 2：带宽

重点对比：

```bash
rostopic bw /image_raw/compressed
rostopic bw /camera/image_raw
rostopic bw /camera/image_det_track/compressed
```

你会看到：

- 相机入口压缩图的带宽远小于原始 RGB。
- 最终回传压缩图的带宽也远小于中间 raw 图。

### 8.5 对比维度 3：只做控制闭环 vs 同时开图像回传

这是最推荐做的一组对比，因为它和真实使用最相关。

#### 场景 A：只跑控制，不看最终压缩图

不要订阅 `/camera/image_det_track/compressed`，只看：

```bash
rostopic hz /camera/image_det_track
rostopic delay /camera/image_det_track
```

#### 场景 B：同时打开最终图像回传

再额外订阅：

```bash
rostopic hz /camera/image_det_track/compressed
rostopic bw /camera/image_det_track/compressed
```

然后对比场景 A 和 B 下：

- `/camera/image_det_track` 的 `hz`
- `/camera/image_det_track` 的 `delay`
- `/cmd_vel` 的更新及时性

如果场景 B 明显更慢，说明显示回传正在拖累链路。这个项目的设计目标就是尽量把这种拖累降到最小。

### 8.6 对比维度 4：离线检测 vs 在线检测

先做离线图片推理：

```bash
cd /home/orangepi/robot
source /opt/ros/noetic/setup.bash
source devel/setup.bash
PKG=$(rospack find rknn_yolov6)
rosrun rknn_yolov6 rknn_yolov6 \
  _is_offline_image_mode:=true \
  _model_file:=$PKG/config/yolov6n_85.rknn \
  _yaml_file:=$PKG/config/yolov6.yaml \
  _offline_images_path:=$PKG/config/images/*.jpg \
  _offline_output_path:=$PKG/config/images_output
```

再做在线摄像头检测，对比：

- 检测耗时
- 输出帧率
- 是否有明显积压

### 8.7 建议你自己做一张对比表

```text
场景                  /image_raw/compressed   /camera/image_raw   /camera/image_det   /camera/image_det_track   /camera/image_det_track/compressed
默认参数
不开最终回传
开最终回传
离线图片推理
```

每一列至少记三项：

- `hz`
- `bw`
- `delay`

## 9. 从零到完成的整机验收顺序

如果你想知道“这个项目理论上从零做到完成，需要经过哪些步骤”，可以按下面这张清单走。

### 9.1 基础运行层

1. 容器启动成功。
2. 能进入容器。
3. `roscore` 正常。
4. `web_controller.py` 正常。
5. `base_control` 正常。
6. `Agent` 能启动，不要求语音先成功。

### 9.2 传感器与底层链路

1. 串口底盘回包正常，`/odom` 正常。
2. 摄像头发布 `/image_raw/compressed`。
3. 雷达发布 `/scan`。
4. `camera_link -> laser_link` TF 正常。

### 9.3 视觉处理链路

1. `img_decode` 输出 `/camera/image_raw`。
2. `rknn_yolov6` 输出 `/camera/image_det` 和 `/ai_msg_det`。
3. `object_track` 输出 `/camera/image_det_track`。
4. `img_encode` 输出 `/camera/image_det_track/compressed`。

### 9.4 控制闭环

1. `/enable_tracking` 能打开跟踪。
2. `/enable_lidar` 能打开雷达融合。
3. 目标出现时 `/cmd_vel` 有合理变化。
4. 目标消失时 `/cmd_vel` 回零。

### 9.5 导航闭环

1. 能建图。
2. 能存图。
3. `map_server + amcl + move_base` 正常。
4. 能导航到预置目标点。

### 9.6 Agent 闭环

1. 配置文件可读写。
2. ASR 能连上。
3. 唤醒词生效。
4. LLM 正常返回。
5. TTS 正常播放。
6. Function Call 能触发跟随、头部动作、导航。

## 10. 当前仓库里最值得优先排查的三类故障

这三类问题是最容易让你误判“系统没起来”的。

### 10.1 编译产物缺失

如果 `docker logs robot` 里出现：

```text
Cannot locate node of type [usb_camera/usb_camera]
Cannot locate node of type [img_decode/img_decode]
Cannot locate node of type [rknn_yolov6/rknn_yolov6]
Cannot locate node of type [img_encode/img_encode]
Cannot locate node of type [ydlidar/ydlidar_node]
```

优先动作不是改 launch，而是回去检查：

```bash
find devel/lib -maxdepth 2 -type f | sort
catkin_make
```

### 10.2 串口无数据

如果 `logs/nodes/base_control/` 下最新的时间戳日志里一直是：

```text
uart read no data!
```

说明：

- `base_control` 进程起来了
- 但 MCU 侧没有有效串口回包

此时不要继续测导航和跟随，先解决底盘链路。

### 10.3 音频 / PWM / SPI 设备缺失

如果 `docker logs robot` 里出现这些关键词：

- `PulseAudio: Unable to connect`
- `Opening SPI device: No such file or directory`
- `PWM chip not found`

说明 Agent 的外设侧条件不满足。先把 Agent 降级为：

1. 配置文件测试
2. 跟随控制器测试
3. 导航控制器测试
4. 函数执行器测试

不要一上来就执着于完整语音回路。

## 11. 相关文档

建议搭配这些文档一起看：

- `doc/project_deployment_manual.md`
- `doc/board_docker_deployment.md`
- `doc/note/01_低时延相机视觉链路.md`
- `doc/note/03_Agent系统全景分析报告.md`
- `doc/note/05_跟踪算法节点全景分析报告.md`

这份文档偏“怎么做”；上面几份偏“为什么这么做”。
