#include <ros/ros.h>
#include <ros/package.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/CameraInfo.h>
#include <opencv2/opencv.hpp>
#include <camera_info_manager/camera_info_manager.h>
#include <image_transport/image_transport.h>

#include <iostream>
#include <sys/time.h>
#include <math.h>

#if(USE_ARM_LIB==1)
    #include "mpp_encode.h"
    #include "rga_cvtcolor.h"
#endif

using namespace std;
using namespace cv;

/**
 * 低时延视觉链路的回传编码节点。
 *
 * 输入：/camera/image_det_track（带跟踪信息的 RGB 图像）
 * 输出：/camera/image_det_track/compressed（JPEG 压缩图像）
 *
 * 这一级的职责不是参与控制闭环，而是把可视化结果重新压缩后提供给
 * Web / 上位机 / 远程显示，避免显示链路继续传大尺寸 raw 图像。
 */
class ImgEncode
{
public:
    ImgEncode();


    /**
     * 把一帧 RGB 图像编码成 JPEG。
     *
     * @param msg sensor_msgs/Image：
     *            - header:   来自跟踪节点的时间戳和 frame_id；
     *            - encoding: 预期是 rgb8；
     *            - data:     原始 RGB 像素数据。
     */
    void image_callback(const sensor_msgs::ImageConstPtr& msg);

    ros::NodeHandle nh;

    // 只有在真的有人订阅压缩回传图像时，才打开上游 raw 图像订阅。
    ros::Subscriber image_sub;

    // JPEG 压缩图像发布者，话题名通常是 sub_image_topic + "/compressed"。
    ros::Publisher jpeg_pub;

    // 复用的压缩图像消息对象。
    sensor_msgs::CompressedImage msg_pub;

    // JPEG 质量参数，值越大画质越好、码流也越大。
    int jpeg_quality;

    // 上游 raw 图像话题。
    string sub_image_topic;
    
    /**
     * 根据下游订阅者数量决定是否订阅上游 raw 图像。
     */
    void run_check_thread();

#if(USE_ARM_LIB==1)
    // Rockchip 平台下的 MPP JPEG 编码器。
    MppEncode mpp_encode;
#endif
};
