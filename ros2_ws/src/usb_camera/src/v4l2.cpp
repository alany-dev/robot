#include "v4l2.h"

// 使用标准输出替代 ROS 日志
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)


/**
 * 初始化 ROS2 采集实验链路的 V4L2 输入。
 *
 * @param dev_name 摄像头设备路径。
 * @param width    目标采集宽度。
 * @param height   目标采集高度。
 * @return         0 成功；非 0 失败。
 */
int V4l2::init_video(const char *dev_name,int width,int height)
{
    struct v4l2_fmtdesc fmtdesc;
    int i, ret;

    // 打开设备节点。
    fd = open(dev_name, O_RDWR, 0);
    if (fd < 0) {
        LOG("Open %s failed!!!", dev_name);
        return -1;
    }

    // 查询设备能力。
    struct v4l2_capability cap;
    ret = ioctl(fd,VIDIOC_QUERYCAP,&cap);
    if (ret < 0) {
        LOG("VIDIOC_QUERYCAP failed (%d)", ret);
        return ret;
    }

    // 枚举支持的像素格式，方便确认设备是否真的支持 MJPEG。
    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG("Support format:");
    while(ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1)
    {
        LOG("SUPPORT %d.%s",fmtdesc.index+1,fmtdesc.description);
        fmtdesc.index++;
    }

    // 设置采集参数：
    // - width / height: 目标分辨率；
    // - pixelformat:    MJPEG；
    // - field:          场类型配置。
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = VIDEO_FORMAT;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0)
    {
        LOG("VIDIOC_S_FMT failed (%d)", ret);
        return ret;
    }

    // 读回最终生效的格式，避免驱动协商后实际值和请求值不一致却不自知。
    ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        LOG("VIDIOC_G_FMT failed (%d)", ret);
        return ret;
    }

    // 设置目标帧率为 30fps。
    struct v4l2_streamparm param;
    memset(&param,0,sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator=1;
    param.parm.capture.timeperframe.denominator=30;
    param.parm.capture.capturemode = 1;
    ret = ioctl(fd, VIDIOC_S_PARM, &param) ;
    if(ret < 0)
    {
        LOG("VIDIOC_S_PARAM failed (%d)", ret);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_PARM, &param) ;
    if(ret < 0)  {
        LOG("VIDIOC_G_PARAM failed (%d)", ret);
        return ret;
    }

    // 打印最终流配置，便于排查设备协商问题。
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
    //LOG(" priv: %d", fmt.fmt.pix.priv);
    //LOG(" raw_date: %s", fmt.fmt.raw_data);

    // 申请 mmap 环形缓冲区。
    struct v4l2_requestbuffers reqbuf;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = BUFFER_COUNT;
    ret = ioctl(fd , VIDIOC_REQBUFS, &reqbuf);
    if(ret < 0) {
        LOG("VIDIOC_REQBUFS failed (%d)", ret);
        return ret;
    }

    // 查询每个缓冲区并映射到用户态，然后先全部 QBUF 回去供驱动写入。
    for(i=0; i<BUFFER_COUNT; i++)
    {
        // 查询第 i 个缓冲区的长度和偏移。
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd , VIDIOC_QUERYBUF, &buf);
        if(ret < 0) {
            LOG("VIDIOC_QUERYBUF (%d) failed (%d)", i, ret);
            return ret;
        }
        // mmap 到当前进程地址空间，后续无需 read() 拷贝。
        mmap_buffer[i].length= buf.length;
        mmap_buffer[i].start = (unsigned char *)mmap(0, buf.length, 
            PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (mmap_buffer[i].start == MAP_FAILED) {
            LOG("mmap (%d) failed: %s", i, strerror(errno));
            return -1;
        }
        // 把空缓冲区交还给驱动。
        ret = ioctl(fd , VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG("VIDIOC_QBUF (%d) failed (%d)", i, ret);
            return -1;
        }
    }

    // 通知驱动开始采集。
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0)
    {
        LOG("VIDIOC_STREAMON failed (%d)", ret);
        return ret;
    }
    return 0;
}

/**
 * 读取一帧当前可用图像。
 *
 * @param frame_buf 输出当前帧地址与长度。
 * @return          0 成功；非 0 失败。
 */
int V4l2::get_data(FrameBuf *frame_buf)//读取数据到buf
{
    int ret;

    fd_set fds;
    struct timeval tv;
    int r;

    //将fd加入fds集合
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    // 最长等待 5 秒，如果设备完全无响应则让上层执行重连逻辑。
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    //监测是否有数据，最多等待5s
    r = select(fd + 1, &fds, NULL, NULL, &tv);
    if(r==-1)
    {
        LOG("select err");
        return -1;
    }
    else if(r==0)
    {
        LOG("select timeout");
        return -1;
    }
    // 从驱动完成队列中取出一帧。
    ret = ioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOG("VIDIOC_DQBUF failed (%d)", ret);
        return ret;
    }

    // buf.index 指向本次取出的缓冲区，bytesused 是实际 JPEG 大小。
    frame_buf->start = mmap_buffer[buf.index].start;
    frame_buf->length = buf.bytesused;

    // 立刻把缓冲区归还给驱动，为下一帧采集腾位置。
    ret = ioctl(fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        LOG("VIDIOC_QBUF failed (%d)", ret);
        return ret;
    }

    return 0;
}

/**
 * 释放采集资源。
 */
void V4l2::release_video()
{
    // 解除所有 mmap 映射并关闭设备。
    for (int i=0; i<BUFFER_COUNT; i++) {
        munmap(mmap_buffer[i].start, mmap_buffer[i].length);
    }
    close(fd);
    LOG("Camera release done.");
}
