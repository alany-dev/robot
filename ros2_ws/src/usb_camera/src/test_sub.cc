#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sys/time.h>

using namespace std;

// ROS2 压缩图像订阅测试节点：
// 用来观察在 ROS2 / DDS 模型下，压缩图像链路的接收 FPS 和通信时延。

// 统计变量
unsigned int recv_frames = 0;
double start_time = 0.0;
double max_fps = 0.0, min_fps = 1000.0;
double last_recv_time = 0.0;

/**
 * 统计 ROS2 压缩图像接收 FPS 和时延。
 *
 * @param msg    一帧压缩图像消息。
 * @param logger 当前节点 logger，用于打印统计信息。
 */
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

    // 口径：接收时刻 - 发布时间戳。
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

    // 测试端这里使用标准 ROS2 订阅方式，方便和 ROS1 / 共享内存方案对比。
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
