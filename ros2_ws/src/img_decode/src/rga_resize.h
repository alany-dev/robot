#if(USE_ARM_LIB==1)

#include <opencv2/opencv.hpp>

/**
 * ROS2 版 RGA 缩放接口。
 *
 * @param img_in       输入 RGB 图像。
 * @param img_out_data 输出缓冲区首地址。
 * @param scale        缩放比例。
 * @return             RGA 状态码。
 */
int rga_resize(cv::Mat &img_in,unsigned char *img_out_data,float scale);

#endif
