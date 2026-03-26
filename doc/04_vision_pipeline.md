# 低时延相机视觉处理全链路

## 1. 这个功能到底解决什么问题

这一部分把相机采集、图像解码、硬件加速、目标检测、多传感器融合跟踪和结果回传串成了一条完整视觉链路。真正的亮点不是“用了 YOLO”，而是整条链路都在围绕低时延做设计：相机只在有人订阅时工作，解码尽量复用硬件能力，推理与后处理分线程，跟踪阶段复用雷达和 TF 做距离估计。

## 2. 先看哪些文件

建议按流水线顺序阅读：

- `src/usb_camera/src/v4l2.cpp`
- `src/usb_camera/src/usb_camera.cpp`
- `src/img_decode/src/img_decode.cpp`
- `src/img_encode/src/img_encode.cpp`
- `src/rknn_yolov6/src/main.cpp`
- `src/rknn_yolov6/src/rknn_run.cpp`
- `src/rknn_yolov6/src/postprocess.cpp`
- `src/object_track/src/object_track.cpp`
- `src/ai_msgs/msg/Det.msg`
- `src/ai_msgs/msg/Dets.msg`

## 3. 启动路径 / 数据流 / 控制流

完整链路可以用下面的 launch 组合理解：

```bash
catkin_make
source devel/setup.bash
roslaunch pkg_launch all.launch
```

如果你想拆开学，建议按这个顺序单独启动：

1. `roslaunch usb_camera usb_camera.launch`
2. `roslaunch img_decode img_decode.launch`
3. `roslaunch rknn_yolov6 rknn_yolov6.launch`
4. `roslaunch object_track object_track.launch`
5. `roslaunch img_encode img_encode.launch`

主数据流是：V4L2 采集 MJPEG -> 解码/缩放 -> YOLO 检测 -> 图像与检测结果同步 -> 融合雷达估距离并输出跟踪控制 -> 重新编码显示或传输。

## 4. 亮点实现细节

### 4.1 相机采集从驱动层就开始控延迟

`v4l2.cpp` 使用 `mmap` 缓冲和 `select()` 等待，这比简单同步读取更接近高吞吐相机程序的标准写法。`usb_camera.cpp` 会根据订阅者情况决定是否真正打开设备，避免相机在没人消费时白白采集。

### 4.2 解码节点区分 RK 硬件路径和 x86 回退路径

`img_decode.cpp` 在 ARM/RK 平台走 RK MPP 解码与 RGA 缩放，在 x86 上回退到 OpenCV。这里的阅读重点不是 API 细节，而是作者保留了统一节点职责，同时按平台切换实现，从而兼顾板端性能与桌面调试效率。

### 4.3 时间戳被刻意保留

解码后继续沿用原始时间戳，这一点对整条链路非常重要。没有时间戳一致性，后面的检测延迟测量、图像与雷达同步、跟踪控制都容易失真。

### 4.4 检测节点采用多线程分工

`rknn_yolov6` 不是单线程“收图 -> 推理 -> 后处理 -> 发布”，而是把推理、处理、检查拆成多个线程。对于 NPU 设备来说，这种流水化分工能更好地掩盖不同阶段耗时，减少整链路抖动。

### 4.5 后处理部分是真正的目标检测工程细节

`postprocess.cpp` 里包含 stride 8/16/32 的解码和 NMS，不只是“调库调用”。如果你要理解模型输出怎样变成机器人可用目标框，这部分必须亲自跟一遍。

### 4.6 跟踪节点体现了多传感器融合思维

`object_track.cpp` 通过 ExactTime 同步图像和检测结果，选择目标类别中最合适的框，再利用 TF、相机标定和可选手工偏移，把雷达点投影到图像坐标系估算距离。角速度来自框中心偏差，线速度来自雷达距离，这比单纯图像框跟踪可靠得多。

### 4.7 跟踪控制有明确的使能开关

只有 `enable_tracking` 打开时才发布 `/cmd_vel`。这说明该节点不是“只要检测到人就强制控制机器人”，而是被设计成可安全接入整机系统的一环。

### 4.8 重编码链路保证可视化和回传

`img_encode.cpp` 把跟踪后的 RGB 图像重新编码成 JPEG，用于显示或网络传输。RK 平台下继续使用 RGA/MPP 做颜色转换和编码，说明作者关注的不只是感知内部性能，也关注结果输出链路的成本。

## 5. 调试与验证方法

- 单独验证相机采集，确认无人订阅时设备不会持续输出。
- 验证解码节点是否正确继承原始时间戳。
- 检测节点重点看推理线程是否堵塞、后处理是否成为瓶颈。
- 跟踪节点重点校验 TF、相机内参、雷达到相机外参是否正确，否则距离会失真。
- 如需评估整链路延迟，必须从采集时间戳一路跟到跟踪输出或重编码输出。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先按采集、解码、检测、跟踪四段把代码入口找齐，建立总流水线地图。

### 2 小时

重点把 `usb_camera.cpp`、`img_decode.cpp`、`rknn_yolov6/src/main.cpp`、`object_track.cpp` 四个核心节点读完，搞清楚每段输入输出和线程/回调关系。

### 1 天

自己测一次完整链路延迟，并验证几种异常情况：无人订阅、检测延迟突增、TF 配置错误、雷达缺失时的行为。到这一步你才真正吃透这个模块的工程亮点。

## 7. 你学完后应该能回答什么问题

- 为什么这条视觉链路的价值在“全链路低时延”，而不是单个模型精度。
- RK 平台与 x86 平台分别承担什么职责。
- 时间戳在视觉系统里为什么比很多人想象得更重要。
- 目标跟踪为什么要融合雷达，而不是只靠图像框中心。
- 这条链路的真正瓶颈更可能出现在采集、搬运、推理还是同步阶段。
