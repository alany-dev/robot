#if(USE_ARM_LIB==1)

#include <opencv2/opencv.hpp>

/**
 * 用 RGA 执行 RGB -> YUV420P 颜色空间转换。
 *
 * @param img_rgb 输入 RGB 图像。
 * @param img_yuv 输出 YUV420P 图像，调用方负责提前按 rows * 3 / 2 分配好空间。
 * @return        RGA 状态码；0 / IM_STATUS_NOERROR 表示成功。
 */
int rga_cvtcolor(const cv::Mat &img_rgb,cv::Mat &img_yuv);

#endif
