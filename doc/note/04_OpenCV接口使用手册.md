# OpenCV接口使用手册

## 1. 这份手册覆盖什么

这份手册面向本项目的初学者，目标不是讲完整 OpenCV 教程，而是讲清楚：

- 这个项目里到底用了哪些 OpenCV API
- 每个 API 在项目里是干什么的
- 你在看源码时，应该先理解哪些数据结构
- 常见坑有哪些，尤其是 `BGR/RGB`、`cv::Mat` 生命周期、缩放后的相机内参

本手册覆盖仓库主工作区里显式使用 OpenCV 的模块，重点是：

- `src/img_decode`
- `src/img_encode`
- `src/rknn_yolov6`
- `src/object_track`
- `src/agent` 中少量 `cv2` 用法
- `ros2_ws/src/img_decode` 中与 ROS1 对应的重复用法

不展开 vendor-heavy 目录里的第三方代码。

---

## 2. 先建立一个整体认识

在这个项目里，OpenCV 主要扮演 6 类角色：

1. 图像容器
   `cv::Mat` 是整条视觉链路里最常见的数据载体。

2. 图像编解码
   例如 `imdecode()` 把 JPEG 字节变成图像，`imencode()` 把图像压成 JPEG。

3. 颜色空间和尺寸变换
   例如 `cvtColor()`、`resize()`。

4. 图像绘制和调试
   例如 `rectangle()`、`putText()`、`circle()`、`imshow()`。

5. 标定参数读取和几何投影
   例如 `FileStorage`、`projectPoints()`。

6. 传统视觉备用方案
   例如 `CascadeClassifier` 和 `detectMultiScale()`。

一句话理解项目数据流：

`压缩图像字节 -> OpenCV解码成Mat -> 缩放/转色彩 -> RKNN推理 -> OpenCV画框 -> 发布ROS图像`

还有一条融合链路：

`激光点 -> 变换到相机坐标系 -> OpenCV投影到像素平面 -> 和检测框做匹配`

---

## 3. 你必须先认识的核心对象

### 3.1 `cv::Mat`

`cv::Mat` 是 OpenCV 最核心的图像矩阵类型。你可以把它理解成：

- 一张图片
- 一个二维矩阵
- 一块带有行列信息和类型信息的像素内存

项目里常见的 `cv::Mat` 用法有 4 种：

1. 从 OpenCV API 返回
   例如 `cv::imread()`、`cv::imdecode()`。

2. 自己新建一块图像内存
   例如：

```cpp
cv::Mat img_resize(model_height, model_width, CV_8UC3);
```

3. 包装现有外部 buffer，不拷贝数据
   例如：

```cpp
cv::Mat image(image_msg->height, image_msg->width, CV_8UC3,
              (char *)image_msg->data.data());
```

4. 生成固定大小、固定类型的零矩阵
   例如：

```cpp
cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
```

项目里的代表位置：

- `src/img_decode/src/img_decode.cpp`
- `src/object_track/src/object_track.cpp`
- `src/rknn_yolov6/src/rknn_run.cpp`

### 3.2 `cv::Size`

`cv::Size(width, height)` 表示尺寸。常用于：

- `resize()` 的目标大小
- 保存图像宽高
- 表示模型输入尺寸

### 3.3 `cv::Point` / `cv::Point2d` / `cv::Point3d`

- `cv::Point(x, y)`：整数像素点
- `cv::Point2d(x, y)`：双精度二维点
- `cv::Point3d(x, y, z)`：双精度三维点

项目里：

- 画框、画点时用 `cv::Point`
- 激光点投影时用 `cv::Point3d`
- 投影后的像素坐标用 `cv::Point2d`

### 3.4 `cv::Scalar`

`cv::Scalar(b, g, r)` 或 `cv::Scalar(v0, v1, v2, v3)` 通常用来表示颜色。

例如绿色：

```cpp
cv::Scalar(0, 255, 0)
```

### 3.5 `cv::Rect`

`cv::Rect` 表示矩形区域，传统检测器 `detectMultiScale()` 会返回它。

