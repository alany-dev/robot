#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <ros/ros.h>
#include <std_msgs/String.h>

#include "monitor/cpu_load_monitor.h"
#include "monitor/cpu_softirq_monitor.h"
#include "monitor/cpu_stat_monitor.h"
#include "monitor/mem_monitor.h"
#include "monitor/monitor_inter.h"
#include "monitor/net_monitor.h"
#include "monitor/MonitorInfo.h"
#include "monitor/RegisterNode.h"
#include "monitor/UnregisterNode.h"
#include "monitor/process_monitor.h"

class MonitorNode {
 public:
  MonitorNode() : nh_("~"), nh_global_(), process_monitor_(new monitor::ProcessMonitor()) {
    register_service_ = nh_global_.advertiseService("/monitor/register_node", 
                                             &MonitorNode::RegisterNodeCallback, this);
    unregister_service_ = nh_global_.advertiseService("/monitor/unregister_node",
                                               &MonitorNode::UnregisterNodeCallback, this);
    
    std::string monitor_topic;
    nh_.param<std::string>("monitor_topic", monitor_topic, "/monitor_info");
    monitor_pub_ = nh_.advertise<monitor::MonitorInfo>(monitor_topic, 10);
    ROS_INFO("Publishing monitor data to topic: %s", monitor_topic.c_str());
    
    nh_.param<std::string>("bag_file_path", bag_file_path_, "monitor_data.bag");
    
    try {
      bag_.open(bag_file_path_, rosbag::bagmode::Write);
      ROS_INFO("Opened bag file: %s", bag_file_path_.c_str());
    } catch (rosbag::BagException& e) {
      ROS_ERROR("Failed to open bag file: %s", e.what());
      ros::shutdown();
      return;
    }
    
    //C++多态
    runners_.emplace_back(new monitor::CpuSoftIrqMonitor());
    runners_.emplace_back(new monitor::CpuLoadMonitor());
    runners_.emplace_back(new monitor::CpuStatMonitor());
    runners_.emplace_back(new monitor::MemMonitor());
    runners_.emplace_back(new monitor::NetMonitor());
    
    
    char* name = getenv("USER");
    if (name == nullptr) {
      name = const_cast<char*>("unknown");
    }
    user_name_ = std::string(name);
    
    ROS_INFO("Monitor node started");
    ROS_INFO("Services available:");
    ROS_INFO("  - /monitor/register_node");
    ROS_INFO("  - /monitor/unregister_node");
  }
  
  ~MonitorNode() {
    if (bag_.isOpen()) {
      bag_.close();
      ROS_INFO("Bag file closed");
    }
  }
  
  void Run() {
    ros::Rate rate(1.0 / 3.0); 
    
    while (ros::ok()) {
      process_monitor_->UpdateAll();
      
      monitor::MonitorInfo monitor_info;
      monitor_info.header.stamp = ros::Time::now();
      monitor_info.header.frame_id = "monitor";
      monitor_info.name = user_name_;
      
      monitor_info.cpu_stat.clear();
      monitor_info.cpu_softirq.clear();
      monitor_info.net_info.clear();
      monitor_info.node_monitors.clear();
      
      for (auto& runner : runners_) {
        runner->UpdateOnce(&monitor_info);
      }
      
      std::vector<monitor::NodeMonitorInfo> node_infos;
      process_monitor_->GetAllNodeInfo(node_infos);
      
      for (const auto& node_info : node_infos) {
        monitor_info.node_monitors.push_back(node_info);
      }
      
      if (monitor_pub_.getNumSubscribers() > 0) {
        monitor_pub_.publish(monitor_info);
      }
      
      try {
        bag_.write("/monitor_info", ros::Time::now(), monitor_info);
      } catch (rosbag::BagException& e) {
        ROS_ERROR("Failed to write to bag file: %s", e.what());
      }
      
      rate.sleep();
      ros::spinOnce();
    }
  }
  
 private:
  bool RegisterNodeCallback(monitor::RegisterNode::Request& req,
                            monitor::RegisterNode::Response& res) {
    ROS_INFO("Register node request: name=%s, process=%s, pid=%d",
             req.node_name.c_str(), req.process_name.c_str(), req.pid);
    
    bool success = process_monitor_->RegisterNode(req.node_name, 
                                                   req.process_name,
                                                   req.pid);
    
    if (success) {
      res.success = true;
      res.message = "Node registered successfully";
      ROS_INFO("Node '%s' registered successfully", req.node_name.c_str());
    } else {
      res.success = false;
      if (process_monitor_->IsRegistered(req.node_name)) {
        res.message = "Node already registered";
      } else {
        res.message = "Failed to find process. Make sure the process is running.";
      }
      ROS_WARN("Failed to register node '%s': %s", req.node_name.c_str(), res.message.c_str());
    }
    
    return true;
  }
  
  bool UnregisterNodeCallback(monitor::UnregisterNode::Request& req,
                              monitor::UnregisterNode::Response& res) {
    ROS_INFO("Unregister node request: name=%s", req.node_name.c_str());
    
    bool success = process_monitor_->UnregisterNode(req.node_name);
    
    if (success) {
      res.success = true;
      res.message = "Node unregistered successfully";
      ROS_INFO("Node '%s' unregistered successfully", req.node_name.c_str());
    } else {
      res.success = false;
      res.message = "Node not found";
      ROS_WARN("Failed to unregister node '%s': Node not found", req.node_name.c_str());
    }
    
    return true;
  }
  
  ros::NodeHandle nh_;  
  ros::NodeHandle nh_global_;  
  rosbag::Bag bag_;
  std::string bag_file_path_;
  std::string user_name_;
  
  ros::ServiceServer register_service_;
  ros::ServiceServer unregister_service_;
  ros::Publisher monitor_pub_; 
  
  std::vector<std::shared_ptr<monitor::MonitorInter>> runners_;

  std::unique_ptr<monitor::ProcessMonitor> process_monitor_;  
};

int main(int argc, char** argv) {
  ros::init(argc, argv, "monitor_node");
  
  MonitorNode monitor_node;
  monitor_node.Run();
  
  return 0;
}
