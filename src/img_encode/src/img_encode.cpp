#include "img_encode.h"

#include <thread>

// 低时延链路的最后一级可视化回传：
// 前面的检测、跟踪、控制都基于 raw RGB 小图完成；
// 到这里才把最终结果重新压成 JPEG，单独服务于显示链路。

ImgEncode::ImgEncode() : nh("~")
{
    
    int width,height,jpeg_quality;

    // sub_image_topic:
    // 上游带跟踪结果的 raw RGB 图像话题。
    nh.param<string>("sub_image_topic", sub_image_topic, "/camera/image_raw");

    // width / height:
    // 告诉硬编码器要按什么图像尺寸准备内部缓冲。
    nh.param<int>("width", width, 640);
    nh.param<int>("height", height, 360);

    // jpeg_quality:
    // JPEG 量化质量，越大则画质越好、体积越大。
    nh.param<int>("jpeg_quality", jpeg_quality, 80);
    this->jpeg_quality = jpeg_quality;

    // 输出话题统一挂在输入 raw 图像后面，形成 xxx/compressed。
    jpeg_pub = nh.advertise<sensor_msgs::CompressedImage>(sub_image_topic+"/compressed", 10);

#if(USE_ARM_LIB==1)
    // ARM 板端走 RGA + MPP 硬编码。
    mpp_encode.init(width,height,jpeg_quality);
#endif

}


/**
 * 把一帧 raw RGB 图像编码成 JPEG。
 *
 * @param msg 输入 raw 图像消息。
 */
void ImgEncode::image_callback(const sensor_msgs::ImageConstPtr& msg)
{
    // toCvShare 不复制像素数据，直接共享底层 buffer。
    cv_bridge::CvImageConstPtr cv_ptr = cv_bridge::toCvShare(msg,"rgb8");//ROS消息转OPENCV

//auto t1 = std::chrono::system_clock::now();

#if(USE_ARM_LIB==1)
    // MPP JPEG 编码器更适合吃 YUV420P，因此先用 RGA 做颜色空间转换。
    cv::Mat image_yuv(cv_ptr->image.rows * 3/2, cv_ptr->image.cols, CV_8UC1);
    rga_cvtcolor(cv_ptr->image, image_yuv);//cv_ptr-->image_yuv420p
#endif

//auto t2 = std::chrono::system_clock::now();

#if(USE_ARM_LIB==1)
    // 硬编码 YUV420P -> JPEG。
    mpp_encode.encode(image_yuv.data, cv_ptr->image.cols * cv_ptr->image.rows * 3/2, msg_pub.data);//image_yuv-->msg_pub.data
#else
    // 非 ARM 平台走 OpenCV 软编码。OpenCV JPEG 编码输入要求 BGR。
    cv::Mat image_bgr;
    cv::cvtColor(cv_ptr->image, image_bgr, cv::COLOR_RGB2BGR);
    std::vector<int> compression_params;
    compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    compression_params.push_back(jpeg_quality);  // JPEG压缩质量
    cv::imencode(".jpg", image_bgr, msg_pub.data, compression_params);
#endif

//auto t3 = std::chrono::system_clock::now();

    // 回传图像继续沿用跟踪节点的 header，便于后面和其它消息做关联分析。
    msg_pub.header = msg->header;//使用原有时间戳
    msg_pub.format = "jpeg";
    jpeg_pub.publish(msg_pub); //发布压缩图像


// auto t4 = std::chrono::system_clock::now();

// ROS_WARN_THROTTLE(1,"yuv=%d encode=%d pub=%d ms",
// std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count(),
// std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count(),
// std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count()
// );

}


/**
 * 根据压缩输出是否真的有人订阅，决定是否继续订阅上游 raw 图像。
 */
void ImgEncode::run_check_thread()
{
    int last_subscribers = 0;
    while(ros::ok())
    {
        int subscribers = jpeg_pub.getNumSubscribers();

        // 无人订阅压缩结果时，直接停掉上游 raw 图像订阅。
        if(subscribers==0 && last_subscribers>0)
        {
            ROS_INFO("encode image subscribers = 0, src sub shutdown");
            image_sub.shutdown();
        }
        // 重新出现消费者时，再恢复 raw 图像订阅。
        else if(subscribers>0 && last_subscribers==0)
        {
            ROS_INFO("encode image subscribers > 0, src sub start");
            image_sub = nh.subscribe(sub_image_topic, 10, &ImgEncode::image_callback,this);
        }

        last_subscribers = subscribers;

        usleep(100*1000);//100ms
    }
}


int main(int argc, char** argv)
{
    ros::init(argc, argv, "img_encode");
    ImgEncode img_encode;

    // 独立线程负责按需开关上游订阅，主线程保持 ROS 回调。
    std::thread check_thread(&ImgEncode::run_check_thread, &img_encode);

    ros::spin();



    return 0;
}

