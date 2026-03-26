#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <std_msgs/Bool.h>
#include <cv_bridge/cv_bridge.h>
#include <iostream>

#include <sys/time.h>
#include "v4l2.h"


//#include <opencv2/opencv.hpp>
//using namespace cv;

using namespace std;

// 低时延视觉链路的采集入口。
//
// 这个节点的设计目标不是在这里做任何图像处理，而是尽快把摄像头吐出的 MJPEG
// 压缩帧送进 ROS 图像链路。这样后面的 img_decode 可以在更合适的节点上统一做
// 硬解码、缩放和时间戳传递。
//
// 控制逻辑：
// 1. 只有当下游真的有订阅者时才打开摄像头，避免无意义采集；
// 2. 支持通过 /enable_camera 在运行中临时停采；
// 3. 摄像头异常时自动释放并尝试切换到下一个 video 设备节点。

// enable_camera:
// 运行时采集开关。默认打开，收到 /enable_camera=false 后立即停止继续采集。
int enable_camera=1;//默认使能摄像头

/**
 * 处理外部下发的采集使能开关。
 *
 * @param msg std_msgs/Bool:
 *            - true:  允许继续采集并向下游发布压缩图像；
 *            - false: 立即停止采集，释放摄像头。
 */
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
    // frame_cnt:
    // 采集到的总帧数，用于分频发布和粗略统计 FPS。
    unsigned int frame_cnt=0;

    // t / last_t:
    // 每 30 帧更新一次的时间戳，用于估算当前采集频率。
    double t=0,last_t=0;
    double fps;
    struct timeval tv;

    // data_ptr 当前没有使用，但保留在这里方便以后扩展“直接读取帧地址”式调试。
    unsigned char *data_ptr;
    FrameBuf frame_buf;
    
    ros::init(argc, argv, "usb_camera");
    ros::NodeHandle nh("~");

    string pub_image_topic,dev_name,frame_id;
    int width,height,div;

    // pub_image_topic:
    // 采集节点发布的压缩图像话题，默认 /image_raw/compressed。
    nh.param<string>("pub_image_topic", pub_image_topic, "/jpeg_image");

    // dev_name:
    // 首选摄像头设备节点路径，例如 /dev/video0。
    nh.param<string>("dev_name", dev_name, "/dev/video1");

    // frame_id:
    // 发布到 ROS 消息头里的坐标系名称，供后续几何和可视化使用。
    nh.param<string>("frame_id", frame_id, "camera");

    // width / height:
    // 希望摄像头在驱动层输出的分辨率。
    nh.param<int>("width", width, 1280);
    nh.param<int>("height", height, 720);
    
    // div:
    // 采集分频。例如 div=2 表示每采 2 帧只发布 1 帧，常用于快速限流。
    nh.param<int>("div", div, 1);

    //cv::Mat img(height, width, CV_8UC3);

    // 允许上层业务在运行期关闭摄像头，避免无人值守时继续占用 USB / CPU。
    ros::Subscriber enable_camera_sub = nh.subscribe("/enable_camera", 10, enable_camera_callback);

    // 发布压缩图像时队列深度设为 1：
    // 这样如果下游消费慢，不会在采集节点堆积大量过期帧。
    ros::Publisher pub = nh.advertise<sensor_msgs::CompressedImage>(pub_image_topic, 1);
    
    sensor_msgs::CompressedImage msg;

    // 采集循环按 30Hz 节奏推进，和摄像头目标帧率保持一致。
    ros::Rate loop_rate(30);//30Hz

    // 如果首选设备打不开，则尝试 video0 / video1 / video2，提升部署容错性。
    int video_index = 0;
    std::string dev_names[4]={dev_name,"/dev/video0","/dev/video1","/dev/video2"};


    if(pub.getNumSubscribers()==0)
    {
        ROS_INFO("camera subscribers num = 0, do not capture");
    }

    while(nh.ok())
    {
        // 没有人订阅，或者业务上明确关闭了摄像头，就不要继续打开设备采集。
        if(pub.getNumSubscribers()==0 || enable_camera==0)//检查订阅者数目，如果为0，则不采集图像
        {
            ros::spinOnce();
            usleep(100*1000);//100ms
            continue;
        }

        V4l2 v4l2;
        // 只有确认需要采集时才初始化摄像头，避免节点启动就长期占着设备。
        if(v4l2.init_video(dev_names[video_index].c_str(),width,height)<0)//打开视频设备
        {
            ROS_WARN("v4l2 init video error!");
            v4l2.release_video();//释放设备
            ros::spinOnce();
            sleep(1);

            // 当前设备失败后轮询尝试下一个设备节点。
            video_index++;
            if(video_index>=4)
                video_index = 0;
            
            continue;
        }

        while(nh.ok())
        {
            // 从 V4L2 驱动拿一帧 MJPEG 压缩数据。
            if(v4l2.get_data(&frame_buf)<0)//获取视频数据
            {
                ROS_WARN("v4l2 get data error!");
                v4l2.release_video();//释放设备
                ros::spinOnce();
                sleep(1);
                break;
            }
            
            // 一旦外部关闭采集，就立刻释放摄像头，避免后面继续阻塞在采集循环。
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

               // 这里的 fps 是“采样频率 / 分频系数”后的发布频率估计值。
               fps = 30.0/div/(t-last_t);
               ROS_DEBUG("mjpeg read fps %.2f subscribers num =%d",fps, pub.getNumSubscribers());

               last_t = t;

                // 采集中如果发现下游全部退订，就立即停止，避免相机继续跑空转。
                if(pub.getNumSubscribers()==0)//检查订阅者数目，如果为0则停止采集
                {
                    ROS_INFO("camera subscribers num = 0, release video");
                    v4l2.release_video();//释放设备
                    break;
                }
            }
            
            // 分频限流：只把需要的帧送进后续链路，降低下游解码和推理压力。
            if(frame_cnt % div != 0)
                continue;
            
            //printf("frame_buf 0x%X %d\n",frame_buf.start,frame_buf.length);

            //解码显示
            //img = cv::imdecode(jpeg, IMREAD_COLOR);
            //imshow("img", img);
            //waitKey(1);

            // 在采集出口打上时间戳，作为后续整条链路计算通信时延的起点。
            msg.header.stamp = ros::Time::now();
            msg.header.frame_id = frame_id; 
            msg.format = "jpeg";  // 显式告诉下游这是 MJPEG/JPEG 压缩图像。

            // 这里会把当前 V4L2 mmap 缓冲区里的数据拷贝到 ROS 消息中。
            // 虽然不是零拷贝，但由于数据已经是压缩态，拷贝成本相对可控。
            msg.data.assign(frame_buf.start, frame_buf.start+frame_buf.length);

            pub.publish(msg); //发布压缩图像

            ros::spinOnce();
            // 如果当前循环比 30fps 更快，这里会主动 sleep，避免用户态无意义忙跑。
            loop_rate.sleep();//如果比30fps还有空余时间会sleep
        }
    }
}