### 3.6 `cv::Vec3b` / `cv::Vec3f`

- `cv::Vec3b`：3 个 `uchar`，通常表示 RGB/BGR 像素
- `cv::Vec3f`：3 个 `float`，通常表示三维点

项目里在 `save_ply()` 中用它们按像素访问点云和颜色。

---

## 4. 项目里 OpenCV 用在哪些模块

| 模块 | 典型文件 | OpenCV 的角色 |
| --- | --- | --- |
| 图像解码 | `src/img_decode/src/img_decode.cpp` | `imdecode()`、`cvtColor()`、`resize()` |
| 图像编码 | `src/img_encode/src/img_encode.cpp` | `cvtColor()`、`imencode()` |
| RKNN 推理节点 | `src/rknn_yolov6/src/rknn_run.cpp` | 离线读图、颜色转换、画框、保存结果 |
| 跟踪融合 | `src/object_track/src/object_track.cpp` | 读取标定、投影激光点、绘制调试信息 |
| Python 小屏/UI | `src/agent/emoji.py`、`src/agent/lcd.py` | `cv2.putText()`、`cv2.resize()`、`cv2.imshow()`、颜色格式转换 |
| ROS2 图像解码 | `ros2_ws/src/img_decode/src/img_decode.cpp` | 与 ROS1 基本一致 |

---

## 5. 本项目里真正出现过的 OpenCV API 总表

### 5.1 C++ `cv::` API

项目扫描到的唯一 `cv::` 接口如下：

- `cv::Mat`
- `cv::Mat::zeros`
- `cv::Size`
- `cv::Point`
- `cv::Point2d`
- `cv::Point3d`
- `cv::Rect`
- `cv::Scalar`
- `cv::String`
- `cv::Vec3b`
- `cv::Vec3f`
- `cv::FileStorage`
- `cv::FileStorage::READ`
- `cv::imdecode`
- `cv::imencode`
- `cv::imread`
- `cv::imwrite`
- `cv::cvtColor`
- `cv::resize`
- `cv::glob`
- `cv::projectPoints`
- `cv::circle`
- `cv::format`
- `cv::CascadeClassifier`
- `cv::Exception`
- `cv::imshow`
- `cv::waitKey`
- `cv::COLOR_BGR2RGB`
- `cv::COLOR_RGB2BGR`
- `cv::COLOR_RGB2GRAY`
- `cv::IMREAD_COLOR`
- `cv::IMWRITE_JPEG_QUALITY`
- `cv::FONT_HERSHEY_SIMPLEX`
- `cv::CASCADE_SCALE_IMAGE`

说明：

- `rectangle()`、`putText()` 在源码里有时没有写 `cv::` 前缀，但它们仍然是 OpenCV 接口。
- 项目里没有大量使用高级 OpenCV 算法，更多是“图像 I/O + 绘制 + 标定投影”。

### 5.2 Python `cv2` API

项目扫描到的唯一 `cv2` 接口如下：

- `cv2.imread`
- `cv2.cvtColor`
- `cv2.resize`
- `cv2.putText`
- `cv2.getTextSize`
- `cv2.circle`
- `cv2.imshow`
- `cv2.waitKey`
- `cv2.flip`
- `cv2.namedWindow`
- `cv2.setWindowProperty`
- `cv2.COLOR_BGR2BGR565`
- `cv2.FONT_HERSHEY_SIMPLEX`
- `cv2.WND_PROP_FULLSCREEN`
- `cv2.WINDOW_FULLSCREEN`

其中有几项只出现在注释里或调试路径里，但都属于项目里实际出现过的接口。

---

## 6. 按用途分类讲解每个 OpenCV 接口

## 6.1 图像输入、输出、编解码

### `cv::imdecode()`

作用：

- 把“压缩后的图像字节”解码为 `cv::Mat`

项目位置：

- `src/img_decode/src/img_decode.cpp:102`
- `src/img_decode/src/shm_img_decode.cpp:61`
- `ros2_ws/src/img_decode/src/img_decode.cpp:118`

项目示例：

