#include <ros/ros.h>
#include "sensor_msgs/LaserScan.h"
#include "std_srvs/Empty.h"
#include <limits>
#include <memory>

// 新的统一接口头文件
#include "lidar_interface.h"
#include "lidar_factory.h"
#include "lidar_config_loader.h"

#define SDKROS_VERSION "2.0.0"

/**
 * @brief 激光雷达节点类
 * 
 * 使用统一接口管理激光雷达，支持多种雷达类型
 */
class YdLidarNodeV2 {
public:
  YdLidarNodeV2() : lidar_(nullptr), initialized_(false), scanning_(false) {}
  
  ~YdLidarNodeV2() {
    shutdown();
  }
  
  // 禁止拷贝和赋值
  YdLidarNodeV2(const YdLidarNodeV2&) = delete;
  YdLidarNodeV2& operator=(const YdLidarNodeV2&) = delete;
  
  /**
   * @brief 初始化节点
   */
  bool initialize(ros::NodeHandle& nh, ros::NodeHandle& nh_private) {
    // 读取雷达类型
    std::string lidar_type_str = "ydlidar";
    nh_private.param<std::string>("lidar_type", lidar_type_str, "ydlidar");
    ROS_INFO("[YDLIDAR] Requested lidar type: %s", lidar_type_str.c_str());
    
    // 使用工厂创建雷达实例
    lidar_ = LidarFactory::createLidar(lidar_type_str);
    if (!lidar_) {
      ROS_ERROR("[YDLIDAR] Failed to create lidar of type: %s", lidar_type_str.c_str());
      ROS_INFO("[YDLIDAR] Supported types: ");
      auto supported = LidarFactory::getSupportedTypes();
      for (const auto& type : supported) {
        ROS_INFO("[YDLIDAR]   - %s", type.c_str());
      }
      return false;
    }
    
    ROS_INFO("[YDLIDAR] Created lidar: %s", lidar_->getLidarType().c_str());
    
    // 加载配置
    config_ = LidarConfigLoader::loadFromRosParams(nh_private);
    
    // 验证配置
    if (!LidarConfigLoader::validateConfig(config_)) {
      ROS_ERROR("[YDLIDAR] Invalid configuration");
      return false;
    }
    
    LidarConfigLoader::printConfig(config_);
    
    // 初始化雷达
    if (!lidar_->initialize(config_)) {
      ROS_ERROR("[YDLIDAR] Failed to initialize lidar: %s", lidar_->getErrorDescription().c_str());
      return false;
    }
    
    initialized_ = true;
    ROS_INFO("[YDLIDAR] Lidar initialized successfully");
    
    return true;
  }
  
  /**
   * @brief 启动扫描
   */
  bool startScanning() {
    if (!initialized_) {
      ROS_ERROR("[YDLIDAR] Not initialized");
      return false;
    }
    
    if (scanning_) {
      ROS_WARN("[YDLIDAR] Already scanning");
      return true;
    }
    
    bool ret = lidar_->startScan();
    if (ret) {
      scanning_ = true;
    }
    return ret;
  }
  
  /**
   * @brief 停止扫描
   */
  bool stopScanning() {
    if (!scanning_) {
      return true;
    }
    
    bool ret = lidar_->stopScan();
    if (ret) {
      scanning_ = false;
    }
    return ret;
  }
  
  /**
   * @brief 获取扫描数据并发布
   */
  bool publishScanData(ros::Publisher& pub) {
    if (!scanning_) {
      return false;
    }
    
    LidarScanData scan_data;
    if (!lidar_->getScanData(scan_data)) {
      return false;
    }
    
    // 转换为ROS消息
    sensor_msgs::LaserScan scan_msg;
    scan_msg.header.stamp = scan_data.stamp;
    scan_msg.header.frame_id = scan_data.frame_id;
    scan_msg.angle_min = scan_data.angle_min;
    scan_msg.angle_max = scan_data.angle_max;
    scan_msg.angle_increment = scan_data.angle_increment;
    scan_msg.scan_time = scan_data.scan_time;
    scan_msg.time_increment = scan_data.time_increment;
    scan_msg.range_min = scan_data.range_min;
    scan_msg.range_max = scan_data.range_max;
    scan_msg.ranges = scan_data.ranges;
    scan_msg.intensities = scan_data.intensities;
    
    pub.publish(scan_msg);
    return true;
  }
  
