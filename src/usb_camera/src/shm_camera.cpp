#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <std_msgs/Bool.h>
#include <cv_bridge/cv_bridge.h>
#include <iostream>
#include <shm_transport/shm_topic.hpp>


#include <sys/time.h>
#include "v4l2.h"


//#include <opencv2/opencv.hpp>
//using namespace cv;

using namespace std;

// shm_camera 是 usb_camera 的共享内存版本：
// - 采集层逻辑完全相同，仍然从 V4L2 读取 MJPEG；
// - 不同点在于发布阶段不直接把大消息走 ROS loopback socket，
//   而是先写入共享内存，再通过 ROS 发一个 handle。

int enable_camera=1;//默认使能摄像头

//订阅回调函数
void enable_camera_callback(const std_msgs::Bool::ConstPtr& msg)
{
  // 在回调函数中处理接收到的消息
  if (msg->data)
  {
    enable_camera = 1;
    ROS_INFO("enable_camera true");
  }
  else
  {
    enable_camera = 0;
    ROS_INFO("enable_camera false");
  }
}

int main(int argc, char** argv)
{

    unsigned int frame_cnt=0;
    double t=0,last_t=0;
    double fps;
    struct timeval tv;

    unsigned char *data_ptr;
    FrameBuf frame_buf;

    ros::init(argc, argv, "usb_camera");
    ros::NodeHandle nh("~");

    string pub_image_topic,dev_name,frame_id;
    int width,height,div;

    nh.param<string>("pub_image_topic", pub_image_topic, "/image_raw/compressed");

    nh.param<string>("dev_name", dev_name, "/dev/video0");

    nh.param<string>("frame_id", frame_id, "camera");

    nh.param<int>("width", width, 1280);

    nh.param<int>("height", height, 720);

    nh.param<int>("div", div, 1);

    //cv::Mat img(height, width, CV_8UC3);

    ros::Subscriber enable_camera_sub = nh.subscribe("/enable_camera", 10, enable_camera_callback);

    // 共享内存发布器：
    // pub_image_topic 仍然保持原来的逻辑 topic 名称，
    // 但实际大消息会先写到共享内存，再通过 ROS topic 发送 handle。
    shm_transport::Topic shm_topic(nh);
    // 这里给压缩 MJPEG 预留 10MB 共享内存，足够承载 1280x720 单帧 JPEG。
    shm_transport::Publisher shm_pub = shm_topic.advertise<sensor_msgs::CompressedImage>(pub_image_topic, 1, 10 * 1024 * 1024);

    sensor_msgs::CompressedImage msg;

    ros::Rate loop_rate(30);//30Hz

    int video_index = 0;
    std::string dev_names[4]={dev_name,"/dev/video0","/dev/video1","/dev/video2"};


    if(shm_pub.getNumSubscribers()==0)
    {
        ROS_INFO("camera subscribers num = 0, do not capture");
    }

    while(nh.ok())
    {
        // 无人消费时，不做无意义采集。
        if(shm_pub.getNumSubscribers()==0 || enable_camera==0)//检查订阅者数目，如果为0，则不采集图像
        {
            ros::spinOnce();
            usleep(100*1000);//100ms
            continue;
        }

        V4l2 v4l2;
        if(v4l2.init_video(dev_names[video_index].c_str(),width,height)<0)//打开视频设备
        {
            ROS_WARN("v4l2 init video error!");
            v4l2.release_video();//释放设备
            ros::spinOnce();
            sleep(1);

            // 当前设备失败时轮询尝试其它 video 设备。
            video_index++;
            if(video_index>=4)
                video_index = 0;

            continue;
        }

        while(nh.ok())
        {


            if(v4l2.get_data(&frame_buf)<0)//获取视频数据
            {
                ROS_WARN("v4l2 get data error!");
                v4l2.release_video();//释放设备
                ros::spinOnce();
                sleep(1);
                break;
            }

            if(enable_camera==0)//检查是否使能摄像头，如果为0则立即停止采集
            {
                ROS_INFO("enable_camera = 0, release video");
                v4l2.release_video();//释放设备
                break;
            }

            frame_cnt++;
            if(frame_cnt % 30 == 0)
            {
               gettimeofday(&tv, NULL);
               t = tv.tv_sec + tv.tv_usec/1000000.0;

               fps = 30.0/div/(t-last_t);
               ROS_DEBUG("mjpeg read fps %.2f subscribers num =%d",fps, shm_pub.getNumSubscribers());

               last_t = t;

                if(shm_pub.getNumSubscribers()==0)//检查订阅者数目，如果为0则停止采集
                {
                    ROS_INFO("camera subscribers num = 0, release video");
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

            // header 仍然由采集出口统一打时间戳，后续可以与普通 ROS 版测试结果做对比。
            msg.header.stamp = ros::Time::now();
            msg.header.frame_id = frame_id;
            msg.format = "jpeg";
            msg.data.assign(frame_buf.start, frame_buf.start+frame_buf.length);

            // 发布时大消息写入共享内存，ROS 网络里只发送 handle。
            shm_pub.publish(msg); //发布压缩图像

            ros::spinOnce();
            loop_rate.sleep();//如果比30fps还有空余时间会sleep
        }
    }
}
