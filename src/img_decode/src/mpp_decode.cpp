#if(USE_ARM_LIB==1)

#include "mpp_decode.h"

#include <cctype>

namespace {

size_t output_buffer_size_for_format(MppFrameFormat format, RK_U32 hor_stride,
                                     RK_U32 ver_stride) {
    switch (format) {
        case MPP_FMT_YUV420SP:
        case MPP_FMT_YUV420P:
            return static_cast<size_t>(hor_stride) * ver_stride * 3 / 2;
        case MPP_FMT_RGB888:
            return static_cast<size_t>(hor_stride) * ver_stride * 3;
        default:
            return static_cast<size_t>(hor_stride) * ver_stride * 4;
    }
}

}  // namespace

std::string normalize_mpp_codec_name(const std::string &codec_name) {
    std::string codec = codec_name;
    for (size_t i = 0; i < codec.size(); ++i) {
        codec[i] = static_cast<char>(tolower(codec[i]));
    }

    if (codec == "jpeg" || codec == "mjpeg" || codec == "mjpg") {
        return "mjpeg";
    }
    if (codec == "h264" || codec == "avc") {
        return "h264";
    }
    if (codec == "h265" || codec == "hevc") {
        return "h265";
    }
    return "";
}

MppCodingType mpp_coding_type_from_name(const std::string &codec_name, bool *known) {
    const std::string normalized = normalize_mpp_codec_name(codec_name);
    if (normalized == "h264") {
        if (known) {
            *known = true;
        }
        return MPP_VIDEO_CodingAVC;
    }
    if (normalized == "h265") {
        if (known) {
            *known = true;
        }
        return MPP_VIDEO_CodingHEVC;
    }
    if (normalized == "mjpeg") {
        if (known) {
            *known = true;
        }
        return MPP_VIDEO_CodingMJPEG;
    }

    if (known) {
        *known = false;
    }
    return MPP_VIDEO_CodingMJPEG;
}

MppFrameFormat mpp_output_format_from_name(const std::string &codec_name) {
    const std::string normalized = normalize_mpp_codec_name(codec_name);
    if (normalized == "h264" || normalized == "h265") {
        return MPP_FMT_YUV420SP;
    }
    return MPP_FMT_RGB888;
}

MppDecode::MppDecode() {
}

void MppDecode::reset() {
    if (packet) {
        mpp_packet_deinit(&packet);
        packet = NULL;
    }

    if (frame) {
        mpp_frame_deinit(&frame);
        frame = NULL;
    }

    if (ctx) {
        mpp_destroy(ctx);
        ctx = NULL;
        mpi = NULL;
    }

    if (pktBuf) {
        mpp_buffer_put(pktBuf);
        pktBuf = NULL;
    }

    if (frmBuf) {
        mpp_buffer_put(frmBuf);
        frmBuf = NULL;
    }

    if (pktGrp) {
        mpp_buffer_group_put(pktGrp);
        pktGrp = NULL;
    }

    if (frmGrp) {
        mpp_buffer_group_put(frmGrp);
        frmGrp = NULL;
    }

    dataBuf = NULL;
    packetSize = 0;
    initialized = false;
}

MppDecode::~MppDecode() {
    reset();
}

void MppDecode::init(int width, int height, const std::string &new_codec_name) {
    if (!ensure_config(width, height, new_codec_name)) {
        printf("mpp_decode init failed codec=%s width=%d height=%d\r\n",
               new_codec_name.c_str(), width, height);
    }
}

bool MppDecode::ensure_config(int width, int height, const std::string &new_codec_name) {
    std::string normalized = normalize_mpp_codec_name(new_codec_name);
    if (normalized.empty()) {
        normalized = "mjpeg";
    }

    if (initialized && config_width == width && config_height == height &&
        codec_name == normalized) {
        return true;
    }

    reset();

    config_width = width;
    config_height = height;
    codec_name = normalized;
    coding_type = mpp_coding_type_from_name(codec_name);
    output_format = mpp_output_format_from_name(codec_name);

    int ret = init_mpp();
    if (ret != MPP_OK) {
        printf("mpp_decode init_mpp failed (%d)\r\n", ret);
        reset();
        return false;
    }

    ret = init_packet_and_frame(width, height);
    if (ret != MPP_OK) {
        printf("mpp_decode init_packet_and_frame failed (%d)\r\n", ret);
        reset();
        return false;
    }

    initialized = true;
    return true;
}

int MppDecode::init_mpp() {
    MPP_RET ret = MPP_OK;
    MpiCmd mpi_cmd = MPP_CMD_BASE;
    MppParam param = NULL;

    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        MPP_ERR("mpp_create erron (%d) \n", ret);
        return ret;
    }

    uint32_t need_split = 1;
    mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
    param = &need_split;
    ret = mpi->control(ctx, mpi_cmd, param);
    if (ret != MPP_OK) {
        MPP_ERR("MPP_DEC_SET_PARSER_SPLIT_MODE set erron (%d) \n", ret);
        return ret;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, coding_type);
    if (MPP_OK != ret) {
        MPP_ERR("mpp_init erron (%d) \n", ret);
        return ret;
    }

    param = &output_format;
    ret = mpi->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, param);
    if (ret != MPP_OK) {
        MPP_ERR("MPP_DEC_SET_OUTPUT_FORMAT erron (%d) \n", ret);
        return ret;
    }

    return MPP_OK;
}

