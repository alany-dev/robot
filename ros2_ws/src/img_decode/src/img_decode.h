#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <opencv2/opencv.hpp>

#include <iostream>
#include <sys/time.h>
#include <math.h>
#include <thread>
#include <atomic>

#if(USE_ARM_LIB==1)
    #include "mpp_decode.h"
    #include "rga_resize.h"
#endif

using namespace std;
using namespace cv;

class ImgDecode : public rclcpp::Node
{
public:
    ImgDecode();

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr raw_image_pub;
    rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr compressed_image_sub;
    
    sensor_msgs::msg::Image msg_pub;
    cv::Mat image;

    int fps_div;
    double scale;
    unsigned int frame_cnt = 0;

    void compressed_image_callback(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

    std::string sub_jpeg_image_topic;


#if(USE_ARM_LIB==1)
    MppDecode mpp_decode;
#endif

private:
    std::atomic<size_t> subscriber_count_{0};
    std::thread check_thread_;
    rclcpp::TimerBase::SharedPtr check_timer_;
};
