# 项目部署手册

## 1. 适用范围

本手册基于仓库现有脚本、launch 文件和 Docker 配置整理，适用于以下三类部署场景：

- 板端原生部署：真实机器人主控板，推荐路径，默认目标环境为 Ubuntu 20.04 + ROS Noetic。
- x86 Docker 部署：用于开发、联调、演示和环境复现。
- 离线 Agent 可选部署：本地 ASR/LLM/TTS 语音链路，独立于在线 Agent。

当前仓库中的很多脚本写死了 `/home/orangepi/robot`、`orangepi` 用户名和部分 sudo 密码。如果你的设备路径或账号不同，请先修改脚本再部署，否则启动流程会直接失败。

## 2. 关键入口

| 路径 | 作用 |
| --- | --- |
| `src/config/install.sh` | 板端依赖安装、ROS 环境变量写入、音频依赖安装、板级外设 overlay 配置 |
| `src/config/start.sh` | 主启动脚本，负责联网检测、AP 模式切换、`roscore`、Web 控制器、Agent、底盘控制启动 |
| `src/config/rc.local` | 开机自启示例，负责权限修复、播放提示音并拉起 `start.sh` |
| `web_controller.py` | Web 运维与控制入口，默认监听 `0.0.0.0:5000` |
| `src/pkg_launch/launch/all.launch` | 主功能链路：URDF、相机、图像解码、RKNN 检测、图像编码、雷达、目标跟踪 |
| `src/robot_navigation/launch/robot_navigation.launch` | 导航链路：URDF、雷达、地图服务、AMCL、move_base |
| `src/agent/start_agent.sh` | 在线 Agent 启动脚本 |
| `offline-agent/start.sh` | 离线 Agent 启动脚本 |
| `docker/build/robot_x86.dockerfile` | x86 容器镜像定义 |
| `docker/scripts/robot_docker_run.sh` | 以 host 网络和特权模式运行容器 |

## 3. 部署前准备

### 3.1 目录与账号约定

- 仓库建议放在 `/home/orangepi/robot`。
- 默认运行用户为 `orangepi`。
- Web 控制、Agent 启动、开机自启、PWM 权限修复都依赖这两个约定。

### 3.2 软件环境

- Ubuntu 20.04
- ROS Noetic
- Python 3
- `nmcli`、`create_ap`
- PulseAudio / PortAudio
- Docker（仅容器部署需要）

### 3.3 硬件与外设

- USB 摄像头
- 激光雷达
- 底盘串口 / MCU
- 音频输入输出设备
- 板端 GPIO / PWM / SPI 设备节点

如果你只做算法或 Web 联调，可以不接真实硬件，但底盘、音频、雷达相关模块会处于不可用状态。

## 4. 板端原生部署

### 4.1 获取代码

```bash
cd /home/orangepi
git clone <your-repo-url> robot
cd /home/orangepi/robot
```

如果仓库不在这个路径，请同步修改以下脚本中的硬编码路径：

- `src/config/start.sh`
- `src/config/rc.local`
- `src/agent/start_agent.sh`
- `web_controller.py`

### 4.2 安装系统依赖

直接执行仓库自带安装脚本：

```bash
cd /home/orangepi/robot
bash src/config/install.sh
```

该脚本会完成以下工作：

- 安装 `nfs-common`、`avahi-daemon`、音频库和常用系统依赖。
- 配置 ROS Noetic 软件源并安装 `ros-noetic-desktop-full`。
- 将 `ROS_IP`、`ROS_HOSTNAME`、`ROS_MASTER_URI` 写入 `~/.bashrc`。
- 安装导航相关 ROS 包，如 `move_base`、`amcl`、`gmapping`、`teb_local_planner`。
- 安装 Python 依赖，如 `dashscope`、`openai`、`pyaudio`、`flask`、`opencv-python`。
- 配置 PulseAudio 默认输入输出设备。
- 向 `/boot/orangepiEnv.txt` 追加 SPI/UART/PWM overlay 配置。
- 最后执行重启。

注意事项：

