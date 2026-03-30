#if(USE_ARM_LIB==1)

#include "mpp_encode.h"

MppEncode::MppEncode()
{

}

MppEncode::~MppEncode()
{
    // 编码器析构时先 reset，再按逆序释放上下文和 buffer。
    MPP_RET ret = MPP_OK;
    if (mpp_enc_data.mpi && mpp_enc_data.ctx)
    {
        ret = mpp_enc_data.mpi->reset(mpp_enc_data.ctx);
        if (ret)
        {
            printf("mpi->reset failed\n");
        }
    }

    if (mpp_enc_data.ctx)
    {
        mpp_destroy(mpp_enc_data.ctx);
        mpp_enc_data.ctx = NULL;
    }

    if (mpp_enc_data.enc_cfg)
    {
        mpp_enc_cfg_deinit(mpp_enc_data.enc_cfg);
        mpp_enc_data.enc_cfg = NULL;
    }

    if (mpp_enc_data.frm_buf)
    {
        mpp_buffer_put(mpp_enc_data.frm_buf);
        mpp_enc_data.frm_buf = NULL;
    }
}

/**
 * 初始化 MPP JPEG 编码器。
 *
 * @param wid          输入图像宽度。
 * @param hei          输入图像高度。
 * @param jpeg_quality JPEG 编码质量。
 */
void MppEncode::init(int wid,int hei,int jpeg_quality)
{
    //清空配置
    memset(&mpp_enc_data, 0, sizeof(MppContext));

    mpp_enc_data.width = wid;
    mpp_enc_data.height = hei;
    int fps = 30;

    // MPP 编码时通常要求 stride 做 16 对齐。
    // 宽度不足 16 的整数倍时，也要按对齐后的 stride 申请输入 buffer。
    mpp_enc_data.hor_stride = MPP_ALIGN(mpp_enc_data.width, 16);
    mpp_enc_data.ver_stride = MPP_ALIGN(mpp_enc_data.height, 8);

    // 这里固定使用 YUV420P，因为上游 RGA 已经把 RGB 转好了。
    mpp_enc_data.fmt = MPP_FMT_YUV420P;
    mpp_enc_data.type = MPP_VIDEO_CodingMJPEG;//MPP_VIDEO_CodingAVC;
    mpp_enc_data.fps = fps;
    mpp_enc_data.gop = fps*2;
    mpp_enc_data.bps = wid*hei/8*mpp_enc_data.fps;//压缩后每秒视频的bit位大小

    switch (mpp_enc_data.fmt & MPP_FRAME_FMT_MASK)
    {
        case MPP_FMT_YUV420SP:
        case MPP_FMT_YUV420P: {
            mpp_enc_data.frame_size = mpp_enc_data.hor_stride * mpp_enc_data.ver_stride * 3 / 2;
        } break;

        case MPP_FMT_YUV422_YUYV :
        case MPP_FMT_YUV422_YVYU :
        case MPP_FMT_YUV422_UYVY :
        case MPP_FMT_YUV422_VYUY :
        case MPP_FMT_YUV422P :
        case MPP_FMT_YUV422SP : {
            mpp_enc_data.frame_size = mpp_enc_data.hor_stride * mpp_enc_data.ver_stride * 2;
        } break;

        case MPP_FMT_RGB444 :
        case MPP_FMT_BGR444 :
        case MPP_FMT_RGB555 :
        case MPP_FMT_BGR555 :
        case MPP_FMT_RGB565 :
        case MPP_FMT_BGR565 :
        case MPP_FMT_RGB888 :
        case MPP_FMT_BGR888 :
        case MPP_FMT_RGB101010 :
        case MPP_FMT_BGR101010 :
        case MPP_FMT_ARGB8888 :
        case MPP_FMT_ABGR8888 :
        case MPP_FMT_BGRA8888 :
        case MPP_FMT_RGBA8888 : {
            mpp_enc_data.frame_size = mpp_enc_data.hor_stride * mpp_enc_data.ver_stride * 3;
        } break;

        default: {
            mpp_enc_data.frame_size = mpp_enc_data.hor_stride * mpp_enc_data.ver_stride * 4;
        } break;
    }

    MPP_RET ret = MPP_OK;
    //开辟编码时需要的内存
    ret = mpp_buffer_get(NULL, &mpp_enc_data.frm_buf, mpp_enc_data.frame_size);
    if (ret)
    {
        printf("failed to get buffer for input frame ret %d\n", ret);
        goto MPP_INIT_OUT;
    }

    //创建 MPP context 和 MPP api 接口
    ret = mpp_create(&mpp_enc_data.ctx, &mpp_enc_data.mpi);
    if (ret)
    {
        printf("mpp_create failed ret %d\n", ret);
        goto MPP_INIT_OUT;
    }

    /*初始化编码还是解码，以及编解码的格式
    MPP_CTX_DEC ： 解码
    MPP_CTX_ENC ： 编码
    MPP_VIDEO_CodingAVC ： H.264
    MPP_VIDEO_CodingHEVC :  H.265
    MPP_VIDEO_CodingMJPEG : MJPEG*/
    ret = mpp_init(mpp_enc_data.ctx, MPP_CTX_ENC, mpp_enc_data.type);
    if (ret)
    {
        printf("mpp_init failed ret %d\n", ret);
        goto MPP_INIT_OUT;
    }

    ret = mpp_enc_cfg_init(&mpp_enc_data.enc_cfg);
    if (ret)
    {
        printf("mpp_enc_cfg_init failed ret %d\n", ret);
        goto MPP_INIT_OUT;
    }

    ret = mpp_enc_data.mpi->control(mpp_enc_data.ctx, MPP_ENC_GET_CFG, mpp_enc_data.enc_cfg);
    if (ret)
    {
        printf("mpi control enc get cfg failed ret %d\n", ret);
        goto MPP_INIT_OUT;
    }

    // 适配新版 RKMPP：统一走 MppEncCfg 的 key-value 配置接口。
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "codec:type", mpp_enc_data.type);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:width", mpp_enc_data.width);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:height", mpp_enc_data.height);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:hor_stride", mpp_enc_data.hor_stride);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:ver_stride", mpp_enc_data.ver_stride);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:format", mpp_enc_data.fmt);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:range", MPP_FRAME_RANGE_JPEG);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "prep:rotation", MPP_ENC_ROT_0);

    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:mode", MPP_ENC_RC_MODE_VBR);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:bps_target", mpp_enc_data.bps);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:bps_max", mpp_enc_data.bps * 17 / 16);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:bps_min", mpp_enc_data.bps * 1 / 16);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_in_num", mpp_enc_data.fps);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_in_denom", 1);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_out_num", mpp_enc_data.fps);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "rc:gop", mpp_enc_data.gop);
    mpp_enc_cfg_set_u32(mpp_enc_data.enc_cfg, "rc:max_reenc_times", 0);
    mpp_enc_cfg_set_u32(mpp_enc_data.enc_cfg, "rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
    mpp_enc_cfg_set_u32(mpp_enc_data.enc_cfg, "rc:drop_thd", 20);
    mpp_enc_cfg_set_u32(mpp_enc_data.enc_cfg, "rc:drop_gap", 1);

    // 当前节点只用于 JPEG 回传，直接按 OpenCV 风格质量范围映射 q_factor。
    if (jpeg_quality < 1)
    {
        jpeg_quality = 1;
    }
    else if (jpeg_quality > 99)
    {
        jpeg_quality = 99;
    }
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "jpeg:q_factor", jpeg_quality);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "jpeg:qf_max", 99);
    mpp_enc_cfg_set_s32(mpp_enc_data.enc_cfg, "jpeg:qf_min", 1);

    ret = mpp_enc_data.mpi->control(mpp_enc_data.ctx, MPP_ENC_SET_CFG, mpp_enc_data.enc_cfg);
    if (ret)
    {
        printf("mpi control enc set cfg failed ret %d\n", ret);
        goto MPP_INIT_OUT;
    }



    //////////////
	buf_ptr = mpp_buffer_get_ptr(mpp_enc_data.frm_buf);

	ret = mpp_frame_init(&frame);
	if (ret)
	{
		printf("mpp_frame_init failed\n");
        return;
	}

	mpp_frame_set_width(frame, mpp_enc_data.width);
	mpp_frame_set_height(frame, mpp_enc_data.height);
	mpp_frame_set_hor_stride(frame, mpp_enc_data.hor_stride);
	mpp_frame_set_ver_stride(frame, mpp_enc_data.ver_stride);
	mpp_frame_set_fmt(frame, mpp_enc_data.fmt);
	mpp_frame_set_buffer(frame, mpp_enc_data.frm_buf);
	mpp_frame_set_eos(frame, mpp_enc_data.frm_eos);


    return;

