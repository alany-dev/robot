#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <base_control/BaseConfig.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <dynamic_reconfigure/server.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <ros/spinner.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/Int32.h>
#include <std_msgs/String.h>
#include <tf/tf.h>
#include <tf/transform_broadcaster.h>

#include "pid.h"
#include "uart.h"

using namespace std;

#pragma pack(1)

typedef struct
{
  unsigned char head1;        //数据头1 'D'
  unsigned char head2;        //数据头2 'A'
  unsigned char struct_size;  //结构体长度

  short encoder1;  //编码器当前值1
  short encoder2;  // 编码器当前值2

  unsigned char end1;  //数据尾1 'T'
  unsigned char end2;  //数据尾2 'A'
  unsigned char end3;  //数据尾3 '\r' 0x0d
  unsigned char end4;  //数据尾4 '\n' 0x0a
}McuData;


typedef struct
{
  unsigned char head1;        //数据头1 'D'
  unsigned char head2;        //数据头2 'A'
  unsigned char struct_size;  //结构体长度

  short pwm1;          //油门PWM1
  short pwm2;          //油门PWM2
  unsigned char end1;  //数据尾1 'T'
  unsigned char end2;  //数据尾2 'A'
  unsigned char end3;  //数据尾3 '\r' 0x0d
  unsigned char end4;  //数据尾4 '\n' 0x0a
}CmdData;

#pragma pack()

typedef struct
{
    int wheel_circumference_mm;
    int pulses_per_wheel_turn_around;
    int pulses_per_robot_turn_around;

    int max_pwm;

    float pid_p;
    float pid_i;
    float pid_d;
}SettingData;


class BaseControl
{
public:
    BaseControl();
    ~BaseControl();
    void run();

private:
    void dynamic_reconfigure_callback(base_control::BaseConfig &config, uint32_t level);
    void cmd_vel_callback(const geometry_msgs::Twist::ConstPtr& msg);
    void enable_lidar_callback(const std_msgs::Bool::ConstPtr& msg);
    void reset_callback(const std_msgs::Bool::ConstPtr& msg);
    void control_robot(int target1,int target2);
    void pub_tf_and_odom(ros::Time ros_time_now,double delta_m_s,double delta_rad_s);
    void pub_plot(vector<float> array);


    CmdData cmd_data = {'D','A',sizeof(CmdData),0,0,'T','A','\r','\n'};

    double target_m_s=0;//目标线速度m/s
    double target_rad_s=0;//目标角速度rad/s

    ros::NodeHandle nh;

    string odom_frame_id, base_frame_id;

    ros::Subscriber command_sub;
    ros::Subscriber reset_sub;
    ros::Publisher odom_pub;
    ros::Publisher plot_pub;

    Uart uart;

    McuData mcu_data;
    unsigned int cnt = 0;

    SettingData setting_data;

    double current_time = 0, previous_time = 0;
    double dt = 0, pose_yaw = 0;
    double pose_x = 0, pose_y = 0;

    tf::TransformBroadcaster odom_broadcaster;
    nav_msgs::Odometry odom_msg;
    geometry_msgs::TransformStamped odom_trans;
    geometry_msgs::Quaternion odom_quat;

    string recv_str;

    long long all_encoder1 = 0;
    long long all_encoder2 = 0;

    dynamic_reconfigure::Server<base_control::BaseConfig> server;
    dynamic_reconfigure::Server<base_control::BaseConfig>::CallbackType
        dynamic_config;

    PidController left_pid;
    PidController right_pid;

    int last_target1 = 0;
    int last_target2 = 0;

    double pluses_m = 0;
    double wheel_distance_m = 0;
};