- 脚本会修改主机名为 `orangepi`。
- 脚本会直接写 `~/.bashrc`，适合首次部署，不建议在多人共用环境反复执行。
- 音频设备名是按当前开发板环境写死的，换 USB 声卡后通常需要重配。

### 4.3 编译 ROS1 工作区

重启后执行：

```bash
source /opt/ros/noetic/setup.bash
cd /home/orangepi/robot
catkin_make
source devel/setup.bash
```

如果 `YDLidar-SDK` 相关头文件或库缺失，需要额外安装雷达 SDK。仓库里的 `src/config/install.sh` 已预留说明，Docker 构建也使用了 `docker/build/install/ydlidar-sdk/install_ydlidar_sdk.sh`。

### 4.4 准备 Agent 配置

复制示例配置：

```bash
cd /home/orangepi/robot
cp src/agent/config.json.example src/agent/config.json
```

`src/agent/config.json` 至少要确认以下字段：

- `api_key`：在线模型密钥，不填则在线 LLM 不可用。
- `model`：默认 `qwen-plus`。
- `base_url`：默认是 DashScope 兼容接口。
- `tts_model`、`tts_voice`、`enable_tts`
- `wake_words`

配置文件由 `src/agent/config_manager.py` 管理，保存时采用临时文件替换方式，避免写坏配置。

### 4.5 权限与音频检查

Agent 启动前会执行 `src/agent/fix.sh`，它会尝试：

- 为 `/sys/class/pwm/pwmchip*/export`、`unexport` 授权。
- 导出 `pwm0` 并给 `period`、`duty_cycle`、`enable`、`polarity` 写权限。

建议部署前先检查：

```bash
pacmd list-sinks | grep -e 'index:' -e 'name:'
pacmd list-sources | grep -e 'index:' -e 'name:'
```

如果设备名称与 `src/config/start.sh` 中的默认值不同，需要先改脚本中的：

- 默认扬声器 `alsa_output.platform-rk809-sound.stereo-fallback`
- 默认麦克风 `alsa_input.usb-C-Media_Electronics_Inc._USB_PnP_Sound_Device-00.mono-fallback`

## 5. 系统启动方式

### 5.1 推荐启动：使用统一启动脚本

```bash
cd /home/orangepi/robot/src/config
bash start.sh
```

`start.sh` 的行为分两种：

### 5.2 有可用网络时

脚本会按顺序执行：

1. 获取 IPv4 地址并导出 `ROS_IP`、`ROS_HOSTNAME`、`ROS_MASTER_URI`
2. `source /opt/ros/noetic/setup.sh`
3. `source /home/orangepi/robot/devel/setup.sh`
4. 设置默认麦克风与音量
5. 启动 `roscore`
6. 后台启动 `python3 web_controller.py`
7. 后台启动 `src/agent/start_agent.sh`
8. 后台启动 `roslaunch base_control base_control.launch`

启动完成后，Web 控制入口为：

```text
http://<设备IP>:5000
```

注意：视觉/雷达主链路 `pkg_launch all.launch` 不会在 `start.sh` 中自动启动，而是交给 Web 控制器按需拉起。

### 5.3 没有可用网络时

如果系统未获得 IP，脚本会自动进入 AP 模式：

- 执行 `create_ap --no-virt -m nat wlan0 eth0 orangepi orangepi`
- 启动 `python3 web_controller.py --ap-mode`
- Web 配网页面地址为 `http://192.168.12.1:5000/wifi_config`

也就是说，热点 SSID 和密码默认都是 `orangepi`。完成配网后，系统会通过 Web API 调用 `nmcli` 配置 WiFi，然后重启。

## 6. Web 控制与功能链路启动

`web_controller.py` 同时承担两类职责：

- 机器人控制入口：速度、转向、头部动作、跟随开关
- 运维入口：启动/停止 ROS 主链路、导航链路、重启 Agent、WiFi 配置、参数保存

### 6.1 启动主功能链路

Web 控制器调用的实际命令是：

```bash
roslaunch pkg_launch all.launch
```

该 launch 会启动：

