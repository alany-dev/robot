#include <rclcpp/rclcpp.hpp>
#include <rclcpp/qos.hpp>
#include <std_msgs/msg/bool.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <iostream>

#include <sys/time.h>
#include "v4l2.h"



//#include <opencv2/opencv.hpp>
//using namespace cv;

using namespace std;

int main(int argc, char** argv)
{
    // 摄像头使能标志（通过订阅 /enable_camera 话题控制）
    int enable_camera = 1;  // 默认使能摄像头

    unsigned int frame_cnt=0;
    double t=0,last_t=0;
    double fps;
    struct timeval tv;

    FrameBuf frame_buf;
    
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("usb_camera");

    string pub_image_topic,dev_name,frame_id;
    int width,height,div;

    node->declare_parameter("pub_image_topic", "/image_raw/compressed");
    node->declare_parameter("dev_name", "/dev/video0");
    node->declare_parameter("frame_id", "camera_link");
    node->declare_parameter("width", 1280);
    node->declare_parameter("height", 720);
    node->declare_parameter("div", 1);

    node->get_parameter("pub_image_topic", pub_image_topic);
    node->get_parameter("dev_name", dev_name);
    node->get_parameter("frame_id", frame_id);
    node->get_parameter("width", width);
    node->get_parameter("height", height);
    node->get_parameter("div", div);

    //cv::Mat img(height, width, CV_8UC3);

    auto enable_camera_sub = node->create_subscription<std_msgs::msg::Bool>(
        "/enable_camera", 10, 
        [&enable_camera, node](const std_msgs::msg::Bool::SharedPtr msg) {
            if (msg->data)
            {
                enable_camera = 1;
                RCLCPP_INFO(node->get_logger(), "enable_camera true");
            }
            else
            {
                enable_camera = 0;
                RCLCPP_INFO(node->get_logger(), "enable_camera false");
            }
        });

    // 优化 QoS 设置：历史深度=1，只保留最新消息，避免旧数据堆积
    rclcpp::QoS qos_profile(1);
    qos_profile.best_effort(); 
    qos_profile.durability_volatile();
    auto pub = node->create_publisher<sensor_msgs::msg::CompressedImage>(pub_image_topic, qos_profile);
    
    sensor_msgs::msg::CompressedImage msg;

    rclcpp::Rate loop_rate(30);//30Hz

    int video_index = 0;
    std::string dev_names[4]={dev_name,"/dev/video0","/dev/video1","/dev/video2"};


    if(pub->get_subscription_count()==0)
    {
        RCLCPP_INFO(node->get_logger(), "camera subscribers num = 0, do not capture");
    }

    while(rclcpp::ok())
    {
        if(pub->get_subscription_count()==0 || enable_camera==0)//检查订阅者数目，如果为0，则不采集图像
        {
            rclcpp::spin_some(node);
            usleep(100*1000);//100ms
            continue;
        }

        V4l2 v4l2;
        if(v4l2.init_video(dev_names[video_index].c_str(),width,height)<0)//打开视频设备
        {
            RCLCPP_WARN(node->get_logger(), "v4l2 init video error!");
            v4l2.release_video();//释放设备
            rclcpp::spin_some(node);
            sleep(1);

            //切换视频设备尝试
            video_index++;
            if(video_index>=4)
                video_index = 0;
            
            continue;
        }

        while(rclcpp::ok())
        {


            if(v4l2.get_data(&frame_buf)<0)//获取视频数据
            {
                RCLCPP_WARN(node->get_logger(), "v4l2 get data error!");
                v4l2.release_video();//释放设备
                rclcpp::spin_some(node);
                sleep(1);
                break;
            }
            
            if(enable_camera==0)//检查是否使能摄像头，如果为0则立即停止采集
            {
                RCLCPP_INFO(node->get_logger(), "enable_camera = 0, release video");
                v4l2.release_video();//释放设备
                break;
            }

            frame_cnt++;
            if(frame_cnt % 30 == 0)
            {
               gettimeofday(&tv, NULL);
               t = tv.tv_sec + tv.tv_usec/1000000.0;

               fps = 30.0/div/(t-last_t);
               RCLCPP_DEBUG(node->get_logger(), "mjpeg read fps %.2f subscribers num =%zu", fps, pub->get_subscription_count());

               last_t = t;

                if(pub->get_subscription_count()==0)//检查订阅者数目，如果为0则停止采集
                {
                    RCLCPP_INFO(node->get_logger(), "camera subscribers num = 0, release video");
                    v4l2.release_video();//释放设备
                    break;
                }
            }
            
            if(frame_cnt % div != 0)
                continue;
            
            //printf("frame_buf 0x%X %d\n",frame_buf.start,frame_buf.length);

            //解码显示
            //img = cv::imdecode(jpeg, IMREAD_COLOR);
            //imshow("img", img);
            //waitKey(1);

            msg.header.stamp = node->now();
            msg.header.frame_id = frame_id; 
            msg.format = "jpeg";
            msg.data.assign(frame_buf.start, frame_buf.start+frame_buf.length);

            pub->publish(msg); //发布压缩图像

            rclcpp::spin_some(node);
            loop_rate.sleep();//如果比30fps还有空余时间会sleep
        }
    }
    
    rclcpp::shutdown();
    return 0;
}
