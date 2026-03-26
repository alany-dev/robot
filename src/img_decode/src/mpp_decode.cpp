#if(USE_ARM_LIB==1)

#include "mpp_decode.h"

MppDecode::MppDecode()
{
}

/**
 * 初始化整个 MPP 解码流水线。
 *
 * @param width  输入 JPEG 对应的目标宽度。
 * @param height 输入 JPEG 对应的目标高度。
 */
void MppDecode::init(int width,int height)
{
	int ret = init_mpp();
	if (ret != MPP_OK)
	{
		printf("mpp_decode init erron (%d) \r\n", ret);
		return;
	}
	
    ret = init_packet_and_frame(width, height);
	if (ret != MPP_OK)
	{
		printf("mpp_decode init_packet_and_frame (%d) \r\n", ret);
		return;
	}
}

MppDecode::~MppDecode()
{
	// 析构顺序按“对象 -> buffer -> group -> ctx”逆序释放，
	// 避免残留句柄占用 MPP / ION 资源。
	if (packet)
	{
        mpp_packet_deinit(&packet);
        packet = NULL;
    }

	if (frame) 
	{
        mpp_frame_deinit(&frame);
        frame = NULL;
    }

	if (ctx) 
	{
        mpp_destroy(ctx);
        ctx = NULL;
    }

	if (pktBuf) 
	{
        mpp_buffer_put(pktBuf);
        pktBuf = NULL;
    }

    if (frmBuf) 
	{
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

}


/**
 * 创建 MPP context，并把它配置成 MJPEG -> RGB888 解码器。
 *
 * @return MPP_OK 表示成功，其它值表示创建或配置失败。
 */
int MppDecode::init_mpp()
{
	MPP_RET ret = MPP_OK;
	MpiCmd mpi_cmd = MPP_CMD_BASE;
    MppParam param = NULL;
	
	//创建 MPP context 和 MPP api 接口
	ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) 
	{
		MPP_ERR("mpp_create erron (%d) \n", ret);
        return ret;
    }

	uint32_t need_split = 1;
	//MPP_DEC_SET_PARSER_SPLIT_MODE ：  （仅限解码）
	//自动拼包（建议开启），硬编解码器每次解码就是一个Frame，
	//所以如果输入的数据不确定是不是一个Frame
	//（例如可能是一个Slice、一个Nalu或者一个FU-A分包，甚至可能随意读的任意长度数据），
	//那就必须把该模式打开，MPP会自动分包拼包成一个完整Frame送给硬解码器
	mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
	param = &need_split;
	ret = mpi->control(ctx, mpi_cmd, param);
	if (ret != MPP_OK)
	{
        MPP_ERR("MPP_DEC_SET_PARSER_SPLIT_MODE set erron (%d) \n", ret);
        return ret;
    }

	// 设置 MPP 为 MJPEG 解码模式。
	//MPP_CTX_DEC ： 解码
	//MPP_VIDEO_CodingAVC ： H.264
	//MPP_VIDEO_CodingHEVC :  H.265
	//MPP_VIDEO_CodingMJPEG : MJPEG
	ret = mpp_init(ctx, MPP_CTX_DEC, MppCodingType::MPP_VIDEO_CodingMJPEG);//这里填解码MJPEG
	if (MPP_OK != ret) 
	{
		MPP_ERR("mpp_init erron (%d) \n", ret);
        return ret;
	}

	// 输出格式固定成 RGB888。
	// 这样上层 img_decode 可以直接继续走 RGA 缩放，避免再做一次颜色空间转换。
	MppFrameFormat frmType = MPP_FMT_RGB888; //MPP_FMT_RGB888; //MPP_FMT_YUV420P;
	param = &frmType;
	mpi->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, param);

	return MPP_OK;
}


/**
 * 为解码器分配输入和输出缓冲区。
 *
 * @param width  输入图像宽度。
 * @param height 输入图像高度。
 * @return       0 表示成功；-1 表示某一步缓冲区初始化失败。
 */
