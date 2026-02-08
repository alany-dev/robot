// Copyright 2016 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <memory>
#include <thread>

#include "rclcpp/rclcpp.hpp"
// string.msg-ros 序列化编译器，生成的头文件
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  
  auto node = rclcpp::Node::make_shared("minimal_publisher");

  rclcpp::QoS qos_profile(1);
  // 尽量而为
  qos_profile.best_effort(); 
  
  auto publisher = node->create_publisher<std_msgs::msg::String>("topic", qos_profile);
  
  size_t count = 0;
  
  rclcpp::Rate rate(2); 
  
  while (rclcpp::ok()) {
    auto message = std_msgs::msg::String();
    message.data = "Hello, world! " + std::to_string(count++);
    RCLCPP_INFO(node->get_logger(), "Publishing: '%s'", message.data.c_str());
    publisher->publish(message);
    
    rclcpp::spin_some(node);
    
    rate.sleep();
  }
  
  rclcpp::shutdown();
  return 0;
}

