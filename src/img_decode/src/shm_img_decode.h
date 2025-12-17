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

class ImgDecode
{
public:
    ImgDecode();

    ros::NodeHandle nh;

    ros::Subscriber compressed_image_sub;
    shm_transport::Publisher shm_raw_image_pub;  // 共享内存发布者
    sensor_msgs::Image msg_pub;
    cv::Mat image;

    int fps_div;
    double scale;
    unsigned int frame_cnt=0;

    void compressed_image_callback(const sensor_msgs::CompressedImageConstPtr& msg);

    string sub_jpeg_image_topic;

    void run_check_thread();

#if(USE_ARM_LIB==1)
    MppDecode mpp_decode;
#endif

};
