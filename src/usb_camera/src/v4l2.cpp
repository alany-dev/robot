#include "v4l2.h"

#include <poll.h>
#include <sys/ioctl.h>

#include <cinttypes>
#include <cstdio>

#include <linux/videodev2.h>

#define LOG ROS_INFO

/**
 * 按低时延链路需要初始化 V4L2 采集。
 *
 * 这里的核心目标不是“支持尽可能多的格式”，而是把摄像头固定到
 * MJPEG + 目标分辨率 + 30fps 上，为后面“压缩流传输 -> 硬解码 -> 推理”
 * 这条路径提供稳定输入。
 *
 * @param dev_name 摄像头设备路径，例如 "/dev/video0"。
 * @param width    希望设置的采集宽度。
 * @param height   希望设置的采集高度。
 * @return         0 表示成功；非 0 表示某一步 ioctl 失败。
 */
int V4l2::init_video(const char *dev_name, int width, int height) {
    struct v4l2_fmtdesc fmtdesc;
    int i, ret;

    // Open Device 打开设备
    fd = open(dev_name, O_RDWR, 0);
    if (fd < 0) {
        LOG("Open %s failed!!!", dev_name);
        return -1;
    }

    // Query Capability  查询能力
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(struct v4l2_capability));
    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret == 0) {
        if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
            LOG("Error opening device %s : video capture not supported");
            return -1;
        }
        if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
            LOG("%s does not support streaming i/o", dev_name);
            return -1;
        }
    } else {
        LOG("VIDIOC_QUERYCAP failed (%d)", ret);
        return ret;
    }

    // 枚举摄像头支持的格式，日志里可以直观看到是否真的支持 MJPEG。
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG("Support format:");

    struct v4l2_frmsizeenum fsenum;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        /* 枚举这种格式所支持的帧大小 */
        memset(&fsenum, 0, sizeof(struct v4l2_frmsizeenum));
        fsenum.pixel_format = fmtdesc.pixelformat;
        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsenum) == 0) {
            LOG("SUPPORT %d.%s, freamsize: %d: %d x %d", fmtdesc.index + 1,
                fmtdesc.description, fsenum.index, fsenum.discrete.width,
                fsenum.discrete.height);
            fsenum.index++;
        }
        fmtdesc.index++;
    }

    // 设置采集输出格式：
    // - width / height: 希望摄像头输出的分辨率；
    // - pixelformat:    这里固定选 MJPEG，直接拿压缩帧；
    // - field:          场类型，一般保持默认的隔行字段配置即可。
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = VIDEO_FORMAT;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);  // 计算缓存区大小
    if (ret < 0) {
        LOG("VIDIOC_S_FMT failed (%d)", ret);
        return ret;
    }

    // 再读回一次驱动最终生效的格式，避免用户以为请求值一定被完全接受。
    ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        LOG("VIDIOC_G_FMT failed (%d)", ret);
        return ret;
    }

    // 设置采样帧率：
    // numerator / denominator = 1 / 30，表示目标采样周期为 1/30 秒。
    // capturemode 保持为 1，沿用当前代码里对驱动的兼容配置。
    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = 30;
    param.parm.capture.capturemode = 1;
    ret = ioctl(fd, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        LOG("VIDIOC_S_PARAM failed (%d)", ret);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_PARM, &param);
    if (ret < 0) {
        LOG("VIDIOC_G_PARAM failed (%d)", ret);
        return ret;
    }

    // 打印驱动最终接受的流配置，方便定位“请求了
    // 1280x720，驱动却给了别的值”的问题。
    LOG("Stream Format Informations:");
    LOG(" type: %d", fmt.type);
    LOG(" width: %d", fmt.fmt.pix.width);
    LOG(" height: %d", fmt.fmt.pix.height);

    char fmtstr[8];
    memset(fmtstr, 0, 8);
    memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
    LOG(" pixelformat: %s", fmtstr);
    LOG(" field: %d", fmt.fmt.pix.field);
    LOG(" bytesperline: %d", fmt.fmt.pix.bytesperline);
    LOG(" sizeimage: %d", fmt.fmt.pix.sizeimage);
    LOG(" colorspace: %d", fmt.fmt.pix.colorspace);
    // LOG(" priv: %d", fmt.fmt.pix.priv);
    // LOG(" raw_date: %s", fmt.fmt.raw_data);

    // 向驱动申请 mmap 缓冲区：
    // - type:   视频采集缓冲区；
    // - memory: V4L2_MEMORY_MMAP，表示用户态通过 mmap 直接访问驱动缓冲；
    // - count:  申请 BUFFER_COUNT 个环形缓冲区。
    struct v4l2_requestbuffers reqbuf;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = BUFFER_COUNT;
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) {
        LOG("VIDIOC_REQBUFS failed (%d)", ret);
        return ret;
    }

    // 查询并映射每个驱动缓冲区，然后预先全部 QBUF 回队列。
    // 这样 STREAMON 之后驱动可以立刻开始写入采集数据。
    for (i = 0; i < reqbuf.count; i++) {
        // Query buffer:
        // 根据 index 取回该缓冲区的长度和偏移量。
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            LOG("VIDIOC_QUERYBUF (%d) failed (%d)", i, ret);
            return ret;
        }
        // mmap buffer:
        // 把驱动缓冲区映射进当前进程地址空间，后续读取帧数据时就不需要 read()
        // 拷贝。
        mmap_buffer[i].length = buf.length;
        mmap_buffer[i].start =
            (unsigned char *)mmap(0, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, buf.m.offset);
        if (mmap_buffer[i].start == MAP_FAILED) {
            LOG("mmap (%d) failed: %s", i, strerror(errno));
            return -1;
        }
        // QBUF:
        // 把空缓冲区交还给驱动，让驱动采到下一帧时可以往这里填数据。
        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG("VIDIOC_QBUF (%d) failed (%d)", i, ret);
            return -1;
        }
    }

    // STREAMON:
    // 通知驱动开始正式采集。只有执行到这里，摄像头才会持续把帧写入上面的环形缓冲区。
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOG("VIDIOC_STREAMON failed (%d)", ret);
        return ret;
    }
    return 0;
}