- `pkg_launch/launch/urdf.launch`
- `usb_camera/launch/usb_camera.launch`
- `img_decode/launch/img_decode.launch`
- `rknn_yolov6/launch/rknn_yolov6.launch`
- `img_encode/launch/img_encode.launch`
- `ydlidar/launch/ydlidar.launch`
- `object_track/launch/object_track.launch`

### 6.2 启动导航链路

Web 控制器调用的实际命令是：

```bash
roslaunch robot_navigation robot_navigation.launch
```

该 launch 会启动：

- `pkg_launch/launch/urdf.launch`
- `ydlidar/launch/ydlidar.launch`
- `map_server`
- `amcl`
- `move_base`

因此，导航依赖地图、雷达、TF 和底盘控制同时可用。

## 7. 手动分模块启动

如果你不想走统一脚本，可以按模块手动启动。建议顺序如下：

### 7.1 基础环境

```bash
source /opt/ros/noetic/setup.bash
cd /home/orangepi/robot
source devel/setup.bash
roscore
```

### 7.2 底盘控制

```bash
roslaunch base_control base_control.launch
```

### 7.3 Web 控制器

```bash
cd /home/orangepi/robot
python3 web_controller.py
```

AP 模式单独启动：

```bash
cd /home/orangepi/robot
python3 web_controller.py --ap-mode
```

### 7.4 在线 Agent

```bash
cd /home/orangepi/robot/src/agent
bash start_agent.sh
```

### 7.5 主视觉与雷达链路

```bash
cd /home/orangepi/robot
roslaunch pkg_launch all.launch
```

### 7.6 导航链路

```bash
cd /home/orangepi/robot
roslaunch robot_navigation robot_navigation.launch
```

## 8. 开机自启部署

仓库提供了 `src/config/rc.local` 作为开机自启示例，核心动作包括：

- 关闭热点残留 `create_ap --fix-unmanaged`
- 播放开机提示音
- 配置 `/dev/spidev3.0` 和 GPIO 权限
- 创建 `/home/orangepi/.ros`
- 使用 `orangepi` 用户执行 `src/config/start.sh`

如果你希望开机自启，可以将该文件部署到系统的 `/etc/rc.local`，并确保：

- 文件可执行
- 系统启用了 rc.local 服务
- `orangepi` 用户存在
- 仓库路径与脚本一致

如果你的系统不是基于 rc.local 管理启动，建议改成 systemd service，而不是继续扩展这个脚本。

## 9. x86 Docker 部署

该方案更适合开发和演示，不是完整替代板端部署。现有脚本使用特权模式和 host 网络，目的是保留 ROS 与硬件访问能力。

### 9.1 构建镜像

```bash
cd /home/yang/projects/robot
docker build -t robot:v2.0 -f docker/build/robot_x86.dockerfile docker/build
```

说明：

- 构建上下文必须是 `docker/build`，因为 Dockerfile 要 `COPY apt/...` 和 `install/...`。
- 镜像基础环境是 Ubuntu 20.04 + ROS Noetic。

### 9.2 启动容器

```bash
cd /home/yang/projects/robot
bash docker/scripts/robot_docker_run.sh
```

该脚本会：

- 删除旧的 `robot` 容器
- 使用 `--privileged=true`
- 使用 `--network host`
- 将仓库根目录挂载到容器内 `/robot`
- 注入 `ROS_MASTER_URI` 与 `ROS_IP`

### 9.3 进入容器

```bash
bash docker/scripts/robot_docker_into.sh
```

进入容器后通常还需要：

```bash
source /opt/ros/noetic/setup.bash
cd /robot
catkin_make
source devel/setup.bash
```

如果只是做代码联调，不一定需要启动全部硬件相关节点。

## 10. 离线 Agent 可选部署

离线 Agent 的运行入口是：

```bash
cd /home/orangepi/robot/offline-agent
bash start.sh
```

该脚本启动前会检查以下可执行文件是否存在：

- `offline-agent/install/bin/sherpa-onnx-microphone-test1`
- `offline-agent/install/bin/llamacpp-ros`
- `offline-agent/install/bin/tts_server`