  /**
   * @brief 关闭节点
   */
  void shutdown() {
    if (scanning_) {
      stopScanning();
    }
    if (initialized_ && lidar_) {
      lidar_->disconnect();
      initialized_ = false;
    }
  }
  
  /**
   * @brief 服务回调：停止扫描
   */
  bool stopScanService(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {
    ROS_INFO("[YDLIDAR] Stop scan service called");
    return stopScanning();
  }
  
  /**
   * @brief 服务回调：启动扫描
   */
  bool startScanService(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {
    ROS_INFO("[YDLIDAR] Start scan service called");
    return startScanning();
  }
  
  bool isInitialized() const { return initialized_; }
  bool isScanning() const { return scanning_; }
  
private:
  std::unique_ptr<LidarInterface> lidar_;
  LidarConfig config_;
  bool initialized_;
  bool scanning_;
};

// 全局节点实例
static std::unique_ptr<YdLidarNodeV2> g_node;

// 服务回调包装函数
bool stop_scan(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {
  if (g_node) {
    return g_node->stopScanService(req, res);
  }
  return false;
}

bool start_scan(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {
  if (g_node) {
    return g_node->startScanService(req, res);
  }
  return false;
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "ydlidar_node");
  ROS_INFO("[YDLIDAR] YDLIDAR ROS Driver Version: %s (Unified Interface)", SDKROS_VERSION);
  
  ros::NodeHandle nh;
  ros::NodeHandle nh_private("~");
  
  g_node = std::make_unique<YdLidarNodeV2>();
  
  if (!g_node->initialize(nh, nh_private)) {
    ROS_ERROR("[YDLIDAR] Failed to initialize node");
    return 1;
  }
  
  int queue_size = 1;
  nh_private.param<int>("publisher_queue_size", queue_size, 1);
  ros::Publisher scan_pub = nh.advertise<sensor_msgs::LaserScan>("scan", queue_size);
  
  ros::ServiceServer stop_scan_service = nh.advertiseService("stop_scan", stop_scan);
  ros::ServiceServer start_scan_service = nh.advertiseService("start_scan", start_scan);
  
  int loop_rate = 30;
  nh_private.param<int>("loop_rate", loop_rate, 30);
  if (loop_rate <= 0) {
    ROS_WARN("[YDLIDAR] Invalid loop_rate %d, using default 30", loop_rate);
    loop_rate = 30;
  }
  ros::Rate r(loop_rate);
  ROS_INFO("[YDLIDAR] Loop rate: %d Hz", loop_rate);
  
  ROS_INFO("[YDLIDAR] Starting scan...");
  int retry_count = 0;
  const int MAX_RETRY_LOG_INTERVAL = 10;
  
  while (!g_node->startScanning() && ros::ok()) {
    retry_count++;
    if (retry_count % MAX_RETRY_LOG_INTERVAL == 0) {
      ROS_WARN("[YDLIDAR] Failed to start scan (retry %d). Check lidar connection.", retry_count);
    }
    ros::spinOnce();
    r.sleep();
  }
  
  if (!g_node->isScanning()) {
    ROS_ERROR("[YDLIDAR] Failed to start scanning after retries");
    return 1;
  }
  
  ROS_INFO("[YDLIDAR] Scan started successfully. Entering main loop...");
  
  int scan_count = 0;
  int failed_scan_count = 0;
  
  while (ros::ok() && g_node->isScanning()) {
    if (g_node->publishScanData(scan_pub)) {
      scan_count++;
      if (scan_count % 100 == 0) {
        ROS_DEBUG_THROTTLE(5.0, "[YDLIDAR] Published %d scans, failed: %d", scan_count, failed_scan_count);
      }
    } else {
      failed_scan_count++;
      ROS_WARN_THROTTLE(1.0, "[YDLIDAR] Failed to get scan data (total failures: %d)", failed_scan_count);
    }
    
    ros::spinOnce();
    r.sleep();
  }
  
  ROS_INFO("[YDLIDAR] Shutting down...");
  if (g_node) {
    g_node->shutdown();
  }
  ROS_INFO("[YDLIDAR] YDLIDAR stopped. Total scans: %d, Failed: %d", scan_count, failed_scan_count);
  
  return 0;
}
