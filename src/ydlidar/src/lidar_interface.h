#pragma once

#include <ros/ros.h>
#include "sensor_msgs/LaserScan.h"
#include <string>
#include <memory>
#include <unordered_map>

struct LidarScanData {
  std::vector<float> ranges;           // 距离数据（米）
  std::vector<float> intensities;      // 强度数据
  float angle_min;                      // 最小角度（弧度）
  float angle_max;                      // 最大角度（弧度）
  float angle_increment;                // 角度增量（弧度）
  float range_min;                      // 最小距离（米）
  float range_max;                      // 最大距离（米）
  float scan_time;                      // 扫描时间（秒）
  float time_increment;                 // 时间增量（秒）
  ros::Time stamp;                      // 时间戳
  std::string frame_id;                 // 坐标系ID
};


struct LidarConfig {
  // 串口配置
  std::string port = "/dev/ydlidar";
  int baudrate = 115200;
  
  // 角度配置（度）
  float angle_min = -180.0f;
  float angle_max = 180.0f;
  

  float range_min = 0.1f;
  float range_max = 16.0f;
  

  float frequency = 10.0f;            
  int sample_rate = 9;                  
  bool fixed_resolution = true;         
  bool reversion = true;            
  bool inverted = true;              
  bool intensity = false;              
  bool auto_reconnect = true;          
  bool isSingleChannel = false;       
  bool support_motor_dtr = false;      
  
  int lidar_type = 1;                   // 雷达类型: 0=TYPE_TOF, 1=TYPE_TRIANGLE, 2=TYPE_TOF_NET
  int device_type = 0;                  // 设备类型: 0=YDLIDAR_TYPE_SERIAL, 1=YDLIDAR_TYPE_TCP

  // 其他配置
  std::string frame_id = "laser_frame";
  std::string ignore_array = "";       
  int abnormal_check_count = 4;         
  bool invalid_range_is_inf = false;    
};

class LidarInterface {
public:
  virtual ~LidarInterface() = default;

  virtual bool initialize(const LidarConfig& config) = 0;
  virtual bool startScan() = 0;
  virtual bool stopScan() = 0;
  virtual bool getScanData(LidarScanData& scan_data) = 0;
  virtual void disconnect() = 0;
  virtual std::string getErrorDescription() const = 0;
  virtual bool isConnected() const = 0;
  virtual bool isScanning() const = 0;
  virtual std::string getLidarType() const = 0;

  virtual bool updateConfig(const LidarConfig& config) = 0;
};


enum class LidarType {
  YDLIDAR,      // YdLidar雷达
  RPLIDAR,      // RPLidar雷达（预留）
  LDLIDAR,       // LDLidar雷达（预留）
  UNKNOWN       // 未知类型
};


inline LidarType stringToLidarType(const std::string& type_str) {
  static const std::unordered_map<std::string, LidarType> type_map = {
    {"ydlidar", LidarType::YDLIDAR},
    {"YdLidar", LidarType::YDLIDAR},
    {"YDLIDAR", LidarType::YDLIDAR},
    {"rplidar", LidarType::RPLIDAR},
    {"RPLidar", LidarType::RPLIDAR},
    {"RPLIDAR", LidarType::RPLIDAR},
    {"ldlidar", LidarType::LDLIDAR},
    {"LDLidar", LidarType::LDLIDAR},
    {"LDLIDAR", LidarType::LDLIDAR}
  };
  
  auto it = type_map.find(type_str);
  if (it != type_map.end()) {
    return it->second;
  }
  
  return LidarType::UNKNOWN;
}


inline std::string lidarTypeToString(LidarType type) {
  switch (type) {
    case LidarType::YDLIDAR: return "YdLidar";
    case LidarType::RPLIDAR: return "RPLidar";
    case LidarType::LDLIDAR: return "LDLidar";
    default: return "Unknown";
  }
}
