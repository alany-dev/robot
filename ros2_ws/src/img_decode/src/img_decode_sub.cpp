#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>

using namespace std;

// ROS2 raw 图像时延测试节点：
// 口径与 ROS1 测试一致，都是 receive_time - header.stamp。

std::vector<double> delay_list;
const int MAX_DELAY_LIST_SIZE = 100;

// 全局变量：统计帧率
rclcpp::Time last_receive_time;
int frame_count = 0;
double fps = 0.0;

class ImageSubscriberNode : public rclcpp::Node
{
public:
    ImageSubscriberNode() : Node("img_decode_sub")
    {
        this->declare_parameter<std::string>("image_topic", "/camera/image_raw");

        std::string image_topic;
        this->get_parameter("image_topic", image_topic);

        // 测试端 QoS 也和主链路保持一致：
        // depth=1 + best_effort + volatile，只关心最新消息。
        rclcpp::QoS qos_profile(1);
        qos_profile.best_effort();  
        qos_profile.durability_volatile(); 
        subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
            image_topic, qos_profile,
            std::bind(&ImageSubscriberNode::image_callback, this, std::placeholders::_1));

        RCLCPP_INFO(this->get_logger(), "IMG DECODE SUB client start! Waiting for %s topic...", image_topic.c_str());
    }

private:
    /**
     * @param msg 一帧 raw 图像消息。
     */
    void image_callback(const sensor_msgs::msg::Image::SharedPtr msg)
    {
       
        printf("[IMG DECODE SUB]: Received image [%s]:\n", msg->header.frame_id.c_str());
        printf("[IMG DECODE SUB]: Resolution: %ux%u | Encoding: %s | Step: %u | Data size: %zu bytes\n",
               msg->width, msg->height, msg->encoding.c_str(), msg->step, msg->data.size());

    
        rclcpp::Time publish_time = msg->header.stamp;  
        rclcpp::Time receive_time = this->now();     

        if (publish_time.nanoseconds() == 0) {
            RCLCPP_WARN(this->get_logger(), "[IMG DECODE SUB]: Invalid publish time (stamp is zero), skip delay calculation!");
            return;
        }

        // 当前口径：接收时间 - 发布时间戳。
        rclcpp::Duration delay = receive_time - publish_time;
        double delay_ms = delay.nanoseconds() / 1000000.0; 


        delay_list.push_back(delay_ms);
        if (delay_list.size() > MAX_DELAY_LIST_SIZE) {
            delay_list.erase(delay_list.begin());  
        }
 
        double avg_delay = std::accumulate(delay_list.begin(), delay_list.end(), 0.0) / delay_list.size();
        double max_delay = *std::max_element(delay_list.begin(), delay_list.end());
        double min_delay = *std::min_element(delay_list.begin(), delay_list.end());

        if (last_receive_time.nanoseconds() != 0) {
            rclcpp::Duration time_diff = receive_time - last_receive_time;
            if (time_diff.nanoseconds() > 0) {
                double instant_fps = 1.0 / (time_diff.nanoseconds() / 1e9);
                fps = fps * 0.9 + instant_fps * 0.1;
            }
        }
        last_receive_time = receive_time;
        frame_count++;

        printf("[COMM DELAY INFO]: 单帧时延 = %.3f ms | 平均时延 = %.3f ms | 最大时延 = %.3f ms | 最小时延 = %.3f ms | 统计帧数 = %zu\n",
               delay_ms, avg_delay, max_delay, min_delay, delay_list.size());

        printf("[FPS INFO]: 当前帧率 = %.2f Hz | 总接收帧数 = %d\n", fps, frame_count);

        printf("----------------------------------------\n");
    }

    // 订阅对象本身。
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ImageSubscriberNode>());
    rclcpp::shutdown();
    return 0;
}