在 `img_decode` 里，输入是 ROS 压缩图像消息中的 JPEG 字节，代码先把字节包装成 `cv::Mat`，再用 `imdecode()` 解码：

- `src/img_decode/src/img_decode.cpp:102`

关键点：

- `imdecode()` 默认读出的是 `BGR`
- 项目后续链路统一使用 `RGB`
- 所以紧接着就调用了 `cvtColor(BGR -> RGB)`

### `cv::imencode()`

作用：

- 把 `cv::Mat` 压缩为 JPEG、PNG 等字节流

项目位置：

- `src/img_encode/src/img_encode.cpp:69`

项目示例：

- `src/img_encode/src/img_encode.cpp:63-69`

关键点：

- OpenCV 的 JPEG 编码输入通常按 `BGR` 理解
- 所以项目先把 `RGB` 转回 `BGR`，再 `imencode(".jpg", ...)`

### `cv::imread()`

作用：

- 从磁盘读图

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:219`

用途：

- RKNN 离线测试模式，逐张读取本地图片做推理

### `cv::imwrite()`

作用：

- 把图像写回磁盘

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:560`

用途：

- 离线模式保存画好框的结果图

### `cv::IMREAD_COLOR`

作用：

- 读取彩色图

项目位置：

- `src/img_decode/src/img_decode.cpp:102`

### `cv::IMWRITE_JPEG_QUALITY`

作用：

- 指定 JPEG 压缩质量参数

项目位置：

- `src/img_encode/src/img_encode.cpp:67`

---

## 6.2 颜色空间转换

### `cv::cvtColor()`

作用：

- 图像颜色空间转换

项目里实际出现的转换有：

- `cv::COLOR_BGR2RGB`
- `cv::COLOR_RGB2BGR`
- `cv::COLOR_RGB2GRAY`

项目位置：

- `src/img_decode/src/img_decode.cpp:108`
- `src/img_encode/src/img_encode.cpp:65`
- `src/rknn_yolov6/src/rknn_run.cpp:220`
- `src/rknn_yolov6/src/rknn_run.cpp:440`
- `src/agent/lcd.py:89`

你要重点记住：

1. OpenCV 大多数读图 API 默认常常是 `BGR`
2. 这个项目的 ROS 图像主链路很多地方用的是 `RGB`
3. 所以你经常会看到 `BGR <-> RGB`

如果你把颜色顺序搞错，最常见现象是：

- 红蓝通道互换
- 模型输入效果异常
- 编码后颜色失真

---

## 6.3 图像尺寸变换

### `cv::resize()`

作用：

- 改变图像分辨率

项目位置：

- `src/img_decode/src/img_decode.cpp:136`
- `src/img_decode/src/shm_img_decode.cpp:85`
- `ros2_ws/src/img_decode/src/img_decode.cpp:145`
- `src/agent/emoji.py:349`

项目用途：

1. 前置小图
   把大图缩成推理更快的小图

2. PC 调试显示
   把 240x240 的小屏图像放大到电脑窗口里看

关键参数：

- `cv::Size()` 传空表示用缩放因子
- `fx`、`fy` 是横纵向缩放比例

项目示例：

- `src/img_decode/src/img_decode.cpp:136`
- `src/agent/emoji.py:349`

---

## 6.4 文件和配置读取

### `cv::FileStorage`

作用：

- 读取 OpenCV 风格的 YAML/XML 配置

项目位置：

- `src/object_track/src/object_track.cpp:44`
- `src/rknn_yolov6/src/rknn_run.cpp:64`

项目有两类典型配置：

1. 相机标定文件
   `src/object_track/cfg/fisheye.yml`

2. YOLO 类别文件
   `src/rknn_yolov6/config/yolov6.yaml`

项目示例：

- `src/object_track/src/object_track.cpp:44-58`

你需要理解的调用顺序：

1. `cv::FileStorage fs(path, cv::FileStorage::READ);`
2. `fs["key"] >> variable;`
3. `fs.release();`

### `cv::FileStorage::READ`

作用：

- 只读方式打开配置文件

---

## 6.5 路径与文件列表

