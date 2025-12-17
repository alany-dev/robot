#include "ros/ros.h"
#include "sensor_msgs/Image.h"
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>

// 全局变量：存储最近100帧的通信时延（毫秒），避免数据过多
std::vector<double> delay_list;
const int MAX_DELAY_LIST_SIZE = 100;

// 全局变量：统计帧率
ros::Time last_receive_time;
int frame_count = 0;
double fps = 0.0;

void imageCallback(const sensor_msgs::Image::ConstPtr& msg)
{
    // 1. 基础信息打印（图像信息）
    printf("[IMG DECODE SUB]: Received image [%s]:\n", msg->header.frame_id.c_str());
    printf("[IMG DECODE SUB]: Resolution: %dx%d | Encoding: %s | Step: %u | Data size: %lu bytes\n",
           msg->width, msg->height, msg->encoding.c_str(), msg->step, msg->data.size());
  
    // 2. 计算通信时延（核心逻辑）
    ros::Time publish_time = msg->header.stamp;  // 消息发布时间（img_decode节点打戳）
    ros::Time receive_time = ros::Time::now();    // 接收端当前时间（订阅回调触发时）
    
    // 防异常：发布时间无效（如0）时跳过计算
    if (publish_time.isZero()) {
        ROS_WARN("[IMG DECODE SUB]: Invalid publish time (stamp is zero), skip delay calculation!");
        return;
    }
    
    // 时延计算：秒→毫秒（更直观）
    ros::Duration delay = receive_time - publish_time;
    double delay_ms = delay.toSec() * 1000.0;
    
    // 3. 存储时延数据（控制列表长度，仅保留最近100帧）
    delay_list.push_back(delay_ms);
    if (delay_list.size() > MAX_DELAY_LIST_SIZE) {
        delay_list.erase(delay_list.begin()); // 移除最旧数据
    }
    
    // 4. 统计时延（平均/最大/最小）
    double avg_delay = std::accumulate(delay_list.begin(), delay_list.end(), 0.0) / delay_list.size();
    double max_delay = *std::max_element(delay_list.begin(), delay_list.end());
    double min_delay = *std::min_element(delay_list.begin(), delay_list.end());
    
    // 5. 计算帧率（FPS）
    if (!last_receive_time.isZero()) {
        ros::Duration time_diff = receive_time - last_receive_time;
        if (time_diff.toSec() > 0) {
            double instant_fps = 1.0 / time_diff.toSec();
            // 使用滑动平均计算FPS（更稳定）
            fps = fps * 0.9 + instant_fps * 0.1;
        }
    }
    last_receive_time = receive_time;
    frame_count++;
    
    // 6. 打印时延信息（关键）
    printf("[COMM DELAY INFO]: 单帧时延 = %.3f ms | 平均时延 = %.3f ms | 最大时延 = %.3f ms | 最小时延 = %.3f ms | 统计帧数 = %lu\n",
           delay_ms, avg_delay, max_delay, min_delay, delay_list.size());
    
    // 7. 打印帧率信息
    printf("[FPS INFO]: 当前帧率 = %.2f Hz | 总接收帧数 = %d\n", fps, frame_count);
    
    // 8. 打印分隔线（便于阅读）
    printf("----------------------------------------\n");
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "img_decode_sub");
    ros::NodeHandle n;
    
    // 从参数服务器获取订阅话题名称（默认使用img_decode发布的话题）
    std::string image_topic = "/camera/image_raw";
    n.param<std::string>("image_topic", image_topic, "/camera/image_raw");
    
    // 订阅图像话题：使用普通ROS传输
    ros::Subscriber sub = n.subscribe<sensor_msgs::Image>(image_topic, 1000, imageCallback);
    
    ROS_INFO("IMG DECODE SUB client start! Waiting for %s topic...", image_topic.c_str());
    ros::spin(); // 阻塞式处理回调，直到节点退出

    return 0;
}
