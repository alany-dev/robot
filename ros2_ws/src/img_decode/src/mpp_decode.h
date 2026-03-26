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

//宏定义
#define MPP_ALIGN(x, a)   (((x)+(a)-1)&~((a)-1))

#define ESC_START     "\033["
#define ESC_END       "\033[0m"
#define COLOR_GREEN   "32;40;1m"
#define COLOR_RED     "31;40;1m"
#define MPP_DBG(format, ...) (printf( ESC_START COLOR_GREEN "[MPP DBG]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##__VA_ARGS__))
#define MPP_ERR(format, ...) (printf( ESC_START COLOR_RED   "[MPP ERR]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##__VA_ARGS__))

/**
 * ROS2 版 Rockchip MPP MJPEG 解码封装。
 *
 * 作用和 ROS1 版相同：
 * - 把 JPEG 字节流送入 MPP；
 * - 直接得到 RGB888 输出；
 * - 让上层继续交给 RGA 缩放。
 */
class MppDecode
{
public:
	MppDecode();
	~MppDecode();

	/**
	 * @param width  输入图像宽度。
	 * @param height 输入图像高度。
	 */
	void init(int width,int height);

	/**
	 * @param srcFrm JPEG 数据首地址。
	 * @param srcLen JPEG 数据长度。
	 * @param image  输出 RGB 图像，成功时引用 MPP buffer。
	 * @return       0 成功；非 0 失败。
	 */
	int decode(unsigned char *srcFrm, size_t srcLen, cv::Mat &image);

private:
	MppBufferGroup frmGrp;
	MppBufferGroup pktGrp;
	MppPacket      packet;
	MppFrame       frame;
	size_t         packetSize;

	MppBuffer      frmBuf   = NULL;
	MppBuffer      pktBuf   = NULL;

	char *dataBuf = NULL;

	MppCtx  ctx   = NULL;
    MppApi *mpi   = NULL;

	int init_mpp();
	int init_packet_and_frame(int width, int height);
	int get_image(MppFrame &frame, cv::Mat &image);
};



#endif

#endif

