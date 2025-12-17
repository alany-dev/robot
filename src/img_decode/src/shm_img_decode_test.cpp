#include "ros/ros.h"
#include "sensor_msgs/Image.h"
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
#include <shm_transport/shm_topic.hpp>

std::vector<double> delay_list;
const int MAX_DELAY_LIST_SIZE = 100;

ros::Time last_receive_time;
int frame_count = 0;
double fps = 0.0;

void imageCallback(const sensor_msgs::Image::ConstPtr& msg)
{
    printf("[SHM IMG DECODE TEST]: Received image [%s]:\n", msg->header.frame_id.c_str());
    printf("[SHM IMG DECODE TEST]: Resolution: %dx%d | Encoding: %s | Step: %u | Data size: %lu bytes\n",
           msg->width, msg->height, msg->encoding.c_str(), msg->step, msg->data.size());
  
    ros::Time publish_time = msg->header.stamp;  
    ros::Time receive_time = ros::Time::now();    
    
    if (publish_time.isZero()) {
        ROS_WARN("[SHM IMG DECODE TEST]: Invalid publish time (stamp is zero), skip delay calculation!");
        return;
    }
    
   ros::Duration delay = receive_time - publish_time;
    double delay_ms = delay.toSec() * 1000.0;
    
    delay_list.push_back(delay_ms);
    if (delay_list.size() > MAX_DELAY_LIST_SIZE) {
        delay_list.erase(delay_list.begin()); // 移除最旧数据
    }
    
    double avg_delay = std::accumulate(delay_list.begin(), delay_list.end(), 0.0) / delay_list.size();
    double max_delay = *std::max_element(delay_list.begin(), delay_list.end());
    double min_delay = *std::min_element(delay_list.begin(), delay_list.end());
    
    if (!last_receive_time.isZero()) {
        ros::Duration time_diff = receive_time - last_receive_time;
        if (time_diff.toSec() > 0) {
            double instant_fps = 1.0 / time_diff.toSec();
            fps = fps * 0.9 + instant_fps * 0.1;
        }
    }
    last_receive_time = receive_time;
    frame_count++;
    
    printf("[SHM COMM DELAY INFO]: 单帧时延 = %.3f ms | 平均时延 = %.3f ms | 最大时延 = %.3f ms | 最小时延 = %.3f ms | 统计帧数 = %lu\n",
           delay_ms, avg_delay, max_delay, min_delay, delay_list.size());
    
    printf("[SHM FPS INFO]: 当前帧率 = %.2f Hz | 总接收帧数 = %d\n", fps, frame_count);
    
    printf("----------------------------------------\n");
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "shm_img_decode_test");
    ros::NodeHandle n;
    
    std::string image_topic = "/camera/image_raw";
    n.param<std::string>("image_topic", image_topic, "/camera/image_raw");
    
    shm_transport::Topic shm_topic(n);
    shm_transport::Subscriber<sensor_msgs::Image> shm_sub = shm_topic.subscribe(image_topic, 1000, imageCallback);
    
    ROS_INFO("SHM IMG DECODE TEST client start! Waiting for %s topic (using shared memory transport)...", image_topic.c_str());
    ros::spin(); 

    return 0;
}
