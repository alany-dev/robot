#include "ydlidar_adapter.h"
#include <ros/ros.h>
#include <limits>
#include <cmath>

YdLidarAdapter::YdLidarAdapter() 
  : lidar_(std::make_unique<CYdLidar>()), 
    initialized_(false), 
    scanning_(false) {
}

YdLidarAdapter::~YdLidarAdapter() {
  disconnect();
}

bool YdLidarAdapter::initialize(const LidarConfig& config) {
  if (initialized_) {
    ROS_WARN("[YdLidarAdapter] Already initialized, disconnecting first...");
    disconnect();
  }
  
  current_config_ = config;
  applyConfigToLidar(config);
  
  bool ret = lidar_->initialize();
  if (ret) {
    initialized_ = true;
    ROS_INFO("[YdLidarAdapter] Initialized successfully");
  } else {
    ROS_ERROR("[YdLidarAdapter] Initialize failed: %s", lidar_->DescribeError());
  }
  
  return ret;
}

bool YdLidarAdapter::startScan() {
  if (!initialized_) {
    ROS_ERROR("[YdLidarAdapter] Not initialized, cannot start scan");
    return false;
  }
  
  bool ret = lidar_->turnOn();
  if (ret) {
    scanning_ = true;
    ROS_INFO("[YdLidarAdapter] Scan started");
  } else {
    ROS_WARN("[YdLidarAdapter] Failed to start scan: %s", lidar_->DescribeError());
  }
  
  return ret;
}

bool YdLidarAdapter::stopScan() {
  if (!scanning_) {
    return true; // 已经停止
  }
  
  bool ret = lidar_->turnOff();
  if (ret) {
    scanning_ = false;
    ROS_INFO("[YdLidarAdapter] Scan stopped");
  } else {
    ROS_WARN("[YdLidarAdapter] Failed to stop scan");
  }
  
  return ret;
}

bool YdLidarAdapter::getScanData(LidarScanData& scan_data) {
  if (!scanning_) {
    return false;
  }
  
  LaserScan ydlidar_scan;
  if (lidar_->doProcessSimple(ydlidar_scan)) {
    convertScanData(ydlidar_scan, scan_data);
    return true;
  }
  
  return false;
}

void YdLidarAdapter::disconnect() {
  if (scanning_) {
    stopScan();
  }
  
  if (initialized_) {
    lidar_->disconnecting();
    initialized_ = false;
    ROS_INFO("[YdLidarAdapter] Disconnected");
  }
}

std::string YdLidarAdapter::getErrorDescription() const {
  if (lidar_) {
    return std::string(lidar_->DescribeError());
  }
  return "Lidar not initialized";
}

bool YdLidarAdapter::isConnected() const {
  return initialized_;
}

bool YdLidarAdapter::isScanning() const {
  return scanning_;
}

std::string YdLidarAdapter::getLidarType() const {
  return "YdLidar";
}

bool YdLidarAdapter::updateConfig(const LidarConfig& config) {
  if (!initialized_) {
    ROS_WARN("[YdLidarAdapter] Not initialized, cannot update config");
    return false;
  }
  
  current_config_ = config;
  applyConfigToLidar(config);
  ROS_INFO("[YdLidarAdapter] Config updated");
  return true;
}

