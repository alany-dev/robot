# 板卡 Docker 部署说明

这套部署是给 `aarch64` 板卡准备的，不是现有 `docker/build/robot_x86.dockerfile` 的简单平移。核心目标是三件事：

1. 复用你板卡上已经有的 `ubuntu:20.04` 基础镜像。
2. 让 `librga.so` / `librockchip_mpp.so` 在容器内基于宿主机 `/home/orangepi/soft` 源码重编，不再直接打包宿主机编好的二进制。
3. 让容器启动后直接拉起 `roscore + web_controller + agent + base_control`，`pkg_launch all.launch` 仍然保持可选。

## 1. 新增文件

- `docker/build/robot_board.dockerfile`
- `docker/build/runtime/robot_board_entrypoint.sh`
- `docker/docker-compose.orangepi.yml`
- `docker/scripts/prepare_rk_vendor.sh`
- `docker/scripts/robot_board_build.sh`
- `docker/scripts/robot_board_run.sh`
- `docker/scripts/robot_board_into.sh`

## 2. 依赖来源

板卡镜像构建和启动时会用到三类 RK 产物：

- `rkmpp`
  - 宿主机源码目录: `/home/orangepi/soft/rkmpp`
  - 容器启动后在容器内编译安装
- `rkrga`
  - 宿主机源码目录: `/home/orangepi/soft/rkrga`
  - 容器启动后在容器内编译安装
- `RKNN runtime`
  - `/usr/lib/librknn_api.so`
  - `/usr/lib/librknnrt.so` 或 `/lib/librknnrt.so`

`docker/scripts/prepare_rk_vendor.sh` 现在只负责把 `RKNN runtime` 二进制整理到 `docker/build/vendor/rk/`，供 Docker build 使用。
`rkmpp` 和 `rkrga` 则在容器入口里从 `/home/orangepi/soft` 挂载进来的源码目录直接编译。

## 3. 你现在还需要保证什么

下面这些是板卡侧仍然需要保证的前提：

### 3.1 必须已经有一个可用的 `ubuntu:20.04` 基础镜像

默认构建参数是：

```bash
ROBOT_BASE_IMAGE=ubuntu:20.04
```

Dockerfile 会在这张 Ubuntu 镜像里按 `docker/build/robot_x86.dockerfile` 的思路安装 ROS Noetic 和相关依赖。

如果你本地要换别的 Ubuntu 20.04 标签，直接改环境变量，例如：

```bash
export ROBOT_BASE_IMAGE=your-local-ubuntu:tag
```

### 3.2 `rkmpp` 源码目录必须存在于 `/home/orangepi/soft/rkmpp`

入口脚本会在容器里自动执行类似下面的构建：

```bash
cmake -S /home/orangepi/soft/rkmpp -B /home/orangepi/soft/rkmpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /home/orangepi/soft/rkmpp/build -j$(nproc)
```

这里的关键点不是宿主机先编好，而是源码本身能在 Ubuntu 20.04 容器里重编通过。

### 3.3 `rkrga` 源码目录必须存在于 `/home/orangepi/soft/rkrga`

入口脚本同样会在容器里重编 `rkrga`，而不是直接复用宿主机 `.so`：

```bash
cmake -S /home/orangepi/soft/rkrga -B /home/orangepi/soft/rkrga/rkrga_build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /home/orangepi/soft/rkrga/rkrga_build -j$(nproc)
```

这样 `librga.so` 和 `librockchip_mpp.so` 的 ABI 会天然对齐容器基底，不再依赖宿主机预编译库版本。

### 3.4 RKNN runtime `.so` 必须已经在宿主机系统目录

这部分不是本仓库源码直接编出来的。如果缺：

- `/usr/lib/librknn_api.so`
- `/usr/lib/librknnrt.so` 或 `/lib/librknnrt.so`

你需要从板卡 BSP / RKNN runtime 安装对应版本。

### 3.5 Docker build 仍然需要 apt / pip 源来从零安装 ROS

虽然 Ubuntu 基础镜像可以直接复用本地已有镜像，但 `robot_board.dockerfile` 还会在线安装：

- ROS Noetic `desktop-full`
- 音频工具
- NetworkManager / `nmcli`
- ROS Python 构建依赖，例如 `python3-genmsg`
- Python 依赖
- 导航相关 ROS 包

如果板卡不能访问 apt / pip 源，你需要：

- 预先准备可访问的镜像源，或者
- 自己先做一层“已装完依赖”的本地基础镜像，然后把 `ROBOT_BASE_IMAGE` 指到它

## 4. 构建和启动

### 4.0 如果根目录空间紧张，先把 Docker data-root 迁到 NVMe