int MppDecode::init_packet_and_frame(int width, int height) {
    RK_U32 hor_stride = MPP_ALIGN(width, 16);
    RK_U32 ver_stride = MPP_ALIGN(height, 16);

    int ret;
    ret = mpp_buffer_group_get_internal(&frmGrp, MPP_BUFFER_TYPE_ION);
    if (ret) {
        MPP_ERR("frmGrp mpp_buffer_group_get_internal erron (%d)\r\n", ret);
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&pktGrp, MPP_BUFFER_TYPE_ION);
    if (ret) {
        MPP_ERR("pktGrp mpp_buffer_group_get_internal erron (%d)\r\n", ret);
        return -1;
    }

    ret = mpp_frame_init(&frame);
    if (MPP_OK != ret) {
        MPP_ERR("mpp_frame_init failed\n");
        return -1;
    }

    const size_t frame_buffer_size =
        output_buffer_size_for_format(output_format, hor_stride, ver_stride);
    ret = mpp_buffer_get(frmGrp, &frmBuf, frame_buffer_size);
    if (ret) {
        MPP_ERR("frmGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }

    packetSize = static_cast<size_t>(hor_stride) * ver_stride * 4;
    ret = mpp_buffer_get(pktGrp, &pktBuf, packetSize);
    if (ret) {
        MPP_ERR("pktGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }

    ret = mpp_packet_init_with_buffer(&packet, pktBuf);
    if (ret != MPP_OK) {
        MPP_ERR("mpp_packet_init_with_buffer failed (%d)\n", ret);
        return -1;
    }
    dataBuf = reinterpret_cast<char *>(mpp_buffer_get_ptr(pktBuf));

    mpp_frame_set_buffer(frame, frmBuf);
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, hor_stride);
    mpp_frame_set_ver_stride(frame, ver_stride);
    mpp_frame_set_fmt(frame, output_format);
    return 0;
}

int MppDecode::decode(unsigned char *srcFrm, size_t srcLen, DecodedFrame &decoded) {
    decoded = DecodedFrame();

    if (!initialized || ctx == NULL || mpi == NULL || packet == NULL || dataBuf == NULL) {
        MPP_ERR("decode called before decoder is initialized\n");
        return -1;
    }

    if (srcLen == 0 || srcLen > packetSize) {
        MPP_ERR("invalid input packet size %zu, limit=%zu\n", srcLen, packetSize);
        return -1;
    }

    MppTask task = NULL;
    int ret;

    memcpy(dataBuf, srcFrm, srcLen);
    mpp_packet_set_pos(packet, dataBuf);
    mpp_packet_set_length(packet, srcLen);

    ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret) {
        MPP_ERR("mpp input poll failed\n");
        return ret;
    }

    ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
    if (ret) {
        MPP_ERR("mpp task input dequeue failed\n");
        return ret;
    }

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
    if (ret) {
        MPP_ERR("mpp task input enqueue failed\n");
        return ret;
    }

    ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret) {
        MPP_ERR("mpp output poll failed\n");
        return ret;
    }

    ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task);
    if (ret) {
        MPP_ERR("mpp task output dequeue failed\n");
        return ret;
    }

    int image_res = 0;
    if (task) {
        MppFrame frame_out = NULL;
        mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frame_out);
        image_res = get_image(frame_out ? frame_out : frame, decoded);

        ret = mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
        if (ret) {
            MPP_ERR("mpp task output enqueue failed\n");
            return ret;
        }
    } else {
        MPP_ERR("!task\n");
        return -1;
    }

    return image_res;
}

int MppDecode::get_image(MppFrame frame_in, DecodedFrame &decoded) {
    if (frame_in == NULL) {
        MPP_ERR("!frame\n");
        return -1;
    }

    MppBuffer buffer = mpp_frame_get_buffer(frame_in);
    if (buffer == NULL) {
        MPP_ERR("!buffer\n");
        return -1;
    }

    RK_U8 *base = reinterpret_cast<RK_U8 *>(mpp_buffer_get_ptr(buffer));
    if (base == NULL) {
        MPP_ERR("base==NULL\n");
        return -1;
    }

    decoded.width = static_cast<int>(mpp_frame_get_width(frame_in));
    decoded.height = static_cast<int>(mpp_frame_get_height(frame_in));
    decoded.hor_stride = static_cast<int>(mpp_frame_get_hor_stride(frame_in));
    decoded.ver_stride = static_cast<int>(mpp_frame_get_ver_stride(frame_in));
    decoded.format = mpp_frame_get_fmt(frame_in);
    decoded.data = base;

    if (decoded.height <= 0 || decoded.width <= 0) {
        MPP_ERR("height<=0 || width<=0\n");
        return -1;
    }

    if (decoded.format == MPP_FMT_RGB888) {
        decoded.image = cv::Mat(decoded.height, decoded.width, CV_8UC3, base,
                                decoded.hor_stride * 3);
    } else {
        decoded.image.release();
    }

    return 0;
}

#endif
