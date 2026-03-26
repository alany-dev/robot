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

/**
 * Rockchip MPP MJPEG 解码器封装。
 *
 * 在低时延链路里，这个类专门负责：
 * 1. 把 usb_camera 送来的 JPEG 字节流喂给 MPP；
 * 2. 让 MPP 直接产出 RGB888；
 * 3. 把输出 buffer 以 cv::Mat 的形式交给上层，供 RGA 继续缩放。
 *
 * 设计重点：
 * - 不在这里做多余的颜色转换；
 * - 让后续 RGA 直接处理 MPP 输出，尽量减少 CPU 内存拷贝。
 */
class MppDecode
{
public:
	MppDecode();
	~MppDecode();

	/**
	 * 初始化解码器上下文和缓冲区。
	 *
	 * @param width  输入 JPEG 预计对应的图像宽度，用于按对齐尺寸准备缓冲区。
	 * @param height 输入 JPEG 预计对应的图像高度，用于按对齐尺寸准备缓冲区。
	 */
	void init(int width,int height);

	/**
	 * 解码一帧 JPEG 数据。
	 *
	 * @param srcFrm JPEG 字节流首地址。
	 * @param srcLen JPEG 字节流长度（单位：字节）。
	 * @param image  输出参数。成功时会被设置为引用 MPP 输出 buffer 的 cv::Mat。
	 * @return       0 表示成功；小于 0 表示 MPP poll / enqueue / dequeue / 取图失败。
	 */
	int decode(unsigned char *srcFrm, size_t srcLen, cv::Mat &image);

private:
	// frmGrp / pktGrp:
	// 分别管理解码输出帧缓冲区和输入 packet 缓冲区。
	MppBufferGroup frmGrp;
	MppBufferGroup pktGrp;

	// packet / frame:
	// MPP 每次送入的输入包对象，以及承接输出图像的帧对象。
	MppPacket      packet;
	MppFrame       frame;
	size_t         packetSize;

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

	// 初始化 MPP 解码上下文本身。
	int init_mpp();

	// 根据输入分辨率准备 packet / frame 相关内存。
	int init_packet_and_frame(int width, int height);

	// 把 MPP 输出 frame 包装为 OpenCV Mat。
	int get_image(MppFrame &frame, cv::Mat &image);
};



#endif

#endif

