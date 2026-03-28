#include <ros/ros.h>
#include <sensor_msgs/CompressedImage.h> //ROS 官方自带的消息类型，用来发「压缩图片」（JPEG / MJPEG）
#include <std_msgs/Bool.h>
#include <sys/time.h>
#include <string>
#include "v4l2.h"

using namespace std;

// 全局状态：运行时采集开关
int enable_camera = 1;

// 处理外部下发的采集使能开关
void enable_camera_callback(const std_msgs::Bool::ConstPtr& msg) {
    enable_camera = msg->data ? 1 : 0;
    ROS_INFO("enable_camera: %s", enable_camera ? "true" : "false");
}

int main(int argc, char** argv) {
    ros::init(argc, argv, "usb_camera");
    ros::NodeHandle nh("~");

    // ======================
    // 1. 读取 ROS 参数
    // ======================
    string pub_image_topic, dev_name, frame_id;
    int width, height, div;

    nh.param<string>("pub_image_topic", pub_image_topic, "/jpeg_image");
    nh.param<string>("dev_name", dev_name, "/dev/video0");
    nh.param<string>("frame_id", frame_id, "camera");
    nh.param<int>("width", width, 1280);
    nh.param<int>("height", height, 720);
    nh.param<int>("div", div, 1);

    // ======================
    // 2. 初始化 ROS 接口
    // ======================
    ros::Subscriber enable_sub = nh.subscribe("/enable_camera", 10, enable_camera_callback);
    ros::Publisher pub = nh.advertise<sensor_msgs::CompressedImage>(pub_image_topic, 1);
    ros::Rate loop_rate(30);

    // ======================
    // 3. 摄像头初始化（带设备轮询）
    // ======================
    V4l2 v4l2;
    FrameBuf frame_buf;
    sensor_msgs::CompressedImage msg;
    unsigned int frame_cnt = 0;
    double last_t = 0;
    int video_index = 0;
    std::string dev_names[4] = {dev_name, "/dev/video0", "/dev/video1", "/dev/video2"};

    // 循环尝试初始化摄像头，直到成功
    while (nh.ok() && v4l2.init_video(dev_names[video_index].c_str(), width, height) < 0) {
        ROS_WARN("Failed to init %s, try next...", dev_names[video_index].c_str());
        video_index = (video_index + 1) % 4;
        ros::spinOnce();
        sleep(1);
    }
    ROS_INFO("Camera init success: %s", dev_names[video_index].c_str());

    // ======================
    // 4. 主采集循环（简化版）
    // ======================
    while (nh.ok()) {
        bool need_publish = (pub.getNumSubscribers() > 0) && (enable_camera == 1);

        // 从驱动取帧（无论是否发布都取，保持摄像头流稳定）
        if (v4l2.get_data(&frame_buf) < 0) {
            ROS_WARN("Get data failed, reinit camera...");
            v4l2.release_video();
            
            // 重新初始化摄像头
            while (nh.ok() && v4l2.init_video(dev_names[video_index].c_str(), width, height) < 0) {
                video_index = (video_index + 1) % 4;
                ros::spinOnce();
                sleep(1);
            }
            continue;
        }

        // 仅在需要时发布
        if (need_publish && (frame_cnt % div == 0)) {
            msg.header.stamp = ros::Time::now();
            msg.header.frame_id = frame_id;
            msg.format = "jpeg";
            msg.data.assign(frame_buf.start, frame_buf.start + frame_buf.length);
            pub.publish(msg);
        }

        // FPS 统计（每30帧一次）
        frame_cnt++;
        if (frame_cnt % 30 == 0) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            double t = tv.tv_sec + tv.tv_usec / 1000000.0;
            if (last_t > 0) {
                double fps = 30.0 / div / (t - last_t);
                ROS_DEBUG("FPS: %.2f, Subscribers: %d", fps, pub.getNumSubscribers());
            }
            last_t = t;
        }

        ros::spinOnce();
        loop_rate.sleep();
    }

    // 退出时释放资源
    v4l2.release_video();
    return 0;
}