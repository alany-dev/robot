#include "lidar_factory.h"
#include "ydlidar_adapter.h"
#include <ros/ros.h>

// 预留：其他雷达适配器的头文件
// #include "ldlidar_adapter.h"

//C++ 多态的概念：父类的指针指向子类的对象
std::unique_ptr<LidarInterface> LidarFactory::createLidar(LidarType type) {
  switch (type) {
    case LidarType::YDLIDAR: {
      ROS_INFO("[LidarFactory] Creating YdLidar instance");
      return std::make_unique<YdLidarAdapter>();
    }
    
    case LidarType::RPLIDAR: {
      ROS_WARN("[LidarFactory] RPLidar adapter not implemented yet");
      // return std::make_unique<RPLidarAdapter>();
      return nullptr;
    }
    
    case LidarType::LDLIDAR: {
      ROS_WARN("[LidarFactory] LDLIDAR adapter not implemented yet");
      // return std::make_unique<LDLidarAdapter>();
      return nullptr;
    }
    
    default: {
      ROS_ERROR("[LidarFactory] Unknown lidar type");
      return nullptr;
    }
  }
}

std::unique_ptr<LidarInterface> LidarFactory::createLidar(const std::string& type_str) {
  LidarType type = stringToLidarType(type_str);
  if (type == LidarType::UNKNOWN) {
    ROS_ERROR("[LidarFactory] Unknown lidar type string: %s", type_str.c_str());
    return nullptr;
  }
  return createLidar(type);
}

bool LidarFactory::isSupported(LidarType type) {
  switch (type) {
    case LidarType::YDLIDAR:
      return true;
    case LidarType::RPLIDAR:
    case LidarType::LDLIDAR:
      return false; // 预留
    default:
      return false;
  }
}

std::vector<std::string> LidarFactory::getSupportedTypes() {
  std::vector<std::string> supported;
  if (isSupported(LidarType::YDLIDAR)) {
    supported.push_back("ydlidar");
  }
  return supported;
}
