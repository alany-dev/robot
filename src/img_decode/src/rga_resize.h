#if(USE_ARM_LIB==1)

#include <opencv2/opencv.hpp>

/**
 * 用 Rockchip RGA 执行 RGB 图像缩放或拷贝。
 *
 * @param img_in       输入 RGB 图像。通常来自 MPP 解码输出。
 * @param img_out_data 输出缓冲区首地址，调用方通常直接传 sensor_msgs::Image::data.data()。
 * @param scale        缩放比例：
 *                     - 1.0 表示不缩放，只做一次高效拷贝；
 *                     - 0.5 表示宽高各缩成一半。
 * @return             RGA 状态码；0 / IM_STATUS_NOERROR 表示成功。
 */
int rga_resize(cv::Mat &img_in,unsigned char *img_out_data,float scale);

/**
 * 对 NV12/YUV420SP 图像做缩放并转成 RGB。
 *
 * @param img_in_data 输入 YUV420SP 图像首地址。
 * @param width       实际图像宽度。
 * @param height      实际图像高度。
 * @param wstride     输入 buffer 的宽 stride。
 * @param hstride     输入 buffer 的高 stride。
 * @param img_out_data 输出 RGB 图像首地址。
 * @param out_width   输出图像宽度。
 * @param out_height  输出图像高度。
 * @return            RGA 返回状态码。
 */
int rga_resize_yuv420sp_to_rgb(unsigned char *img_in_data, int width, int height,
                               int wstride, int hstride, unsigned char *img_out_data,
                               int out_width, int out_height);

int rga_resize_yuyv422_to_rgb(unsigned char *img_in_data, int width, int height,
                              int wstride, int hstride, unsigned char *img_out_data,
                              int out_width, int out_height);

#endif
