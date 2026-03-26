#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/CameraInfo.h>
#include <opencv2/opencv.hpp>
#include <camera_info_manager/camera_info_manager.h>
#include <shm_transport/shm_topic.hpp>

#include <iostream>
#include <sys/time.h>
#include <math.h>

#if(USE_ARM_LIB==1)
    #include "mpp_decode.h"
    #include "rga_resize.h"
#endif

using namespace std;
using namespace cv;

/**
 * img_decode 的共享内存输出版本。
 *
 * 它和普通 img_decode 的差别只有一处：
 * - 解码和缩放逻辑完全一致；
 * - 发布 raw RGB 图像时改走 shm_transport。
 *
 * 这样就能对比“普通 ROS loopback 传 raw 图像”和“共享内存传 raw 图像”的时延差异。
 */
class ImgDecode
{
public:
    ImgDecode();

    ros::NodeHandle nh;

    // 上游仍然是普通 ROS 压缩图像订阅。
    ros::Subscriber compressed_image_sub;

    // 下游改成共享内存发布 raw 图像。
    shm_transport::Publisher shm_raw_image_pub;  // 共享内存发布者
    sensor_msgs::Image msg_pub;
    cv::Mat image;

    int fps_div;
    double scale;
    unsigned int frame_cnt=0;

    /**
     * 与普通 img_decode 相同：解码 JPEG、缩放成 RGB 小图。
     *
     * @param msg 压缩图像消息。
     */
    void compressed_image_callback(const sensor_msgs::CompressedImageConstPtr& msg);

    string sub_jpeg_image_topic;

    /**
     * 根据共享内存下游订阅者数量决定是否继续订阅上游压缩流。
     */
    void run_check_thread();

#if(USE_ARM_LIB==1)
    MppDecode mpp_decode;
#endif

};
