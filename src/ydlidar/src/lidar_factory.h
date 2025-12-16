#pragma once

#include "lidar_interface.h"
#include <memory>
#include <string>

class LidarFactory {
public:
  static std::unique_ptr<LidarInterface> createLidar(LidarType type);
  static std::unique_ptr<LidarInterface> createLidar(const std::string& type_str);
  static bool isSupported(LidarType type);
  static std::vector<std::string> getSupportedTypes();
};