### `cv::glob()`

作用：

- 根据路径模式收集文件列表

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:180`

项目用途：

- 离线推理模式下，读取一批测试图片

关键点：

- 第 3 个参数控制是否递归
- 当前项目传的是 `false`

---

## 6.6 绘制、调试和可视化

### `cv::rectangle()`

作用：

- 画矩形框

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:478`
- `src/object_track/src/object_track.cpp:322`

用途：

- 画检测框
- 画当前跟踪目标框

### `cv::putText()`

作用：

- 画文字

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:479`
- `src/object_track/src/object_track.cpp:324`
- `src/object_track/src/object_track.cpp:332`
- `src/agent/emoji.py:285`

用途：

- 显示类别和置信度
- 显示速度、距离、状态
- 在 LCD/PC 调试画面显示文字

### `cv::circle()`

作用：

- 画圆

项目位置：

- `src/object_track/src/object_track.cpp:346`
- `src/agent/lcd.py:403`

用途：

- 在图像上画激光投影点
- 在 LCD 调试画面画圆形元素

### `cv::format()`

作用：

- 生成格式化字符串

项目位置：

- `src/object_track/src/object_track.cpp:324`

用途：

- 直接把像素偏差、距离、速度格式化后画到图像上

### `cv::FONT_HERSHEY_SIMPLEX`

作用：

- 文字绘制时使用的字体常量

项目位置：

- `src/object_track/src/object_track.cpp:324`
- `src/rknn_yolov6/src/rknn_run.cpp:479`
- `src/agent/emoji.py:268`
- `src/agent/lcd.py:402`

### `cv::imshow()` 和 `cv::waitKey()`

作用：

- 本地弹窗显示图像
- 处理键盘事件

项目位置：

- `src/agent/emoji.py:351-352`
- C++ 模块中也有一些注释掉的调试代码

关键点：

- 这两个 API 更适合桌面调试
- 在无 GUI 的板端环境里，通常会被关闭或注释掉

### `cv2.getTextSize()`

作用：

- 计算文本像素尺寸

项目位置：

- `src/agent/emoji.py:283`
- `src/agent/emoji.py:289`
- `src/agent/emoji.py:295`
- `src/agent/emoji.py:301`
- `src/agent/emoji.py:308`
- `src/agent/emoji.py:315`

用途：

- 先算文本宽度，再居中排版

---

## 6.7 几何、标定和投影

### `cv::projectPoints()`

作用：

- 把三维点投影到图像平面

项目位置：

- `src/object_track/src/object_track.cpp:438`

这是 `object_track` 非常关键的一个接口。它把激光雷达点先变换到相机坐标系，再按照相机内参矩阵 `K` 和畸变系数 `D` 投影为像素点。

项目示例：

- `src/object_track/src/object_track.cpp:423-438`

你要理解它的输入：

- `camera_points`
  相机坐标系下的 3D 点

- `rvec`
  旋转向量

- `tvec`
  平移向量

- `K`
  相机内参矩阵

- `D`
  畸变系数

- `pixels`
  投影结果

### `cv::Mat::zeros()`

作用：

- 快速创建全 0 矩阵

项目位置：

- `src/object_track/src/object_track.cpp:431-432`

用途：

- 初始化 `projectPoints()` 需要的 `rvec` 和 `tvec`

### `cv::Exception`

作用：

- OpenCV 异常类型

项目位置：

- `src/object_track/src/object_track.cpp:439`

用途：

- 捕获 `projectPoints()` 调用异常

---

## 6.8 传统视觉检测备用方案

### `cv::CascadeClassifier`

作用：

- OpenCV 的传统级联分类器

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:378-383`

项目用途：

- 当 `USE_ARM_LIB != 1` 时，代码里有一个备用的人脸检测示例路径

### `detectMultiScale()`

作用：

- 运行级联检测器

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:443-446`

调用前通常会先转灰度：

- `src/rknn_yolov6/src/rknn_run.cpp:439-440`

### `cv::CASCADE_SCALE_IMAGE`

作用：

- 级联检测时的标志位

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:445`