MPP_INIT_OUT:

    if (mpp_enc_data.enc_cfg)
    {
        mpp_enc_cfg_deinit(mpp_enc_data.enc_cfg);
        mpp_enc_data.enc_cfg = NULL;
    }

    if (mpp_enc_data.ctx)
    {
        mpp_destroy(mpp_enc_data.ctx);
        mpp_enc_data.ctx = NULL;
    }

    if (mpp_enc_data.frm_buf)
    {
        mpp_buffer_put(mpp_enc_data.frm_buf);
        mpp_enc_data.frm_buf = NULL;
    }

    printf("init mpp failed!\n");
}

/**
 * 编码一帧 YUV420P 图像。
 *
 * @param in_data   输入 YUV420P 数据。
 * @param in_size   输入数据总字节数。
 * @param jpeg_data 输出 JPEG 字节流。
 * @return          0 成功；非 0 表示 put_frame / get_packet 失败。
 */
int MppEncode::encode(unsigned char *in_data, int in_size,std::vector<unsigned char> &jpeg_data)
{
	MPP_RET ret = MPP_OK;
	MppPacket packet = NULL;

    // 把当前帧数据拷贝进 MPP 输入 buffer。
    memcpy(buf_ptr, in_data, in_size);

	// 把输入 frame 送进编码器。
  	ret = mpp_enc_data.mpi->encode_put_frame(mpp_enc_data.ctx, frame);
	if (ret)
	{
		printf("mpp encode put frame failed\n");
        return ret;
	}

	// 从编码器拉取输出 packet，即 JPEG 字节流。
	ret = mpp_enc_data.mpi->encode_get_packet(mpp_enc_data.ctx, &packet);
	if (ret)
	{
		printf("mpp encode get packet failed\n");
        return ret;
	}

	if (!packet)
	{
		printf("!packet\n");
        return -1;
    }

    // ptr 指向编码后的 JPEG 数据。
    uint8_t *ptr  = (uint8_t*)mpp_packet_get_pos(packet);
    size_t   len  = mpp_packet_get_length(packet);
    if(len<=0)
    {
        printf("encode len error!!!\n");
        return -1;
    }
    
    jpeg_data.assign(ptr,ptr+len);

    // deinit 会释放 packet，因此必须先把数据拷贝到 jpeg_data 再释放。
    mpp_packet_deinit(&packet);//会释放packet，所以需要在上面将packet数据拷贝出去

	return 0;
}


#endif
