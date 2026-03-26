#if(USE_ARM_LIB==1)

#include <opencv2/opencv.hpp>

#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"

/**
 * @param img_in       输入 RGB 图像。
 * @param img_out_data 输出缓冲区首地址。
 * @param scale        输出相对输入的缩放比例。
 * @return             RGA 状态码。
 */
int rga_resize(cv::Mat &img_in,unsigned char *img_out_data,float scale)
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
        (void*)img_in.data,  // vir_addr: 输入图像首地址
        img_in.cols,         // width:    输入宽度
        img_in.rows,         // height:   输入高度
        RK_FORMAT_RGB_888);  // format:   输入像素格式
    dst = wrapbuffer_virtualaddr(
        (void*)img_out_data, // vir_addr: 输出缓冲区首地址
        img_in.cols * scale, // width:    输出宽度
        img_in.rows * scale, // height:   输出高度
        RK_FORMAT_RGB_888);  // format:   输出像素格式

    int ret = imcheck(src, dst, src_rect, dst_rect);
    if (IM_STATUS_NOERROR != ret)
    {
        printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
        return -1;
    }

    IM_STATUS STATUS;
    if(scale != 1.0)
        // 尺寸变化时执行 resize。
        STATUS = imresize(src, dst);
    else
        // 尺寸不变时走 copy，更省开销。
        STATUS = imcopy(src, dst);//如果尺寸不变，则拷贝到用户空间，用这个函数可以节约拷贝时间

    return STATUS;
}

#endif