这部分不是当前主链路重点，但你在源码里会遇到。

---

## 6.9 像素和点云按元素访问

### `Mat::at<T>()`

作用：

- 按行列访问 `cv::Mat` 中的元素

项目位置：

- `src/object_track/src/object_track.cpp:433-435`
- `src/rknn_yolov6/src/utils.cpp:269`
- `src/rknn_yolov6/src/utils.cpp:292`
- `src/rknn_yolov6/src/utils.cpp:296`

项目示例：

- `src/rknn_yolov6/src/utils.cpp:269-300`

这里的典型模式是：

```cpp
cv::Vec3f point = points.at<cv::Vec3f>(y, x);
cv::Vec3b color = color_img.at<cv::Vec3b>(y, x);
```

说明：

- `points` 图里每个像素存一个三维点，用 `Vec3f`
- `color_img` 图里每个像素存 3 个颜色通道，用 `Vec3b`

---

## 6.10 Python `cv2` 相关接口

### `cv2.imread()`

作用：

- 读图

项目位置：

- `src/agent/emoji.py:87`

### `cv2.putText()` 和 `cv2.getTextSize()`

作用：

- 画字
- 在画之前测量文本尺寸

项目位置：

- `src/agent/emoji.py:283-317`
- `src/agent/lcd.py:402`

对于小屏 UI，这组接口非常常见，因为要先算文字宽度再居中。

### `cv2.resize()`

作用：

- 调试显示时放大图像

项目位置：

- `src/agent/emoji.py:349`

### `cv2.imshow()` 和 `cv2.waitKey()`

作用：

- PC 屏幕实时显示
- 获取按键

项目位置：

- `src/agent/emoji.py:351-352`

### `cv2.cvtColor(..., cv2.COLOR_BGR2BGR565)`

作用：

- 把常见的 `BGR888` 图像转换成 `BGR565`

项目位置：

- `src/agent/lcd.py:89`

用途：

- 给 LCD 屏幕驱动准备更紧凑的像素格式

### `cv2.circle()`

项目位置：

- `src/agent/lcd.py:403`

### `cv2.flip()`、`cv2.namedWindow()`、`cv2.setWindowProperty()`

项目里这些接口主要出现在注释或备用调试代码中，说明作者曾经考虑过：

- 图像镜像
- 全屏显示窗口

---

## 7. 本项目里最常见的 OpenCV 数据流

## 7.1 压缩图像消息 -> OpenCV 图像

典型文件：

- `src/img_decode/src/img_decode.cpp`

步骤：

1. 拿到 ROS 压缩图像消息里的 JPEG 字节
2. `cv::imdecode()` 解码成 `cv::Mat`
3. `cv::cvtColor(BGR -> RGB)`
4. `cv::resize()` 或 RGA 缩放
5. 发布成后续推理节点使用的 RGB 图像

关键参考：

- `src/img_decode/src/img_decode.cpp:102-108`
- `src/img_decode/src/img_decode.cpp:136-137`

## 7.2 ROS 图像消息 -> JPEG 压缩消息

典型文件：

- `src/img_encode/src/img_encode.cpp`

步骤：

1. `cv_bridge::toCvShare()` 把 ROS 图像包装成 OpenCV 图像
2. `cv::cvtColor(RGB -> BGR)`
3. `cv::imencode(".jpg", ...)`
4. 发布压缩消息

关键参考：

- `src/img_encode/src/img_encode.cpp:47`
- `src/img_encode/src/img_encode.cpp:64-69`

## 7.3 离线图片 -> RKNN 推理 -> 保存结果图

典型文件：

- `src/rknn_yolov6/src/rknn_run.cpp`

步骤：

1. `cv::glob()` 找到待测试图片
2. `cv::imread()` 逐张读取
3. `cv::cvtColor(BGR -> RGB)` 供模型推理
4. 画框
5. `cv::imwrite()` 存回磁盘

关键参考：

- `src/rknn_yolov6/src/rknn_run.cpp:180`
- `src/rknn_yolov6/src/rknn_run.cpp:219-220`
- `src/rknn_yolov6/src/rknn_run.cpp:478-479`
- `src/rknn_yolov6/src/rknn_run.cpp:557-560`