void YdLidarAdapter::applyConfigToLidar(const LidarConfig& config) {
  // 串口配置
  lidar_->setlidaropt(LidarPropSerialPort, config.port.c_str(), config.port.size());
  
  int optval = config.baudrate;
  lidar_->setlidaropt(LidarPropSerialBaudrate, &optval, sizeof(int));
  
  // 忽略角度数组
  if (!config.ignore_array.empty()) {
    lidar_->setlidaropt(LidarPropIgnoreArray, config.ignore_array.c_str(), config.ignore_array.size());
  }
  
  // 雷达类型
  optval = TYPE_TRIANGLE;
  lidar_->setlidaropt(LidarPropLidarType, &optval, sizeof(int));
  
  // 设备类型
  optval = YDLIDAR_TYPE_SERIAL;
  lidar_->setlidaropt(LidarPropDeviceType, &optval, sizeof(int));
  
  // 采样率
  optval = config.sample_rate;
  lidar_->setlidaropt(LidarPropSampleRate, &optval, sizeof(int));
  
  // 异常检查计数
  optval = config.abnormal_check_count;
  lidar_->setlidaropt(LidarPropAbnormalCheckCount, &optval, sizeof(int));
  
  // 布尔配置
  bool b_optvalue = config.fixed_resolution;
  lidar_->setlidaropt(LidarPropFixedResolution, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.reversion;
  lidar_->setlidaropt(LidarPropReversion, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.inverted;
  lidar_->setlidaropt(LidarPropInverted, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.auto_reconnect;
  lidar_->setlidaropt(LidarPropAutoReconnect, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.isSingleChannel;
  lidar_->setlidaropt(LidarPropSingleChannel, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.intensity;
  lidar_->setlidaropt(LidarPropIntenstiy, &b_optvalue, sizeof(bool));
  
  b_optvalue = config.support_motor_dtr;
  lidar_->setlidaropt(LidarPropSupportMotorDtrCtrl, &b_optvalue, sizeof(bool));
  
  // 浮点配置
  float f_optvalue = config.angle_max;
  lidar_->setlidaropt(LidarPropMaxAngle, &f_optvalue, sizeof(float));
  
  f_optvalue = config.angle_min;
  lidar_->setlidaropt(LidarPropMinAngle, &f_optvalue, sizeof(float));
  
  f_optvalue = config.range_max;
  lidar_->setlidaropt(LidarPropMaxRange, &f_optvalue, sizeof(float));
  
  f_optvalue = config.range_min;
  lidar_->setlidaropt(LidarPropMinRange, &f_optvalue, sizeof(float));
  
  f_optvalue = config.frequency;
  lidar_->setlidaropt(LidarPropScanFrequency, &f_optvalue, sizeof(float));
}

void YdLidarAdapter::convertScanData(const LaserScan& ydlidar_scan, LidarScanData& scan_data) {
  // 转换时间戳
  const uint64_t NANOSECONDS_PER_SECOND = 1000000000UL;
  scan_data.stamp.sec = ydlidar_scan.stamp / NANOSECONDS_PER_SECOND;
  scan_data.stamp.nsec = ydlidar_scan.stamp % NANOSECONDS_PER_SECOND;
  
  // 设置配置参数
  scan_data.angle_min = ydlidar_scan.config.min_angle;
  scan_data.angle_max = ydlidar_scan.config.max_angle;
  scan_data.angle_increment = ydlidar_scan.config.angle_increment;
  scan_data.range_min = ydlidar_scan.config.min_range;
  scan_data.range_max = ydlidar_scan.config.max_range;
  scan_data.scan_time = ydlidar_scan.config.scan_time;
  scan_data.time_increment = ydlidar_scan.config.time_increment;
  scan_data.frame_id = current_config_.frame_id;
  
  // 计算数组大小
  if (ydlidar_scan.config.angle_increment <= 0.0f) {
    ROS_ERROR_THROTTLE(1.0, "[YdLidarAdapter] Invalid angle_increment: %f", ydlidar_scan.config.angle_increment);
    return;
  }
  
  int size = static_cast<int>((ydlidar_scan.config.max_angle - ydlidar_scan.config.min_angle) 
                              / ydlidar_scan.config.angle_increment) + 1;
  
  // 预分配数组
  scan_data.ranges.resize(size, current_config_.invalid_range_is_inf ? 
                          std::numeric_limits<float>::infinity() : 0.0);
  scan_data.intensities.resize(size, 0.0);
  
  // 填充扫描点数据
  for (size_t i = 0; i < ydlidar_scan.points.size(); i++) {
    int index = static_cast<int>(std::ceil((ydlidar_scan.points[i].angle - ydlidar_scan.config.min_angle) 
                                           / ydlidar_scan.config.angle_increment));
    if (index >= 0 && index < size) {
      if (ydlidar_scan.points[i].range >= ydlidar_scan.config.min_range && 
          ydlidar_scan.points[i].range <= ydlidar_scan.config.max_range) {
        scan_data.ranges[index] = ydlidar_scan.points[i].range;
        scan_data.intensities[index] = ydlidar_scan.points[i].intensity;
      }
    }
  }
}