/**
 * 从驱动队列里取出一帧最新数据。
 *
 * 处理顺序是：
 * 1. select 等待设备可读，避免忙轮询；
 * 2. DQBUF 取出一帧已经采好的驱动缓冲区；
 * 3. 把数据地址和长度交给上层；
 * 4. 立刻 QBUF 回去，让缓冲区重新进入驱动可写队列。
 *
 * @param frame_buf 输出参数，承接当前帧地址和字节数。
 * @return          0 成功；非 0 表示等待或缓冲区出队/入队失败。
 */
int V4l2::get_data(FrameBuf *frame_buf)  // 读取数据到buf
{
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;  // 等待可读事件

    // 超时 5 秒（单位：毫秒）
    int r = poll(fds, 1, 5000);
    if (r == -1) {
        LOG("poll err");
        return -1;
    } else if (r == 0) {
        LOG("poll timeout");
        return -1;
    }

    // DQBUF:
    // 从驱动“已采集完成”的队列里取出一个缓冲区。
    if (0 > ioctl(fd, VIDIOC_DQBUF, &buf)) {
        LOG("VIDIOC_DQBUF failed (%d)", ret);
        return ret;
    }

    // buf.index 对应的是本次被取出的那个 mmap 缓冲区。
    // bytesused 是这帧真正写入的字节数，对于 MJPEG 会小于映射总长度。
    frame_buf->start = mmap_buffer[buf.index].start;
    frame_buf->length = buf.bytesused;

    // QBUF:
    // 上层发布 ROS 消息时会自己做一次拷贝，所以这里只把驱动缓冲区尽快归还。
    // 这样下一帧就不会因为“用户态迟迟不归还缓冲区”而卡住。
    if (0 > ioctl(fd, VIDIOC_QBUF, &buf)) {
        LOG("VIDIOC_QBUF failed (%d)", ret);
        return ret;
    }

    return 0;
}

/**
 * 释放 V4L2 采集阶段占用的资源。
 */
 void V4l2::release_video() {
    if (fd < 0) return;  // 防止重复释放

    // ========================
    // ✅ 1. 先停止视频流
    // ========================
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG("STREAMOFF failed");
    }

    // ========================
    // ✅ 2. 释放内核缓冲区
    // ========================
    struct v4l2_requestbuffers reqbuf{};
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;  // 传0表示释放所有
    ioctl(fd, VIDIOC_REQBUFS, &reqbuf);

    // ========================
    // 3. 取消映射
    // ========================
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (mmap_buffer[i].start != nullptr && mmap_buffer[i].start != MAP_FAILED) {
            munmap(mmap_buffer[i].start, mmap_buffer[i].length);
            mmap_buffer[i].start = nullptr; // 标记为空
        }
    }

    // ========================
    // 4. 关闭文件描述符
    // ========================
    close(fd);
    fd = -1;  // ✅ 标记已关闭

    LOG("Camera release done correctly.");
}