## 7.4 激光点 -> 图像像素

典型文件：

- `src/object_track/src/object_track.cpp`

步骤：

1. 从标定文件读取 `K` 和 `D`
2. 把激光点变换到相机坐标系
3. 组织成 `std::vector<cv::Point3d>`
4. `cv::projectPoints()` 投影到 2D 像素
5. 用 `cv::circle()` 画到图像上

关键参考：

- `src/object_track/src/object_track.cpp:44-58`
- `src/object_track/src/object_track.cpp:423-438`
- `src/object_track/src/object_track.cpp:346-347`

---

## 8. 这些接口在项目源码里的典型例子

## 8.1 解码压缩图像

来源：

- `src/img_decode/src/img_decode.cpp:102-108`

含义：

- `imdecode()` 先读出 `BGR`
- `cvtColor()` 再转成项目约定的 `RGB`

## 8.2 读取相机标定

来源：

- `src/object_track/src/object_track.cpp:44-58`

含义：

- 用 `FileStorage` 从 YAML 读 `CameraMat`、`DistCoeff`、`ImageSize`

## 8.3 投影 3D 点到图像

来源：

- `src/object_track/src/object_track.cpp:423-438`

含义：

- `projectPoints()` 是把三维点映射到图像坐标的标准接口

## 8.4 画检测框和提示文字

来源：

- `src/rknn_yolov6/src/rknn_run.cpp:478-479`
- `src/object_track/src/object_track.cpp:322-347`

含义：

- `rectangle()` 画框
- `putText()` 写字
- `circle()` 画投影点

## 8.5 访问点云矩阵中的每个元素

来源：

- `src/rknn_yolov6/src/utils.cpp:269-300`

含义：

- `points.at<cv::Vec3f>(y, x)` 读取三维点
- `color_img.at<cv::Vec3b>(y, x)` 读取颜色像素

---

## 9. 虽然不是 OpenCV，但你会一起遇到的相关接口

这些接口不是 OpenCV 本身，但和 OpenCV 在本项目里是绑在一起用的。

### `cv_bridge::toCvShare()`

作用：

- 把 ROS `sensor_msgs/Image` 包装成 OpenCV 图像

项目位置：

- `src/img_encode/src/img_encode.cpp:47`
- `src/rknn_yolov6/src/rknn_run.cpp:236`

特点：

- 共享底层数据，减少拷贝

### `cv_bridge::CvImage(...).toImageMsg()`

作用：

- 把 OpenCV 图像重新包装成 ROS 图像消息

项目位置：

- `src/rknn_yolov6/src/rknn_run.cpp:544`

这两个接口是 ROS + OpenCV 项目里最常见的桥接点。

---

## 10. 新手最容易踩的坑

## 10.1 `BGR` 和 `RGB` 混淆

这是本项目里最容易出问题的点。

记忆方式：

- OpenCV 读图默认常常是 `BGR`
- 项目链路很多地方统一成 `RGB`
- 编码 JPEG 前又经常要转回 `BGR`

你应该重点检查：

- `imdecode()` 之后有没有 `BGR -> RGB`
- `imencode()` 之前有没有 `RGB -> BGR`
- 绘图颜色是不是按你以为的通道顺序写的

## 10.2 `cv::Mat` 只是“包了一层”，不一定拷贝了数据

例如：

```cpp
cv::Mat image(image_msg->height, image_msg->width, CV_8UC3,
              (char *)image_msg->data.data());
```

这种写法通常只是“引用已有内存”，不是深拷贝。

影响：

- 改 `image`，可能就等于改了原消息数据
- 原始数据生命周期结束后，`image` 也可能失效

所以源码里你会看到：

- 有些地方直接共享
- 有些地方显式 `clone()`

例如：

- `src/rknn_yolov6/src/rknn_run.cpp:395`

## 10.3 缩图之后，相机内参也要跟着缩

`object_track` 里最关键的坑之一就是这个。