默认 Docker 会把镜像、容器层、volume、build cache 都放在宿主机 `/var/lib/docker`。如果板卡根分区空间紧张，先执行：

```bash
cd /home/orangepi/projects/robot
bash docker/scripts/move_docker_data_root.sh --target /mnt/nvme/docker
```

这个脚本会做几件事：

- 停掉 `docker.service` 和 `docker.socket`
- 把当前 Docker 数据同步到 `/mnt/nvme/docker`
- 在 `/etc/docker/daemon.json` 里写入 `"data-root": "/mnt/nvme/docker"`
- 重启 Docker 并校验新的 `Docker Root Dir`
- 默认删除旧的 `/var/lib/docker`，真正释放根分区空间

如果你想先保留旧目录做二次确认，可以加：

```bash
bash docker/scripts/move_docker_data_root.sh --target /mnt/nvme/docker --keep-source
```

注意：

- 这一步需要 `sudo`
- 执行过程中会中断当前 Docker 容器
- 如果 `/mnt/nvme` 还没挂载好，不要执行

### 4.1 准备 RKNN runtime 产物

```bash
cd /home/orangepi/projects/robot
bash docker/scripts/prepare_rk_vendor.sh
```

这个步骤现在只检查并打包 `librknn_api.so` / `librknnrt.so`。
`rkmpp` 和 `rkrga` 会在容器启动时从 `/home/orangepi/soft` 内部编译。

### 4.2 构建镜像

```bash
cd /home/orangepi/projects/robot
bash docker/scripts/robot_board_build.sh
```

### 4.3 启动容器

```bash
cd /home/orangepi/projects/robot
bash docker/scripts/robot_board_run.sh
```

默认行为：

- 镜像名: `robot:board`
- 容器名: `robot`
- host 网络
- `privileged: true`
- `pid: host`
- `ipc: host`
- 仓库根目录挂载到容器内 `/home/orangepi/robot`
- 启动 `roscore`
- 启动 `web_controller.py`
- 启动 `src/agent/start_agent.sh`
- 启动 `roslaunch base_control base_control.launch`
- `agent` 默认作为可选进程处理；如果还没配置 API key，它会退出，但不会把 `roscore/web/base_control` 一起带停

### 4.4 进入容器

```bash
bash docker/scripts/robot_board_into.sh
```

## 5. 启动开关

可以通过环境变量控制入口行为：

```bash
export ROBOT_AUTO_CATKIN_MAKE=1
export ROBOT_FORCE_CATKIN_MAKE=0
export ROBOT_START_ALL_LAUNCH=0
export ROBOT_START_AGENT=1
export ROBOT_AGENT_REQUIRED=0
export ROBOT_START_BASE_CONTROL=1
export ROBOT_AP_MODE=0
```

入口脚本除了检查 `devel/setup.bash`，还会检查关键运行目标是否真的生成出来。
例如当 `ROBOT_START_BASE_CONTROL=1` 且 `devel/lib/base_control/base_control` 不存在时，会自动补跑一次 `catkin_make`，
避免宿主机挂载进来的工作区只有半成品 `devel/` 目录却被误判为“已经编译完成”。

如果你希望 `agent` 一旦退出就让容器整体失败退出，可以显式打开：

```bash
export ROBOT_AGENT_REQUIRED=1
```

默认值为 `0`，这是为了兼容板卡首次部署时还没在 Web 页面配置 API key 的情况。

例如，如果你想让容器启动后直接拉起 `pkg_launch all.launch`：

```bash
export ROBOT_START_ALL_LAUNCH=1
bash docker/scripts/robot_board_run.sh
```

## 6. 设计取舍

- 这里保留了 `privileged + host network + pid host + ipc host`，因为你这个项目本身就依赖串口、视频、PWM、音频、网络管理和 ROS 进程编排。
- 因为启用了 `pid: host`，不要再同时跑宿主机版同一套 ROS 进程，否则 `pkill` 类逻辑可能互相影响。
- 容器入口没有直接复用 `src/config/start.sh`，而是改成 Docker 友好的前台进程管理方式。原因是 `start.sh` 里大量后台化命令会让 PID 1 很快退出，不符合容器生命周期管理。

## 7. 建议的第一轮验证

先按下面顺序做，不要一上来全开：

1. `bash docker/scripts/prepare_rk_vendor.sh`
2. `bash docker/scripts/robot_board_build.sh`
3. `bash docker/scripts/robot_board_run.sh`
4. `bash docker/scripts/robot_board_into.sh`
5. 容器内执行 `rostopic list`
6. 浏览器访问 `http://板卡IP:5000`
7. 再从 Web 页面触发 `all.launch`

如果第 1 步就失败，脚本输出里已经会直接告诉你缺的是 `rkmpp`、`rkrga` 还是 `RKNN runtime`。
