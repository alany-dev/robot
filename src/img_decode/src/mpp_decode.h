#ifndef MPP_DECODE_H
#define MPP_DECODE_H

#if(USE_ARM_LIB==1)

//C 标准函数库
#include <stdio.h>
#include <stdint.h>
#include <string.h>

//Linux 函数库
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

//C++ 标准函数库
#include <iostream>
#include <string>
#include <vector>


//MPP函数库
#include <rockchip/vpu.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_type.h>
#include <rockchip/vpu_api.h>
#include <rockchip/mpp_err.h>
#include <rockchip/mpp_task.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi_cmd.h>

#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

// 宏定义
#define MPP_ALIGN(x, a)   (((x)+(a)-1)&~((a)-1))

#define ESC_START     "\033["
#define ESC_END       "\033[0m"
#define COLOR_GREEN   "32;40;1m"
#define COLOR_RED     "31;40;1m"
#define MPP_DBG(format, args...) (printf( ESC_START COLOR_GREEN "[MPP DBG]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##args))
#define MPP_ERR(format, args...) (printf( ESC_START COLOR_RED   "[MPP ERR]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##args))

struct DecodedFrame
{
	// data 指向 MPP 输出帧的首地址。生命周期受当前 MPP 输出 buffer 管理。
	unsigned char *data = NULL;

	// width / height 是实际图像尺寸；stride 反映底层 buffer 对齐后的行高。
	int width = 0;
	int height = 0;
	int hor_stride = 0;
	int ver_stride = 0;

	// format 用于告诉上层当前拿到的是 RGB888 还是 YUV420SP 等格式。
	MppFrameFormat format = MPP_FMT_BUTT;

	// 当输出本身就是 RGB888 时，image 会直接包裹同一块 buffer；
	// 否则保持 empty，由上层根据 data + format 继续走 RGA 颜色转换。
	cv::Mat image;
};

std::string normalize_mpp_codec_name(const std::string &codec_name);
MppCodingType mpp_coding_type_from_name(const std::string &codec_name, bool *known = NULL);
MppFrameFormat mpp_output_format_from_name(const std::string &codec_name);

/**
 * Rockchip MPP 压缩视频解码器封装。
 *
 * 支持 MJPEG / H.264 / H.265 三种输入：
 * 1. MJPEG 继续走 RGB888 快路径；
 * 2. H.264 / H.265 输出 YUV420SP，交给上层用 RGA 做缩放和转 RGB；
 * 3. 保留 MPP 原始 buffer 信息，避免上层盲目 memcpy。
 */
class MppDecode
{
public:
	MppDecode();
	~MppDecode();

	/**
	 * 初始化解码器上下文和缓冲区。
	 *
	 * @param width      输入码流对应的图像宽度，用于按对齐尺寸准备缓冲区。
	 * @param height     输入码流对应的图像高度，用于按对齐尺寸准备缓冲区。
	 * @param codec_name 输入编码类型，支持 mjpeg / h264 / h265。
	 */
	void init(int width, int height, const std::string &codec_name);
	bool ensure_config(int width, int height, const std::string &codec_name);

	/**
	 * 解码一帧压缩数据。
	 *
	 * @param srcFrm   压缩字节流首地址。
	 * @param srcLen   压缩字节流长度（单位：字节）。
	 * @param decoded  输出参数。成功时包含 MPP 输出 buffer 的地址、尺寸和格式信息。
	 * @return       0 表示成功；小于 0 表示 MPP poll / enqueue / dequeue / 取图失败。
	 */
	int decode(unsigned char *srcFrm, size_t srcLen, DecodedFrame &decoded);

private:
	// frmGrp / pktGrp:
	// 分别管理解码输出帧缓冲区和输入 packet 缓冲区。
	MppBufferGroup frmGrp = NULL;
	MppBufferGroup pktGrp = NULL;

	// packet / frame:
	// MPP 每次送入的输入包对象，以及承接输出图像的帧对象。
	MppPacket      packet = NULL;
	MppFrame       frame = NULL;
	size_t         packetSize = 0;

	// frmBuf / pktBuf:
	// 实际分配出来的输出图像 buffer 与输入压缩包 buffer。
	MppBuffer      frmBuf   = NULL;
	MppBuffer      pktBuf   = NULL;

	// dataBuf:
	// 指向 pktBuf 的用户态可写地址，上层 JPEG 数据会先 memcpy 到这里。
	char *dataBuf = NULL;

	// ctx / mpi:
	// MPP 上下文和操作接口。
	MppCtx  ctx   = NULL;
    MppApi *mpi   = NULL;

	// 当前解码器配置，用于在 codec / 分辨率变化时重建 MPP context。
	bool initialized = false;
	int config_width = 0;
	int config_height = 0;
	std::string codec_name = "mjpeg";
	MppCodingType coding_type = MPP_VIDEO_CodingMJPEG;
	MppFrameFormat output_format = MPP_FMT_RGB888;

	void reset();

	// 初始化 MPP 解码上下文本身。
	int init_mpp();

	// 根据输入分辨率准备 packet / frame 相关内存。
	int init_packet_and_frame(int width, int height);

	// 从 MPP 输出 frame 里提取图像元信息。
	int get_image(MppFrame frame, DecodedFrame &decoded);
};



#endif

#endif

