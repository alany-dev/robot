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

/**
 * ROS2 版压缩图像解码节点。
 *
 * 它和 ROS1 版承担同样职责：
 * - 订阅压缩 MJPEG；
 * - 解码成 RGB；
 * - 在链路前半段就缩放成小图；
 * - 发布给后续视觉节点。
 *
 * 不同点主要在 ROS2 QoS 和按需订阅实现上。
 */
class ImgDecode : public rclcpp::Node
{
public:
    ImgDecode();

    // 解码后小图发布者。
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr raw_image_pub;

    // 上游压缩图像订阅者。初始不订阅，等有人消费 raw 图像再创建。
    rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr compressed_image_sub;
    
    // 复用的输出消息和中间图像。
    sensor_msgs::msg::Image msg_pub;
    cv::Mat image;

    // 输入分频与输出缩放参数。
    int fps_div;
    double scale;
    unsigned int frame_cnt = 0;

    /**
     * @param msg 压缩图像消息。
     */
    void compressed_image_callback(const sensor_msgs::msg::CompressedImage::SharedPtr msg);

    // 上游压缩图像话题名称。
    std::string sub_jpeg_image_topic;


#if(USE_ARM_LIB==1)
    // Rockchip 平台下的 MJPEG 硬解码器。
    MppDecode mpp_decode;
#endif

private:
    // subscriber_count_:
    // 记录上一次观察到的下游订阅者数量，用于判断是否需要重建订阅。
    std::atomic<size_t> subscriber_count_{0};
    std::thread check_thread_;
    rclcpp::TimerBase::SharedPtr check_timer_;
};
