#include "ros/ros.h"
// %EndTag(ROS_HEADER)%
// %Tag(MSG_HEADER)%
#include "std_msgs/String.h"
// %EndTag(MSG_HEADER)%

#include <sstream>
#include "monitor/monitor_helper.h"
#include <signal.h>

std::string g_node_name;

void signalHandler(int sig) {
  ROS_INFO("Received signal %d, unregistering node...", sig);
  if (!g_node_name.empty()) {
    monitor::MonitorHelper::AutoUnregister(g_node_name);
  }
  ros::shutdown();  
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "talker");
  ros::NodeHandle n;
  ros::Publisher chatter_pub = n.advertise<std_msgs::String>("chatter", 1000);
  ros::Rate loop_rate(10);
  int count = 0;
  g_node_name = ros::this_node::getName();

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  monitor::MonitorHelper::AutoRegister(g_node_name);
  
  while (ros::ok())
  {
    std_msgs::String msg;

    std::stringstream ss;
    ss << "hello world " << count;
    msg.data = ss.str();

    ROS_INFO("%s", msg.data.c_str());

    chatter_pub.publish(msg);

    ros::spinOnce();
    loop_rate.sleep();
    ++count;
  }

  return 0;
}