如果原始标定是 `1280x720`，但实际参与跟踪的是 `640x360`，那么：

- `fx`
- `fy`
- `cx`
- `cy`

都要按比例缩小。

项目位置：

- `src/object_track/src/object_track.cpp:71-76`

如果你忘了这一步，激光投影点会整体偏移。

## 10.4 `imshow()` 不是板端常规方案

在桌面电脑上调试，`imshow()` 很方便。

但在无桌面环境的板子上：

- 可能没有 GUI
- 可能会卡住
- 可能根本弹不出窗口

所以本项目里多数 C++ `imshow()` 都是注释掉的，只保留少量 Python 调试显示。

## 10.5 `projectPoints()` 不是“鱼眼专用”

当前项目虽然文件名里有 `fisheye.yml`，但代码注释已经说明当前使用的是普通 `cv::projectPoints()` 路径，不是 `cv::fisheye::*` 那套接口。

位置：

- `src/object_track/src/object_track.cpp:408-409`

---

## 11. 给初学者的阅读顺序建议

如果你第一次读这个项目里的 OpenCV 代码，建议按这个顺序：

1. 先读 `src/img_decode/src/img_decode.cpp`
   你会学会 `imdecode()`、`cvtColor()`、`resize()`。

2. 再读 `src/img_encode/src/img_encode.cpp`
   你会理解图像怎么重新压回 JPEG。

3. 再读 `src/rknn_yolov6/src/rknn_run.cpp`
   你会看到 `imread()`、`glob()`、`rectangle()`、`putText()`、`imwrite()`。

4. 再读 `src/object_track/src/object_track.cpp`
   你会理解 `FileStorage`、`projectPoints()`、`circle()` 这些更“几何”的接口。

5. 最后看 `src/agent/emoji.py`
   你会看到 Python 版 `cv2.putText()`、`cv2.getTextSize()`、`cv2.imshow()`。

---

## 12. 一页速查表

| 你想做什么 | 优先看的 API |
| --- | --- |
| 从 JPEG 字节解码图片 | `cv::imdecode` |
| 从磁盘读图 | `cv::imread` |
| 保存图片 | `cv::imwrite` |
| 把图压缩成 JPEG | `cv::imencode` |
| 转换颜色空间 | `cv::cvtColor` |
| 缩放图像 | `cv::resize` |
| 读取 YAML 标定文件 | `cv::FileStorage` |
| 扫描离线图片列表 | `cv::glob` |
| 画矩形框 | `cv::rectangle` |
| 画文字 | `cv::putText` |
| 画圆点 | `cv::circle` |
| 格式化调试字符串 | `cv::format` |
| 把 3D 点投影到图像 | `cv::projectPoints` |
| 传统检测备用方案 | `cv::CascadeClassifier` + `detectMultiScale` |
| 访问矩阵中的像素/点 | `mat.at<T>()` + `cv::Vec3b` / `cv::Vec3f` |
| ROS 图像转 OpenCV | `cv_bridge::toCvShare` |
| OpenCV 图像转 ROS 图像 | `cv_bridge::CvImage(...).toImageMsg()` |

---

## 13. 你接下来最值得亲手试的 3 个实验

### 实验 1：把 `img_decode` 里的 `BGR -> RGB` 注释掉再恢复

你会立刻理解颜色通道错位是什么效果。

### 实验 2：把 `object_track` 里内参缩放那几行去掉再恢复

你会直观看到投影点整体偏移。

### 实验 3：在 `rknn_yolov6` 离线模式下改 `putText()` 内容

你会快速学会怎么在图上叠加自己的调试信息。

---

## 14. 总结

这个项目对 OpenCV 的使用并不复杂，重点不是“算法很多”，而是“链路很长”。

你真正需要掌握的是：

- `cv::Mat` 是什么
- `BGR/RGB` 怎么流转
- 图像字节、ROS 消息、`cv::Mat` 之间怎么转换
- 标定矩阵 `K/D` 为什么会影响投影
- 绘图调试怎么做

只要把这几件事理解了，这个项目里 90% 的 OpenCV 代码你都能看懂。
