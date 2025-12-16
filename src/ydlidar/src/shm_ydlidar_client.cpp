#include "ros/ros.h"
#include "sensor_msgs/LaserScan.h"
#include <cmath>       // M_PI
#include <vector>     
#include <numeric>    
#include <algorithm>   
#include <shm_transport/shm_topic.hpp>

#define RAD2DEG(x) ((x)*180./M_PI)

std::vector<double> delay_list;
const int MAX_DELAY_LIST_SIZE = 100;

void scanCallback(const sensor_msgs::LaserScan::ConstPtr& scan)
{
    int count = scan->scan_time / scan->time_increment;
    printf("[YDLIDAR INFO]: I heard a laser scan %s[%d]:\n", scan->header.frame_id.c_str(), count);
    printf("[YDLIDAR INFO]: angle_range : [%f, %f]\n", RAD2DEG(scan->angle_min), RAD2DEG(scan->angle_max));
  
    ros::Time publish_time = scan->header.stamp;  
    ros::Time receive_time = ros::Time::now();   
    
    if (publish_time.isZero()) {
        ROS_WARN("Invalid publish time (stamp is zero), skip delay calculation!");
        return;
    }
    
    ros::Duration delay = receive_time - publish_time;
    double delay_ms = delay.toSec() * 1000.0;
    
    delay_list.push_back(delay_ms);
    if (delay_list.size() > MAX_DELAY_LIST_SIZE) {
        delay_list.erase(delay_list.begin()); 
    }
    
    double avg_delay = std::accumulate(delay_list.begin(), delay_list.end(), 0.0) / delay_list.size();
    double max_delay = *std::max_element(delay_list.begin(), delay_list.end());
    double min_delay = *std::min_element(delay_list.begin(), delay_list.end());
    
    printf("[COMM DELAY INFO]: 单帧时延 = %.3f ms | 平均时延 = %.3f ms | 最大时延 = %.3f ms | 最小时延 = %.3f ms | 统计帧数 = %lu\n",
           delay_ms, avg_delay, max_delay, min_delay, delay_list.size());

    for(int i = 0; i < count; i++) {
        float degree = RAD2DEG(scan->angle_min + scan->angle_increment * i);
        if(degree > -5 && degree < 5) {
            printf("[YDLIDAR INFO]: angle-distance : [%f, %f, %i]\n", degree, scan->ranges[i], i);
        }
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "ydlidar_client");
    ros::NodeHandle n;
    
    shm_transport::Topic shm_topic(n);
    shm_transport::Subscriber<sensor_msgs::LaserScan> shm_sub = shm_topic.subscribe("/scan", 1000, scanCallback);
    
    ROS_INFO("YDLIDAR client start! Waiting for /scan topic...");
    ros::spin(); 

    return 0;
}