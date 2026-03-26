#ifndef V4L2_H
#define V4L2_H

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>


// ROS2 低时延实验链路依旧沿用“相机直接输出 MJPEG”的策略，
// 这样在进入 DDS 之前，数据已经是压缩态，带宽和内存压力都更可控。
#define VIDEO_FORMAT V4L2_PIX_FMT_MJPEG //V4L2_PIX_FMT_YUYV

// 申请 4 个 mmap 缓冲区，兼顾流畅性和低排队深度。
#define BUFFER_COUNT 4

//使用的摄像头必须要支持MJPEG
//SUPPORT 1.Motion-JPEG
//SUPPORT 2.YUYV 4:2:2

struct FrameBuf
{
  // 当前帧数据首地址，来自 V4L2 mmap 的驱动缓冲区。
  unsigned char *start;

  // 当前帧实际有效字节数。
  int length;
};

/**
 * ROS2 版 V4L2 采集封装。
 *
 * 功能和 ROS1 版本一致：
 * - 打开设备并配置成 MJPEG；
 * - 申请 mmap 缓冲区；
 * - 提供逐帧读取接口。
 */
class V4l2
{

public:
    struct v4l2_buffer buf;//当前 dequeue / queue 的 V4L2 缓冲区描述符

    FrameBuf mmap_buffer[BUFFER_COUNT];//所有 mmap 缓冲区在用户态的映射信息

    int fd;//video 设备文件描述符

    /**
     * @param dev_name 设备节点路径。
     * @param width    希望设置的采集宽度。
     * @param height   希望设置的采集高度。
     * @return         0 成功；非 0 失败。
     */
    int init_video(const char *dev_name,int width,int height);

    /**
     * @param frame_buf 输出参数，返回当前帧地址和长度。
     * @return          0 成功；非 0 失败。
     */
    int get_data(FrameBuf *frame_buf);

    /**
     * 释放 mmap 缓冲区和设备句柄。
     */
    void release_video();
};


#endif // V4L2_H
