#if(USE_ARM_LIB==1)

#include <opencv2/opencv.hpp>

#include "rga/RgaUtils.h"
#include "rga/im2d.h"
#include "rga/rga.h"

/**
 * 把回传链路里的 RGB 图像转成编码器更适合输入的 YUV420P。
 *
 * @param img_rgb 输入 RGB 图像。
 * @param img_yuv 输出 YUV420P 图像。
 * @return        RGA 状态码。
 */
int rga_cvtcolor(const cv::Mat &img_rgb, cv::Mat &img_yuv)
{
    // init rga context
    rga_buffer_t src;
    rga_buffer_t dst;
    im_rect      src_rect;
    im_rect      dst_rect;
    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));

    src = wrapbuffer_virtualaddr(
        (void*)img_rgb.data, // vir_addr: 输入 RGB 图像首地址
        img_rgb.cols,        // width:    输入宽度
        img_rgb.rows,        // height:   输入高度
        RK_FORMAT_RGB_888);  // format:   输入像素格式
    dst = wrapbuffer_virtualaddr(
        (void*)img_yuv.data,      // vir_addr: 输出 YUV 缓冲区首地址
        img_rgb.cols,             // width:    输出宽度，与输入一致
        img_rgb.rows,             // height:   输出高度，与输入一致
        RK_FORMAT_YCbCr_420_P);   // format:   输出像素格式

    int ret = imcheck(src, dst, src_rect, dst_rect);
    if (IM_STATUS_NOERROR != ret)
    {
        printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
        return -1;
    }

    // src.format / dst.format 共同决定“RGB888 -> YUV420P”这次转换的方向。
    IM_STATUS STATUS = imcvtcolor(src, dst, src.format, dst.format);

    return STATUS;
}

#endif
