#include "img_decode.h"

#include <thread>
#include <chrono>

// 低时延链路的第二级：
// usb_camera 发出压缩 MJPEG 后，这里负责“解码 + 缩放 + 保持原时间戳”。
//
// 为什么缩放要放在这里做：
// 1. 越早把 1280x720 变成 640x360，后面的推理和跟踪节点越省资源；
// 2. 解码之后直接调用 RGA 做硬缩放，可以尽量避免 CPU 大拷贝；
// 3. 对下游来说，始终只消费统一尺寸的小图，代码路径更简单。

// 如果没有波浪线，nh.param 后面的参数要加上节点名字，否则获取不到 launch 中的参数，
// 所以这里统一使用私有命名空间 "~"。
ImgDecode::ImgDecode() : nh("~") 
{
    string pub_raw_image_topic,camera_info_topic;
    string camera_name,camera_info_url;
    int width,height;

    // sub_jpeg_image_topic:
    // 上游压缩图像话题，通常来自 usb_camera 的 /image_raw/compressed。
    nh.param<string>("sub_jpeg_image_topic", sub_jpeg_image_topic, "/image_raw/compressed");

    // pub_raw_image_topic:
    // 当前节点发布的 RGB 小图话题，通常供 RKNN 推理节点订阅。
    nh.param<string>("pub_raw_image_topic", pub_raw_image_topic, "/camera/image_raw");

    // fps_div:
    // 输入帧分频，值越大则向后游送帧越少。
    nh.param<int>("fps_div", fps_div, 1);

    // scale:
    // 输出图像的缩放比例，例如 0.5 对应 1280x720 -> 640x360。
    nh.param<double>("scale", scale, 0.5);

    // width / height:
    // 告诉硬解码器初始化时要按什么输入分辨率准备缓冲区。
    nh.param<int>("width", width, 1280);
    nh.param<int>("height", height, 720);

    // 这里发布的是解码后的 raw RGB 图像。队列保持默认深度 10，
    // 后续真正避免积压主要依赖 run_check_thread 的按需订阅策略。
    raw_image_pub = nh.advertise<sensor_msgs::Image>(pub_raw_image_topic, 10);
    

#if(USE_ARM_LIB==1)
    // ARM 板端直接初始化 Rockchip MPP 解码器。
    mpp_decode.init(width,height);
#endif

}

/**
 * 解码并缩放一帧 JPEG 压缩图像。
 *
 * 整体执行顺序：
 * 1. 先做分频；
 * 2. 走 MPP 或 OpenCV 解码得到 RGB；
 * 3. 直接把缩放结果写进 msg_pub.data；
 * 4. 保留原 header 后发布给推理节点。
 *
 * @param msg 压缩图像消息：
 *            - msg->header: 采集节点写入的时间戳和 frame_id；
 *            - msg->format: 一般为 "jpeg"；
 *            - msg->data:   压缩 JPEG 字节流。
 */
