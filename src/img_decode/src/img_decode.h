#include <ros/ros.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CompressedImage.h>
#include <opencv2/opencv.hpp>

#include <iostream>
#include <string>
#include <sys/time.h>
#include <math.h>

#if(USE_ARM_LIB==1)
    #include "mpp_decode.h"
    #include "rga_resize.h"
#endif

using namespace std;
using namespace cv;

/**
 * 低时延链路的解码与预处理节点。
 *
 * 输入：/image_raw/compressed（MJPEG / JPEG 压缩帧）
 * 输出：/camera/image_raw（RGB 小图）
 *
 * 这个节点承担两件关键工作：
 * 1. 把采集端压缩帧解码成 RGB；
 * 2. 在链路前半段立刻把大图缩成推理需要的小图，降低后续 NPU / CPU / 带宽压力。
 */
class ImgDecode
{
public:
    ImgDecode();

    // 私有命名空间句柄，用于读取 launch 参数并创建发布/订阅。
    ros::NodeHandle nh;

    // 压缩图像订阅者：只有当下游有人订阅 raw 图像时才真正打开。
    ros::Subscriber compressed_image_sub;
    ros::Subscriber raw_image_sub;

    // 解码后的小图发布者，作为后续 RKNN 推理节点的输入。
    ros::Publisher raw_image_pub;

    // 复用的发布消息对象，避免每帧重新构造 sensor_msgs::Image。
    sensor_msgs::Image msg_pub;

    // image:
    // 当前解码得到的 RGB 图像。ARM 平台下可能直接引用 MPP 输出 buffer。
    cv::Mat image;

    // fps_div:
    // 对输入压缩流做分频，跳帧降低后续处理负担。
    int fps_div;

	// scale:
	// 解码后的缩放比例。例如 0.5 表示 1280x720 -> 640x360。
	double scale;

    // 上游输入格式配置，支持 mjpeg / yuyv。
    int width = 1280;
    int height = 720;
    string input_format = "mjpeg";

    // frame_cnt:
    // 已收到的压缩帧数量，用于分频逻辑。
    unsigned int frame_cnt=0;

    /**
     * 处理一帧压缩图像。
     *
     * @param msg sensor_msgs/CompressedImage：
     *            - header.stamp: 采集节点写入的时间戳；
     *            - format:       一般是 "jpeg"；
     *            - data:         压缩后的 JPEG 字节流。
     */
    void compressed_image_callback(const sensor_msgs::CompressedImageConstPtr& msg);
    void raw_image_callback(const sensor_msgs::ImageConstPtr& msg);

    // 上游压缩图像话题名称，通常是 /image_raw/compressed。
    string sub_jpeg_image_topic;
    string sub_raw_image_topic;

    /**
     * 根据下游订阅者数量决定是否打开上游订阅。
     *
     * 这样做的目的是：
     * 1. 没有人用 raw 图像时，不做无意义解码；
     * 2. 避免在相机、解码、推理三段都持续占资源。
     */
    void run_check_thread();

#if(USE_ARM_LIB==1)
    // Rockchip 平台下的 MJPEG 硬解码器。
    MppDecode mpp_decode;
#endif

};
