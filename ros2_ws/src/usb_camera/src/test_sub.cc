#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sys/time.h>

using namespace std;

// 统计变量
unsigned int recv_frames = 0;
double start_time = 0.0;
double max_fps = 0.0, min_fps = 1000.0;
double last_recv_time = 0.0;

// 订阅回调函数
void imageCallback(const sensor_msgs::msg::CompressedImage::SharedPtr msg, rclcpp::Logger logger)
{
    double current_time = rclcpp::Clock().now().seconds();
    if (recv_frames == 0)
    {
        start_time = current_time;
        last_recv_time = current_time;
    }

    // 计算实时帧率
    double frame_interval = current_time - last_recv_time;
    double current_fps = 1.0 / frame_interval;
    max_fps = max(max_fps, current_fps);
    min_fps = min(min_fps, current_fps);

    // 统计消息延迟（发布时间-接收时间）
    double msg_stamp_sec = rclcpp::Time(msg->header.stamp).seconds();
    double msg_delay = (current_time - msg_stamp_sec) * 1000; // 毫秒

    recv_frames++;
    last_recv_time = current_time;

    // 每30帧输出统计
    if (recv_frames % 30 == 0)
    {
        double elapsed_time = current_time - start_time;
        double avg_fps = recv_frames / elapsed_time;
        RCLCPP_INFO(logger, "=== ROS2 Subscribe FPS Stats ===");
        RCLCPP_INFO(logger, "Average subscribe FPS: %.2f", avg_fps);
        RCLCPP_INFO(logger, "Max subscribe FPS: %.2f, Min subscribe FPS: %.2f", max_fps, min_fps);
        RCLCPP_INFO(logger, "Total received frames: %d", recv_frames);
        RCLCPP_INFO(logger, "Average message delay: %.2f ms", msg_delay);
        RCLCPP_INFO(logger, "================================");
    }
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("image_subscriber");

    // 订阅图像话题，使用标准ROS2订阅
    auto sub = node->create_subscription<sensor_msgs::msg::CompressedImage>(
        "/jpeg_image", 
        10, 
        [node](const sensor_msgs::msg::CompressedImage::SharedPtr msg) {
            imageCallback(msg, node->get_logger());
        });

    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