运行时还依赖以下模型路径：

- `offline-agent/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/...`
- `offline-agent/llamacpp-ros/models/Qwen3-0.6B-BF16.gguf`
- `offline-agent/tts/models/single_speaker_fast.bin`

需要特别说明：

- 仓库里没有看到完整的一键离线构建脚本。
- 但 `offline-agent/llamacpp-ros/CMakeLists.txt`、`offline-agent/tts/CMakeLists.txt`、`offline-agent/voice/sherpa-onnx/.../voice/CMakeLists.txt` 已明确约定安装输出目录为 `offline-agent/install/bin`。
- 实际部署时，应分别编译这三个组件，并确认二进制文件落到上述目录。

如果你只部署在线 Agent，可以跳过本节。

## 11. ROS2 可选部署

ROS2 工作区位于 `ros2_ws/`，更适合作为通信和图像链路实验环境，不是当前板端默认启动路径。

构建命令：

```bash
cd /home/orangepi/robot/ros2_ws
colcon build --symlink-install
source install/setup.bash
```

如果需要 Fast DDS 共享内存优化，按 `ros2_ws/env.md` 设置：

```bash
sudo mount -o remount,size=4G /dev/shm
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export FASTRTPS_DEFAULT_PROFILES_FILE=/home/orangepi/ros2_ws/shm_fastdds.xml
```

## 12. 部署后验证

建议按下面顺序验证：

### 12.1 进程检查

```bash
ps -ef | rg 'roscore|web_controller.py|main.py|base_control|all.launch|robot_navigation.launch'
```

### 12.2 ROS 图谱检查

```bash
rosnode list
rostopic list
```

重点确认：

- `/cmd_vel`
- `/robot_status`
- `/scan`
- 相机与检测相关话题

### 12.3 Web 检查

- 正常联网模式访问 `http://<设备IP>:5000`
- AP 模式访问 `http://192.168.12.1:5000/wifi_config`

### 12.4 日志检查

常用日志位置：

- `/tmp/web_controller.log`
- `/tmp/agent.log`
- `/tmp/base_control.log`
- `/tmp/ros_launch.log`
- `/tmp/navigation_launch.log`
- `/home/orangepi/.ros/start.log`

## 13. 常见问题

### 13.1 启动脚本直接失败

优先检查三件事：

- 仓库是否真的位于 `/home/orangepi/robot`
- 当前用户是否是 `orangepi`
- `devel/setup.sh` 是否已生成

### 13.2 Web 页面能打开，但机器人不动

通常是以下原因之一：

- `base_control.launch` 没启动
- MCU 串口或权限异常
- `/cmd_vel` 有发布，但底盘节点未消费

### 13.3 进入 AP 模式后无法配网

优先检查：

- `create_ap` 是否安装
- `nmcli` 是否可用
- 网卡名称是否仍是 `wlan0` / `eth0`

### 13.4 Agent 启动失败

先检查：

- `src/agent/config.json` 是否存在
- `pip3 install -r src/agent/requirements.txt` 是否完成
- `pyaudio` 是否可导入
- 麦克风设备名是否和脚本一致

### 13.5 雷达或导航启动失败

优先检查：

- YDLidar SDK 是否已安装
- 串口权限是否正确
- 地图文件是否存在
- `map_server`、`amcl`、`move_base` 是否都已拉起

### 13.6 Docker 内无法访问图形或硬件

当前容器方案依赖：

- `--privileged`
- `--network host`
- `DISPLAY` / `XDG_RUNTIME_DIR`

如果宿主机环境不同，需要同步调整 `docker/scripts/robot_docker_run.sh`。

## 14. 部署建议

- 首次上板部署，优先走 `install.sh -> catkin_make -> start.sh` 这条最短路径。
- 先验证 Web、底盘、Agent、主链路、导航五部分，再做整机联调。
- 不要一开始就依赖开机自启；先确认手工启动稳定，再接入 `rc.local` 或 systemd。
- 生产化前建议去掉脚本中的明文密码、宽权限 `chmod 777` 和硬编码设备名。
