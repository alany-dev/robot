#pragma once

#include "lidar_interface.h"
#include <ros/ros.h>

class LidarConfigLoader {
public:
  static LidarConfig loadFromRosParams(ros::NodeHandle& nh);
  static bool validateConfig(const LidarConfig& config);
  static void printConfig(const LidarConfig& config);
};
