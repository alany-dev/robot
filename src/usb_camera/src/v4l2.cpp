#include "v4l2.h"

#include <poll.h>
#include <sys/ioctl.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#define LOG ROS_INFO

namespace {

std::string normalize_format_name(const std::string &format_name) {
    std::string format = format_name;
    for (size_t i = 0; i < format.size(); ++i) {
        format[i] = static_cast<char>(tolower(format[i]));
    }

    if (format == "jpeg" || format == "mjpeg" || format == "mjpg") {
        return "mjpeg";
    }
    if (format == "yuyv" || format == "yuv" || format == "yuv422" ||
        format == "yuy2" || format == "yuv422_yuy2") {
        return "yuyv";
    }
    return "";
}

}  // namespace

bool parse_capture_format(const std::string &format_name, CaptureFormat *format) {
    if (format == nullptr) {
        return false;
    }

    const std::string normalized = normalize_format_name(format_name);
    if (normalized == "mjpeg") {
        *format = CaptureFormat::kMjpeg;
        return true;
    }
    if (normalized == "yuyv") {
        *format = CaptureFormat::kYuyv;
        return true;
    }
    return false;
}

bool v4l2_pixfmt_to_capture_format(__u32 pixfmt, CaptureFormat *format) {
    if (format == nullptr) {
        return false;
    }

    switch (pixfmt) {
        case V4L2_PIX_FMT_MJPEG:
            *format = CaptureFormat::kMjpeg;
            return true;
        case V4L2_PIX_FMT_YUYV:
            *format = CaptureFormat::kYuyv;
            return true;
        default:
            return false;
    }
}

const char *capture_format_to_string(CaptureFormat format) {
    switch (format) {
        case CaptureFormat::kMjpeg:
            return "mjpeg";
        case CaptureFormat::kYuyv:
            return "yuyv";
        default:
            return "unknown";
    }
}

const char *capture_format_to_message_format(CaptureFormat format) {
    switch (format) {
        case CaptureFormat::kMjpeg:
            return "jpeg";
        case CaptureFormat::kYuyv:
            return "yuyv";
        default:
            return "unknown";
    }
}

const char *capture_format_to_image_encoding(CaptureFormat format) {
    switch (format) {
        case CaptureFormat::kYuyv:
            return "yuv422_yuy2";
        case CaptureFormat::kMjpeg:
        default:
            return "";
    }
}

__u32 capture_format_to_v4l2_pixfmt(CaptureFormat format) {
    switch (format) {
        case CaptureFormat::kYuyv:
            return V4L2_PIX_FMT_YUYV;
        case CaptureFormat::kMjpeg:
        default:
            return V4L2_PIX_FMT_MJPEG;
    }
}

V4l2::V4l2() : fd(-1), active_format(CaptureFormat::kMjpeg) {
    memset(&buf, 0, sizeof(buf));
    memset(mmap_buffer, 0, sizeof(mmap_buffer));
}

