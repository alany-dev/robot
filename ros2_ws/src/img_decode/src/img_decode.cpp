#include "img_decode.h"

#include <thread>
#include <chrono>

ImgDecode::ImgDecode() : Node("img_decode")
{
    std::string pub_raw_image_topic;
    int width, height;

    this->declare_parameter<std::string>("sub_jpeg_image_topic", "/image_raw/compressed");
    this->declare_parameter<std::string>("pub_raw_image_topic", "/camera/image_raw");
    this->declare_parameter<int>("fps_div", 1);
    this->declare_parameter<double>("scale", 1);
    this->declare_parameter<int>("width", 1280);
    this->declare_parameter<int>("height", 720);

    this->get_parameter("sub_jpeg_image_topic", sub_jpeg_image_topic);
    this->get_parameter("pub_raw_image_topic", pub_raw_image_topic);
    this->get_parameter("fps_div", fps_div);
    this->get_parameter("scale", scale);
    this->get_parameter("width", width);
    this->get_parameter("height", height);

    // 创建发布者 - 使用历史深度=1，只保留最新消息，避免旧数据堆积
    rclcpp::QoS qos_pub(1);
    qos_pub.best_effort();  
    qos_pub.durability_volatile(); 
    raw_image_pub = this->create_publisher<sensor_msgs::msg::Image>(pub_raw_image_topic, qos_pub);

#if(USE_ARM_LIB==1)
    mpp_decode.init(width, height);
#endif

    // 创建订阅者（初始时不订阅，等待有订阅者时再订阅）
    // 使用定时器检查订阅者数量
    check_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(100),
        [this]() {
            size_t subscribers = raw_image_pub->get_subscription_count();
            
            if (subscribers == 0 && subscriber_count_ > 0)
            {
                RCLCPP_INFO(this->get_logger(), "decode image subscribers = 0, src sub shutdown");
                compressed_image_sub.reset();  // 取消订阅
                subscriber_count_ = 0;
            }
            else if (subscribers > 0 && subscriber_count_ == 0)
            {
                RCLCPP_INFO(this->get_logger(), "decode image subscribers > 0, src sub start");
                // 创建订阅者 - 使用历史深度=1，只保留最新消息
                rclcpp::QoS qos_sub(1);
                qos_sub.best_effort();  
                qos_sub.durability_volatile(); 
                compressed_image_sub = this->create_subscription<sensor_msgs::msg::CompressedImage>(
                    sub_jpeg_image_topic, qos_sub,
                    std::bind(&ImgDecode::compressed_image_callback, this, std::placeholders::_1));
                subscriber_count_ = subscribers;
            }
            else if (subscribers != subscriber_count_)
            {
                subscriber_count_ = subscribers;
            }
        });

    RCLCPP_INFO(this->get_logger(), "img_decode node initialized");
    RCLCPP_INFO(this->get_logger(), "  sub_jpeg_image_topic: %s", sub_jpeg_image_topic.c_str());
    RCLCPP_INFO(this->get_logger(), "  pub_raw_image_topic: %s", pub_raw_image_topic.c_str());
    RCLCPP_INFO(this->get_logger(), "  fps_div: %d", fps_div);
    RCLCPP_INFO(this->get_logger(), "  scale: %.2f", scale);
}

void ImgDecode::compressed_image_callback(const sensor_msgs::msg::CompressedImage::SharedPtr msg)
{
    frame_cnt++;

    if(frame_cnt % fps_div != 0)  // 分频，减少后续处理负担
    {
        return;
    }

// auto t1 = std::chrono::system_clock::now();

#if(USE_ARM_LIB==1)
    if(msg->data.size() <= 4096)  // 一般情况下，JPEG图像不能小于4KB
    {
        RCLCPP_WARN(this->get_logger(), "jpeg data size error! size = %zu", msg->data.size());
        return;
    }

    // 硬解码 JPEG->RGB
    int ret = mpp_decode.decode((unsigned char*)msg->data.data(), msg->data.size(), image);  // msg-->image
    if(ret < 0)
    {
        RCLCPP_WARN(this->get_logger(), "jpeg decode error! size = %zu", msg->data.size());
        return;
    }
#else
    // 软解码 JPEG->BGR->RGB
    image = cv::imdecode(cv::Mat(msg->data), cv::IMREAD_COLOR);
    if (image.empty())
    {
        RCLCPP_WARN(this->get_logger(), "Failed to decode compressed image");
        return;
    }
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
#endif

// auto t2 = std::chrono::system_clock::now();

    // 使用原有时间戳
    msg_pub.header = msg->header;
    msg_pub.height = static_cast<uint32_t>(image.rows * scale);
    msg_pub.width = static_cast<uint32_t>(image.cols * scale);
    msg_pub.encoding = "rgb8";
    msg_pub.step = msg_pub.width * 3;
    msg_pub.is_bigendian = false;
    msg_pub.data.resize(msg_pub.height * msg_pub.width * 3);

    // 避免图像多次拷贝，直接将缩放后的数据写到msg_pub中

#if(USE_ARM_LIB==1)
    // 硬缩放 RGB->小RGB
    rga_resize(image, msg_pub.data.data(), scale);  // image-->msg_pub.data
#else
    // 软缩放 RGB->小RGB
    cv::resize(image, image, cv::Size(), scale, scale);
    memcpy(msg_pub.data.data(), image.data, msg_pub.height * msg_pub.width * 3);
#endif

// auto t3 = std::chrono::system_clock::now();

    raw_image_pub->publish(msg_pub);  // 发布图像

// auto t4 = std::chrono::system_clock::now();

// RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
//     "decode=%d ms resize=%d ms pub=%d ms",
//     std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count(),
//     std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count(),
//     std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count());
}



int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    
    auto img_decode = std::make_shared<ImgDecode>();
    
    rclcpp::spin(img_decode);
    
    rclcpp::shutdown();
    return 0;
}

