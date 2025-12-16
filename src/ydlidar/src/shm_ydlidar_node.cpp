  #pragma once
  #include <ros/ros.h>
  #include "sensor_msgs/LaserScan.h"
  #include "std_srvs/Empty.h"
  #include "src/CYdLidar.h"
  #include "ydlidar_config.h"
  #include <limits>       // std::numeric_limits
  #include <shm_transport/shm_topic.hpp>


  #define SDKROSVerision "1.0.0"

  CYdLidar laser;

  bool stop_scan(std_srvs::Empty::Request &req,
                                std_srvs::Empty::Response &res)
  {
    ROS_INFO("Stop scan");
    return laser.turnOff();
  }

  bool start_scan(std_srvs::Empty::Request &req,
                                std_srvs::Empty::Response &res)
  {
    ROS_INFO("Start scan");
    return laser.turnOn();
  }


  int main(int argc, char **argv) {
    ros::init(argc, argv, "ydlidar_node");
    ROS_INFO("YDLIDAR ROS Driver Version: %s", SDKROSVerision);
    ros::NodeHandle nh;
    
    // Use shared memory transport for better performance
    shm_transport::Topic shm_topic(nh);
    // Allocate shared memory for LaserScan messages
    // 512KB shared memory: optimized for 1.5KB messages (~340 messages buffer)
    // Reduces memory footprint while maintaining low latency
    // If messages grow larger, increase to 1MB or 2MB
    shm_transport::Publisher shm_pub = shm_topic.advertise<sensor_msgs::LaserScan>("/scan", 1, 512 * 1024);

    // Parameters from launch file (hardcoded, no ROS param server)
    // String parameters
    std::string str_optvalue = "/dev/ttyS9";  // port from launch file
    laser.setlidaropt(LidarPropSerialPort, str_optvalue.c_str(), str_optvalue.size());
    
    str_optvalue = "";  // ignore_array from launch file
    laser.setlidaropt(LidarPropIgnoreArray, str_optvalue.c_str(), str_optvalue.size());
    
    std::string frame_id = "laser_link";  // frame_id from launch file (default value)
    printf("frame_id = %s\n", frame_id.c_str());

    // Int parameters from launch file
    int optval = 115200;  // baudrate
    laser.setlidaropt(LidarPropSerialBaudrate, &optval, sizeof(int));
    
    optval = 1;  // lidar_type: TYPE_TRIANGLE
    laser.setlidaropt(LidarPropLidarType, &optval, sizeof(int));
    
    optval = 0;  // device_type: YDLIDAR_TYPE_SERIAL
    laser.setlidaropt(LidarPropDeviceType, &optval, sizeof(int));
    
    optval = 3;  // sample_rate
    laser.setlidaropt(LidarPropSampleRate, &optval, sizeof(int));
    
    optval = 4;  // abnormal_check_count
    laser.setlidaropt(LidarPropAbnormalCheckCount, &optval, sizeof(int));

    // Bool parameters from launch file
    bool b_optvalue = true;  // resolution_fixed
    laser.setlidaropt(LidarPropFixedResolution, &b_optvalue, sizeof(bool));
    
    b_optvalue = true;  // auto_reconnect
    laser.setlidaropt(LidarPropAutoReconnect, &b_optvalue, sizeof(bool));
    
    b_optvalue = true;  // reversion
    laser.setlidaropt(LidarPropReversion, &b_optvalue, sizeof(bool));
    
    b_optvalue = true;  // inverted
    laser.setlidaropt(LidarPropInverted, &b_optvalue, sizeof(bool));
    
    b_optvalue = true;  // isSingleChannel
    laser.setlidaropt(LidarPropSingleChannel, &b_optvalue, sizeof(bool));
    
    b_optvalue = false;  // intensity
    laser.setlidaropt(LidarPropIntenstiy, &b_optvalue, sizeof(bool));
    
    b_optvalue = true;  // support_motor_dtr
    laser.setlidaropt(LidarPropSupportMotorDtrCtrl, &b_optvalue, sizeof(bool));

    // Float parameters from launch file
    float f_optvalue = 180.0f;  // angle_max
    laser.setlidaropt(LidarPropMaxAngle, &f_optvalue, sizeof(float));
    
    f_optvalue = -180.0f;  // angle_min
    laser.setlidaropt(LidarPropMinAngle, &f_optvalue, sizeof(float));
    
    f_optvalue = 10.0f;  // range_max
    laser.setlidaropt(LidarPropMaxRange, &f_optvalue, sizeof(float));
    
    f_optvalue = 0.1f;  // range_min
    laser.setlidaropt(LidarPropMinRange, &f_optvalue, sizeof(float));
    
    f_optvalue = 10.0f;  // frequency
    laser.setlidaropt(LidarPropScanFrequency, &f_optvalue, sizeof(float));

    bool invalid_range_is_inf = false;  // invalid_range_is_inf from launch file

    ros::ServiceServer stop_scan_service = nh.advertiseService("stop_scan", stop_scan);
    ros::ServiceServer start_scan_service = nh.advertiseService("start_scan", start_scan);

    ros::Rate r(30);

    // initialize SDK and LiDAR
    bool ret = laser.initialize();
    if (ret) {//success
      //Start the device scanning routine which runs on a separate thread and enable motor.

      ret = false;
      while(ret==false && ros::ok()) //ret如果等于false,则反复尝试打开雷达
      {
        ret = laser.turnOn();
        //ROS_INFO("Lidar trun on failed"); //如果雷达不接电源就会一直在这个循环里循环
        ros::spinOnce();//处理ctrl c中断
        r.sleep();
      }

    } else {
      ROS_WARN("initialize error: %s\n", laser.DescribeError());

      //等待ctrl c按下退出
      while(ros::ok())
      {
        ros::spinOnce();//处理ctrl c中断
        r.sleep();
      }
    }

    
    while (ret && ros::ok()) {
      LaserScan scan;
      if (laser.doProcessSimple(scan)) {
        sensor_msgs::LaserScan scan_msg;

        ros::Time start_scan_time;
        start_scan_time.sec = scan.stamp/1000000000ul;
        start_scan_time.nsec = scan.stamp%1000000000ul;
        scan_msg.header.stamp = start_scan_time;
        scan_msg.header.frame_id = frame_id;

        scan_msg.angle_min =(scan.config.min_angle);
        scan_msg.angle_max = (scan.config.max_angle);
        scan_msg.angle_increment = (scan.config.angle_increment);
        scan_msg.scan_time = scan.config.scan_time;
        scan_msg.time_increment = scan.config.time_increment;
        scan_msg.range_min = (scan.config.min_range);
        scan_msg.range_max = (scan.config.max_range);


        int size = (scan.config.max_angle - scan.config.min_angle)/ scan.config.angle_increment + 1;
        scan_msg.ranges.resize(size, invalid_range_is_inf ? std::numeric_limits<float>::infinity() : 0.0);
        scan_msg.intensities.resize(size);
        for(size_t i=0; i < scan.points.size(); i++) {
          int index = std::ceil((scan.points[i].angle - scan.config.min_angle)/scan.config.angle_increment);
          if(index >=0 && index < size) {
            if(scan.points[i].range >= scan.config.min_range) {
              scan_msg.ranges[index] = scan.points[i].range;
              scan_msg.intensities[index] = scan.points[i].intensity;
            }
          }

        }
        shm_pub.publish(scan_msg);


      } else {
        //ROS_INFO("Failed to get Lidar Data");//ERROR->INFO  edit
        ;
      }

      ros::spinOnce();//处理ctrl c中断
      r.sleep();
    }

    laser.turnOff();
    ROS_INFO("[YDLIDAR INFO] Now YDLIDAR is stopping .......");
    laser.disconnecting();
    return 0;
  }
