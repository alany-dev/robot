#include "lidar_config_loader.h"
#include <ros/ros.h>
#include <algorithm>

LidarConfig LidarConfigLoader::loadFromRosParams(ros::NodeHandle& nh) {
  LidarConfig config;
  
  // 串口配置
  nh.param<std::string>("port", config.port, "/dev/ttyS9");
  nh.param<int>("baudrate", config.baudrate, 115200);
  
  // 角度配置
  nh.param<float>("angle_min", config.angle_min, -180.0f);
  nh.param<float>("angle_max", config.angle_max, 180.0f);
  
  // 距离配置
  nh.param<float>("range_min", config.range_min, 0.1f);
  nh.param<float>("range_max", config.range_max, 10.0f);
  
  // 扫描配置
  nh.param<float>("frequency", config.frequency, 10.0f);
  nh.param<int>("sample_rate", config.sample_rate, 3);
  // 支持两种参数名: resolution_fixed (launch文件) 和 fixed_resolution (代码中)
  if (nh.hasParam("resolution_fixed")) {
    nh.param<bool>("resolution_fixed", config.fixed_resolution, true);
  } else {
    nh.param<bool>("fixed_resolution", config.fixed_resolution, true);
  }
  nh.param<bool>("reversion", config.reversion, false);
  nh.param<bool>("inverted", config.inverted, true);
  nh.param<bool>("intensity", config.intensity, false);
  nh.param<bool>("auto_reconnect", config.auto_reconnect, true);
  
  // 单通道和电机DTR配置
  nh.param<bool>("isSingleChannel", config.isSingleChannel, true);
  nh.param<bool>("support_motor_dtr", config.support_motor_dtr, true);
  
  // 雷达类型和设备类型配置
  nh.param<int>("lidar_type", config.lidar_type, 1);  // 默认TYPE_TRIANGLE
  nh.param<int>("device_type", config.device_type, 0); // 默认YDLIDAR_TYPE_SERIAL
  
  // 其他配置
  nh.param<std::string>("frame_id", config.frame_id, "laser_frame");
  nh.param<std::string>("ignore_array", config.ignore_array, "");
  nh.param<int>("abnormal_check_count", config.abnormal_check_count, 4);
  nh.param<bool>("invalid_range_is_inf", config.invalid_range_is_inf, false);
  
  return config;
}

bool LidarConfigLoader::validateConfig(const LidarConfig& config) {
  // 验证波特率
  if (config.baudrate <= 0) {
    ROS_ERROR("[LidarConfigLoader] Invalid baudrate: %d", config.baudrate);
    return false;
  }
  
  // 验证角度范围
  if (config.angle_min < -180.0f || config.angle_min > 180.0f ||
      config.angle_max < -180.0f || config.angle_max > 180.0f) {
    ROS_ERROR("[LidarConfigLoader] Invalid angle range: [%f, %f]", 
              config.angle_min, config.angle_max);
    return false;
  }
  
  if (config.angle_min >= config.angle_max) {
    ROS_ERROR("[LidarConfigLoader] angle_min (%f) >= angle_max (%f)", 
              config.angle_min, config.angle_max);
    return false;
  }
  
  // 验证距离范围
  if (config.range_min < 0.0f || config.range_max <= 0.0f) {
    ROS_ERROR("[LidarConfigLoader] Invalid range: [%f, %f]", 
              config.range_min, config.range_max);
    return false;
  }
  
  if (config.range_min >= config.range_max) {
    ROS_ERROR("[LidarConfigLoader] range_min (%f) >= range_max (%f)", 
              config.range_min, config.range_max);
    return false;
  }
  
  // 验证频率
  if (config.frequency <= 0.0f) {
    ROS_ERROR("[LidarConfigLoader] Invalid frequency: %f", config.frequency);
    return false;
  }
  
  // 验证采样率
  if (config.sample_rate <= 0) {
    ROS_ERROR("[LidarConfigLoader] Invalid sample_rate: %d", config.sample_rate);
    return false;
  }
  
  return true;
}

void LidarConfigLoader::printConfig(const LidarConfig& config) {
  ROS_INFO("[LidarConfigLoader] ===== Lidar Configuration =====");
  ROS_INFO("[LidarConfigLoader] Port: %s", config.port.c_str());
  ROS_INFO("[LidarConfigLoader] Baudrate: %d", config.baudrate);
  ROS_INFO("[LidarConfigLoader] Angle range: [%.2f, %.2f] degrees", 
           config.angle_min, config.angle_max);
  ROS_INFO("[LidarConfigLoader] Range: [%.2f, %.2f] meters", 
           config.range_min, config.range_max);
  ROS_INFO("[LidarConfigLoader] Frequency: %.2f Hz", config.frequency);
  ROS_INFO("[LidarConfigLoader] Sample rate: %d", config.sample_rate);
  ROS_INFO("[LidarConfigLoader] Frame ID: %s", config.frame_id.c_str());
  ROS_INFO("[LidarConfigLoader] Fixed resolution: %s", config.fixed_resolution ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Reversion: %s", config.reversion ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Inverted: %s", config.inverted ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Intensity: %s", config.intensity ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Auto reconnect: %s", config.auto_reconnect ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Single channel: %s", config.isSingleChannel ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Support motor DTR: %s", config.support_motor_dtr ? "true" : "false");
  ROS_INFO("[LidarConfigLoader] Lidar type: %d (0=TOF, 1=TRIANGLE, 2=TOF_NET)", config.lidar_type);
  ROS_INFO("[LidarConfigLoader] Device type: %d (0=SERIAL, 1=TCP)", config.device_type);
  ROS_INFO("[LidarConfigLoader] ================================");
}