int MppDecode::init_packet_and_frame(int width, int height)
{
	// Rockchip MPP/RGA 常要求 stride 做 16 对齐，这里按对齐尺寸申请 buffer。
	RK_U32 hor_stride = MPP_ALIGN(width, 16);
    RK_U32 ver_stride = MPP_ALIGN(height, 16);
    
	int ret;
	ret = mpp_buffer_group_get_internal(&frmGrp, MPP_BUFFER_TYPE_ION);
	if(ret)
	{
		MPP_ERR("frmGrp mpp_buffer_group_get_internal erron (%d)\r\n",ret);
		return -1;
	}
    

	ret = mpp_buffer_group_get_internal(&pktGrp, MPP_BUFFER_TYPE_ION);
	if(ret)
	{
		MPP_ERR("frmGrp mpp_buffer_group_get_internal erron (%d)\r\n",ret);
		return -1;
	}
	ret = mpp_frame_init(&frame); /* output frame */
    if (MPP_OK != ret)
	{
        MPP_ERR("mpp_frame_init failed\n");
        return -1;
    }

	// frmBuf:
	// 承接 RGB 输出图像的缓冲区。
	ret = mpp_buffer_get(frmGrp, &frmBuf, hor_stride * ver_stride * 4);
    if (ret) 
	{
        MPP_ERR("frmGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }

	// pktBuf:
	// 承接输入 JPEG 字节流的缓冲区。大小按对齐后的图像尺寸粗略估算。
	ret = mpp_buffer_get(pktGrp, &pktBuf, hor_stride * ver_stride * 4); //2);
    if (ret) 
	{
        MPP_ERR("pktGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }
	mpp_packet_init_with_buffer(&packet, pktBuf);
	dataBuf = (char *)mpp_buffer_get_ptr(pktBuf);

	// frame 绑定输出 buffer，之后每次解码都复用这块内存承接结果。
	mpp_frame_set_buffer(frame, frmBuf);
    return 0;
}


/**
 * 把一帧 JPEG 字节流送进 MPP，并取回对应的 RGB 输出。
 *
 * @param srcFrm JPEG 数据首地址。
 * @param srcLen JPEG 数据长度。
 * @param image  输出参数，成功时引用解码后的 RGB 图像 buffer。
 * @return       0 成功，其它值表示 MPP 处理某一步失败。
 */
int MppDecode::decode(unsigned char *srcFrm, size_t srcLen, cv::Mat &image)
{
	MppTask task = NULL;
	int ret;

	//int pktEos = 0;
	// 先把 JPEG 数据拷贝到 MPP 管理的 packet buffer。
	memcpy(dataBuf, srcFrm, srcLen);//拷贝到mpp_buffer

	mpp_packet_set_pos(packet, dataBuf);
    mpp_packet_set_length(packet, srcLen);

	// if(pktEos)
	// {
	// 	mpp_packet_set_eos(packet);
	// }

	// 输入端 poll / dequeue / enqueue：
	// 相当于告诉 MPP “这里有一帧新的 JPEG，请开始解码”。
	ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret) 
	{
        MPP_ERR("mpp input poll failed\n");
        return ret;
    }

	ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);  /* input queue */
    if (ret) 
	{
        MPP_ERR("mpp task input dequeue failed\n");
        return ret;
    }

	mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame (task, KEY_OUTPUT_FRAME,  frame);

	ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);  /* input queue */
    if (ret) 
	{
        MPP_ERR("mpp task input enqueue failed\n");
        return ret;
    }

	// 输出端 poll / dequeue：
	// 等待解码器把这一帧的 RGB 结果准备好。
    ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret) 
	{
        MPP_ERR("mpp output poll failed\n");
        return ret;
    }

	ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task); /* output queue */
    if (ret) 
	{
        MPP_ERR("mpp task output dequeue failed\n");
        return ret;
    }

	int image_res = 0;

	if (task)
	{
		MppFrame frameOut = NULL;
		mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frameOut);

		// frameOut 是本次任务回传的输出 frame；
		// 但当前实现里 frame 对象本身已经绑定了输出 buffer，直接继续取图即可。
		if (frame)
		{
			image_res = get_image(frame,image);

            // if (mpp_frame_get_eos(frameOut))
            // {
			// 	MPP_DBG("found eos frame\n");
			// }
        }
		else
		{
			image_res = -1;
		}

		// 把 task 放回输出队列，完成一次完整的 MPP 任务生命周期。
		ret = mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
        if (ret)
        {
			MPP_ERR("mpp task output enqueue failed\n");
			return ret;
		}
	}
	else
	{
		MPP_ERR("!tast\n");
		return -1;
	}

	return image_res;//如果获取图像错误，从最后返回错误值，而不提前return
}


/**
 * 将 MPP 输出 frame 包装成 OpenCV Mat。
 *
 * @param frame MPP 输出 frame，对应本次解码结果。
 * @param image 输出参数，返回引用同一块 RGB buffer 的 cv::Mat。
 * @return      0 成功；-1 表示 frame / buffer 无效。
 */
int MppDecode::get_image(MppFrame &frame,cv::Mat &image)
{
    RK_U32 width    = 0;
    RK_U32 height   = 0;
    RK_U32 h_stride = 0;
    RK_U32 v_stride = 0;
    MppFrameFormat fmt;
    MppBuffer buffer    = NULL;
    RK_U8 *base = NULL;

    if (NULL == frame)
	{
		MPP_ERR("!frame\n");
        return -1;
	}

    width    = mpp_frame_get_width(frame);
    height   = mpp_frame_get_height(frame);
    h_stride = mpp_frame_get_hor_stride(frame);
    v_stride = mpp_frame_get_ver_stride(frame);
    fmt      = mpp_frame_get_fmt(frame);
    buffer   = mpp_frame_get_buffer(frame);
    if (NULL == buffer)
	{
		MPP_ERR("!buffer\n");
        return -1;
	}

    // 这里拿到的是 MPP 输出 buffer 的首地址。
    // 这块内存往往不是 cache friendly 的普通用户内存，因此不建议马上 memcpy 到 CPU buffer，
    // 更合适的方式是让 RGA 继续直接对这块内存做缩放/拷贝。
    base = (RK_U8 *)mpp_buffer_get_ptr(buffer);//这里的base是不带cache的内存，如果用mmcpy到用户内存会很慢，后续必须通过rga拷贝或者缩放
    
	if(height<=0 || width<=0 || base==NULL)
	{
		MPP_ERR("height<=0 || width<=0 || base==NULL\n");
		return -1;
	}

    // 这里构造的 Mat 只是“包裹”已有 buffer，不会额外复制像素数据。
    image = cv::Mat(height, width, CV_8UC3, base);

	//printf("%dx%d %dx%d %d %d\n",width,height,h_stride,v_stride,fmt,base);

	return 0;
}

#endif

