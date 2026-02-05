#include "monitor/monitor_helper.h"
#include "monitor/RegisterNode.h"
#include "monitor/UnregisterNode.h"
#include <ros/ros.h>
#include <fstream>
#include <sstream>

namespace monitor {

bool MonitorHelper::AutoRegister(const std::string& node_name,
                                 const std::string& service_name) {
  ros::NodeHandle nh;
  
  if (!ros::service::waitForService(service_name, ros::Duration(5.0))) {
    ROS_WARN("Monitor service '%s' is not available after 5 seconds. Make sure monitor_node is running.", service_name.c_str());
    return false;
  }
  
  ros::ServiceClient client = nh.serviceClient<monitor::RegisterNode>(service_name);
  
  int32_t pid = GetCurrentPid();
  std::string process_name = GetCurrentProcessName();
  
  if (process_name.empty()) {
    process_name = node_name;
  }
  
  monitor::RegisterNode srv;
  srv.request.node_name = node_name;
  srv.request.process_name = process_name;
  srv.request.pid = pid; 
  
  if (client.call(srv)) {
    if (srv.response.success) {
      ROS_INFO("Node '%s' (PID: %d) registered to monitor successfully", 
               node_name.c_str(), pid);
      return true;
    } else {
      ROS_WARN("Failed to register node '%s': %s", 
               node_name.c_str(), srv.response.message.c_str());
      return false;
    }
  } else {
    ROS_ERROR("Failed to call monitor register service");
    return false;
  }
}

bool MonitorHelper::AutoUnregister(const std::string& node_name,
                                   const std::string& service_name) {
  ros::NodeHandle nh;
  if (!ros::service::waitForService(service_name, ros::Duration(5.0))) {
    ROS_ERROR("Monitor unregister service '%s' is not available. Node may not be unregistered.", service_name.c_str());
    return false;
  }

  ros::ServiceClient client = nh.serviceClient<monitor::UnregisterNode>(service_name);
  monitor::UnregisterNode srv;
  srv.request.node_name = node_name;
  if (client.call(srv)) {
    if (srv.response.success) {
      ROS_INFO("Node '%s' unregistered from monitor successfully", node_name.c_str());
      return true;
    } else {
      ROS_WARN("Failed to unregister node '%s': %s", 
               node_name.c_str(), srv.response.message.c_str());
      return false;
    }
  } else {
    ROS_ERROR("Failed to call monitor unregister service");
    return false;
  }
}

}  // namespace monitor