void ImgDecode::compressed_image_callback(const sensor_msgs::CompressedImageConstPtr& msg)
{
    frame_cnt++;

    // 分频逻辑：通过跳帧直接减少整个后半段链路的吞吐压力。
    if(frame_cnt%fps_div!=0)//分频 减少后续处理负担
    {
        return;
    }


// auto t1 = std::chrono::system_clock::now();

    
#if(USE_ARM_LIB==1)
    // 极小的 JPEG 包通常意味着采集异常或数据损坏，提前丢掉避免喂给硬解码器。
    if(msg->data.size() <= 4096) //一般情况下，JPEG图像不能小于4KB
    {
        ROS_WARN("jpeg data size error! size = %d\n",msg->data.size());
        return;
    }

    // 硬解码 JPEG -> RGB。
    // image 可能直接引用 MPP 输出 buffer，因此后面优先让 RGA 继续在这块数据上操作。
    int ret = mpp_decode.decode((unsigned char*)msg->data.data(), msg->data.size(), image);//msg-->image
    if(ret < 0)
    {
        ROS_WARN("jpeg decode error! size = %d\n",msg->data.size());
        return;
    }
#else
    // 非 ARM 平台走 OpenCV 软解码：
    // imdecode 默认输出 BGR，因此要再转一次 RGB，保证全链路编码一致。
    image = cv::imdecode(cv::Mat(msg->data), cv::IMREAD_COLOR);
    if (image.empty())
    {
        ROS_WARN("Failed to decode compressed image");
        return;
    }
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
#endif

// auto t2 = std::chrono::system_clock::now();
    
    // 继续沿用采集节点的 header：
    // 这样从采集 -> 解码 -> 推理 -> 跟踪都共享同一套时间基准。
    msg_pub.header = msg->header;//使用原有时间戳

    // 输出尺寸是缩放后的尺寸。
    msg_pub.height = image.rows * scale;
    msg_pub.width =  image.cols * scale;
    msg_pub.encoding = "rgb8";

    // step = 每一行的字节数。RGB8 每像素 3 字节。
    msg_pub.step = msg_pub.width * 3;

    // 直接为输出消息申请最终大小的 buffer，让缩放结果落到最终发布内存里。
    msg_pub.data.resize(msg_pub.height * msg_pub.width * 3);

    //避免图像多次拷贝，直接将缩放后的数据写到msg_pub中

#if(USE_ARM_LIB==1)
    // 硬缩放 RGB -> 小 RGB：
    // 输入是解码后的原图，输出直接写到 ROS 消息 data 中。
    rga_resize(image, msg_pub.data.data(), scale);//image-->msg_pub.data
#else
    // 非 ARM 平台退化为 OpenCV resize + memcpy。
    cv::resize(image, image, cv::Size(), scale, scale);
    memcpy(msg_pub.data.data(), image.data, msg_pub.height * msg_pub.width * 3);
#endif
    
// auto t3 = std::chrono::system_clock::now();

    // 发布给推理节点。到这里为止，1280x720 大图已经被前置压缩成 640x360 小图。
    raw_image_pub.publish(msg_pub);//发布图像

// auto t4 = std::chrono::system_clock::now();

// ROS_WARN_THROTTLE(1,"decode=%d ms resize=%d ms pub=%d ms",
// std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count(),
// std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count(),
// std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count());
        
}

/**
 * 根据 raw_image_pub 的订阅者数量决定是否订阅上游压缩流。
 *
 * 这是低时延链路里很关键的一个资源控制点：
 * - 没有人用解码结果时，不要继续解码；
 * - 一旦有人开始订阅，再恢复上游订阅。
 */
void ImgDecode::run_check_thread()
{
    int last_subscribers = 0;
    ros::Rate check_rate(10); 

    while(ros::ok())
    {
        int subscribers = raw_image_pub.getNumSubscribers();
        // 当前节点没有任何消费者时，直接断开上游压缩图像订阅，整段链路都能跟着空闲下来。
        if(subscribers==0 && last_subscribers>0)
        {
            ROS_INFO("decode image subscribers = 0, src sub shutdown");
            compressed_image_sub.shutdown();
        }
        // 重新出现消费者时，再打开上游订阅恢复处理。
        else if(subscribers>0 && last_subscribers==0)
        {
            ROS_INFO("decode image subscribers > 0, src sub start");
            compressed_image_sub = nh.subscribe(sub_jpeg_image_topic, 10, &ImgDecode::compressed_image_callback,this);
        }

        last_subscribers = subscribers;

        check_rate.sleep(); 
    }
}



int main(int argc, char** argv)
{
    ros::init(argc, argv, "img_decode");

    ImgDecode img_decode;

    // 用独立线程监控订阅者数量，主线程则专注于 ROS 回调。
    std::thread check_thread(&ImgDecode::run_check_thread, &img_decode);

    ros::spin();


    
    return 0;
}



