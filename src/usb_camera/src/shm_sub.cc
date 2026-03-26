#include <ros/ros.h>
#include <sensor_msgs/CompressedImage.h>
#include <sys/time.h>
#include <shm_transport/shm_topic.hpp>

using namespace std;

// 共享内存压缩图像订阅测试：
// 用于和 test_sub.cc 对比“压缩图像这一级如果改走共享内存，性能是否更好”。

// 统计变量
unsigned int recv_frames = 0;
double start_time = 0.0;
double max_fps = 0.0, min_fps = 1000.0;
double last_recv_time = 0.0;

/**
 * 统计共享内存订阅压缩图像的接收 FPS 和时延。
 *
 * @param msg 从共享内存反序列化后的 CompressedImage。
 */
void imageCallback(const sensor_msgs::CompressedImage::ConstPtr &msg)
{
    double current_time = ros::Time::now().toSec();
    if (recv_frames == 0)
    {
        start_time = current_time;
        last_recv_time = current_time;
    }

    // 计算实时帧率
    double frame_interval = current_time - last_recv_time;
    double current_fps = 1.0 / frame_interval;
    max_fps = max(max_fps, current_fps);
    min_fps = min(min_fps, current_fps);

    // 口径仍然保持一致：接收时间 - 发布时间。
    double msg_delay = (current_time - msg->header.stamp.toSec()) * 1000; // 毫秒

    recv_frames++;
    last_recv_time = current_time;

    // 每30帧输出统计
    if (recv_frames % 30 == 0)
    {
        double elapsed_time = current_time - start_time;
        double avg_fps = recv_frames / elapsed_time;
        ROS_INFO("=== ROS Subscribe FPS Stats ===");
        ROS_INFO("Average subscribe FPS: %.2f", avg_fps);
        ROS_INFO("Max subscribe FPS: %.2f, Min subscribe FPS: %.2f", max_fps, min_fps);
        ROS_INFO("Total received frames: %d", recv_frames);
        ROS_INFO("Average message delay: %.2f ms", msg_delay);
        ROS_INFO("================================");
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "image_subscriber");
    ros::NodeHandle nh;

    // 使用共享内存传输订阅压缩图像，大消息不再走普通 ROS loopback socket。
    shm_transport::Topic shm_topic(nh);
    shm_transport::Subscriber<sensor_msgs::CompressedImage> shm_sub = shm_topic.subscribe("/image_raw/compressed", 1000, imageCallback);

    ros::spin();
    return 0;
}
