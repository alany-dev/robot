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

#endif
