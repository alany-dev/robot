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

#include <ros/ros.h>
#include <string>

// V4L2 驱动申请 4 个 mmap 环形缓冲区：
// 数量太少会更容易在消费端抖动时卡住采集；
// 数量太大则会增大驱动侧排队深度，不利于“只关心最新帧”的低时延目标。
#define BUFFER_COUNT 4

//使用的摄像头必须要支持MJPEG
//SUPPORT 1.Motion-JPEG
//SUPPORT 2.YUYV 4:2:2

struct FrameBuf
{
  // start:
  // 指向当前帧数据首地址。这个地址来自 V4L2 mmap 出来的驱动缓冲区，
  // 并不归调用者分配，也不应该由调用者释放。
  unsigned char *start;

  // length:
  // 当前帧实际有效字节数。对于 MJPEG，通常小于驱动申请的缓冲区总大小。
  int length;
};

enum class CaptureFormat
{
  kMjpeg = 0,
  kYuyv,
};

bool parse_capture_format(const std::string &format_name, CaptureFormat *format);
bool v4l2_pixfmt_to_capture_format(__u32 pixfmt, CaptureFormat *format);
const char *capture_format_to_string(CaptureFormat format);
const char *capture_format_to_message_format(CaptureFormat format);
const char *capture_format_to_image_encoding(CaptureFormat format);
__u32 capture_format_to_v4l2_pixfmt(CaptureFormat format);

/**
 * 对 V4L2 采集过程做一个很薄的封装。
 *
 * 这个类只负责三件事：
 * 1. 打开摄像头并配置成目标分辨率 / 帧率 / 像素格式。
 * 2. 申请并管理 mmap 缓冲区。
 * 3. 从驱动 dequeue 最新帧，再立即 queue 回去供下一帧复用。
 *
 * 这样 `usb_camera.cpp` 可以只关心“什么时候采、什么时候发”，
 * 采集细节都集中在这里。
 */
class V4l2
{

public:
    V4l2();

    // buf:
    // V4L2 当前正在 dequeue / queue 的缓冲区描述符。
    struct v4l2_buffer buf;

    // mmap_buffer:
    // 保存每个驱动缓冲区映射到用户态后的虚拟地址和长度。
    FrameBuf mmap_buffer[BUFFER_COUNT];

    // fd:
    // 摄像头设备文件描述符，例如 /dev/video0 打开后得到的句柄。
    int fd;

    // 驱动最终协商成功的采集格式。
    CaptureFormat active_format;

    /**
     * 初始化摄像头采集。
     *
     * @param dev_name 设备节点路径，例如 "/dev/video0"。
     * @param width    期望采集宽度。驱动可能会协商成最接近的可用值。
     * @param height   期望采集高度。驱动可能会协商成最接近的可用值。
     * @param format   期望采集格式，支持 MJPEG / YUYV。
     * @param fps      期望采集帧率。
     * @return         0 表示初始化成功；小于 0 表示打开设备或配置流失败。
     */
    int init_video(const char *dev_name, int width, int height, CaptureFormat format,
                   int fps);

    /**
     * 获取一帧最新图像数据。
     *
     * @param frame_buf 输出参数。函数返回成功后：
     *                  - frame_buf->start 指向当前帧数据；
     *                  - frame_buf->length 表示当前帧有效字节数。
     *                  注意：这个地址指向 mmap 的驱动缓冲区，只在缓冲区被复用前有效，
     *                  调用方应尽快消费或拷贝。
     * @return          0 表示成功拿到一帧；小于 0 表示 select / DQBUF / QBUF 失败。
     */
    int get_data(FrameBuf *frame_buf);

    /**
     * 释放摄像头相关资源。
     *
     * 具体包括：
     * 1. 解除所有 mmap 映射；
     * 2. 关闭设备文件描述符。
     */
    void release_video();
};


#endif // V4L2_H