int V4l2::init_video(const char *dev_name, int width, int height, CaptureFormat format,
                     int fps) {
    struct v4l2_fmtdesc fmtdesc;
    int ret;
    const __u32 video_format = capture_format_to_v4l2_pixfmt(format);

    release_video();

    fd = open(dev_name, O_RDWR, 0);
    if (fd < 0) {
        LOG("Open %s failed!!!", dev_name);
        return -1;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret == 0) {
        const __u32 effective_caps =
            (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if ((effective_caps & V4L2_CAP_VIDEO_CAPTURE) == 0) {
            LOG("Error opening device %s : video capture not supported (capabilities=0x%x, device_caps=0x%x)",
                dev_name, cap.capabilities, cap.device_caps);
            close(fd);
            fd = -1;
            return -1;
        }
        if (!(effective_caps & V4L2_CAP_STREAMING)) {
            LOG("%s does not support streaming i/o", dev_name);
            close(fd);
            fd = -1;
            return -1;
        }
    } else {
        LOG("VIDIOC_QUERYCAP failed (%d)", ret);
        close(fd);
        fd = -1;
        return ret;
    }

    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    LOG("Support format for %s:", dev_name);

    struct v4l2_frmsizeenum fsenum;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1) {
        memset(&fsenum, 0, sizeof(fsenum));
        fsenum.pixel_format = fmtdesc.pixelformat;
        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsenum) == 0) {
            LOG("SUPPORT %d.%s, freamsize: %d: %d x %d", fmtdesc.index + 1,
                fmtdesc.description, fsenum.index, fsenum.discrete.width,
                fsenum.discrete.height);
            fsenum.index++;
        }
        fmtdesc.index++;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = video_format;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        LOG("VIDIOC_S_FMT failed (%d), format=%s", ret, capture_format_to_string(format));
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        LOG("VIDIOC_G_FMT failed (%d)", ret);
        return ret;
    }

    CaptureFormat negotiated_format = CaptureFormat::kMjpeg;
    if (!v4l2_pixfmt_to_capture_format(fmt.fmt.pix.pixelformat, &negotiated_format)) {
        char actual_fmt[8];
        memset(actual_fmt, 0, sizeof(actual_fmt));
        memcpy(actual_fmt, &fmt.fmt.pix.pixelformat, 4);
        LOG("Negotiated unsupported pixelformat=%s (0x%x), only mjpeg/yuyv are supported",
            actual_fmt, fmt.fmt.pix.pixelformat);
        return -1;
    }
    if (negotiated_format != format) {
        LOG("Requested format=%s but camera negotiated format=%s, treat as unsupported",
            capture_format_to_string(format), capture_format_to_string(negotiated_format));
        return -1;
    }
    active_format = negotiated_format;

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = fps;
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

    LOG("Stream Format Informations:");
    LOG(" type: %d", fmt.type);
    LOG(" width: %d", fmt.fmt.pix.width);
    LOG(" height: %d", fmt.fmt.pix.height);

    char fmtstr[8];
    memset(fmtstr, 0, sizeof(fmtstr));
    memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
    LOG(" pixelformat: %s", fmtstr);
    LOG(" field: %d", fmt.fmt.pix.field);
    LOG(" bytesperline: %d", fmt.fmt.pix.bytesperline);
    LOG(" sizeimage: %d", fmt.fmt.pix.sizeimage);
    LOG(" colorspace: %d", fmt.fmt.pix.colorspace);
    LOG(" capture_format: %s", capture_format_to_string(active_format));

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = BUFFER_COUNT;
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) {
        LOG("VIDIOC_REQBUFS failed (%d)", ret);
        return ret;
    }

    for (int i = 0; i < reqbuf.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            LOG("VIDIOC_QUERYBUF (%d) failed (%d)", i, ret);
            return ret;
        }

        mmap_buffer[i].length = buf.length;
        mmap_buffer[i].start =
            (unsigned char *)mmap(0, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, buf.m.offset);
        if (mmap_buffer[i].start == MAP_FAILED) {
            LOG("mmap (%d) failed: %s", i, strerror(errno));
            return -1;
        }

        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG("VIDIOC_QBUF (%d) failed (%d)", i, ret);
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOG("VIDIOC_STREAMON failed (%d)", ret);
        return ret;
    }
    return 0;
}

int V4l2::get_data(FrameBuf *frame_buf) {
    int ret = 0;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    int r = poll(fds, 1, 5000);
    if (r == -1) {
        LOG("poll err");
        return -1;
    } else if (r == 0) {
        LOG("poll timeout");
        return -1;
    }

    ret = ioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOG("VIDIOC_DQBUF failed (%d)", ret);
        return ret;
    }

    frame_buf->start = mmap_buffer[buf.index].start;
    frame_buf->length = buf.bytesused;

    ret = ioctl(fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        LOG("VIDIOC_QBUF failed (%d)", ret);
        return ret;
    }

    return 0;
}

void V4l2::release_video() {
    if (fd < 0) return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG("STREAMOFF failed");
    }

    struct v4l2_requestbuffers reqbuf{};
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    ioctl(fd, VIDIOC_REQBUFS, &reqbuf);

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (mmap_buffer[i].start != nullptr && mmap_buffer[i].start != MAP_FAILED) {
            munmap(mmap_buffer[i].start, mmap_buffer[i].length);
            mmap_buffer[i].start = nullptr;
        }
        mmap_buffer[i].length = 0;
    }

    close(fd);
    fd = -1;
    active_format = CaptureFormat::kMjpeg;

    LOG("Camera release done correctly.");
}
