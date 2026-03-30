#include <ros/ros.h>
#include <sensor_msgs/CompressedImage.h>
#include <sensor_msgs/Image.h>
#include <std_msgs/Bool.h>
#include <sys/time.h>

#include <string>

#include "v4l2.h"

using namespace std;

int enable_camera = 1;

void enable_camera_callback(const std_msgs::Bool::ConstPtr& msg) {
    enable_camera = msg->data ? 1 : 0;
    ROS_INFO("enable_camera: %s", enable_camera ? "true" : "false");
}

int main(int argc, char** argv) {
    ros::init(argc, argv, "usb_camera");
    ros::NodeHandle nh("~");

    string compressed_topic;
    string raw_topic;
    string dev_name;
    string frame_id;
    string format_name;
    int width;
    int height;
    int div;
    int fps;

    nh.param<string>("pub_image_topic", compressed_topic, "/image_raw/compressed");
    nh.param<string>("pub_raw_image_topic", raw_topic, "/image_raw/yuv");
    nh.param<string>("dev_name", dev_name, "/dev/video0");
    nh.param<string>("frame_id", frame_id, "camera");
    nh.param<int>("width", width, 1280);
    nh.param<int>("height", height, 720);
    nh.param<int>("div", div, 1);
    nh.param<int>("fps", fps, 30);
    nh.param<string>("capture_format", format_name, "mjpeg");

    CaptureFormat capture_format = CaptureFormat::kMjpeg;
    if (!parse_capture_format(format_name, &capture_format)) {
        ROS_WARN("Unsupported capture_format '%s', fallback to mjpeg", format_name.c_str());
        capture_format = CaptureFormat::kMjpeg;
    }

    ros::Subscriber enable_sub = nh.subscribe("/enable_camera", 10, enable_camera_callback);
    ros::Publisher compressed_pub =
        nh.advertise<sensor_msgs::CompressedImage>(compressed_topic, 1);
    ros::Publisher raw_pub = nh.advertise<sensor_msgs::Image>(raw_topic, 1);
    ros::Rate loop_rate(fps);

    V4l2 v4l2;
    FrameBuf frame_buf;
    sensor_msgs::CompressedImage compressed_msg;
    sensor_msgs::Image raw_msg;
    unsigned int frame_cnt = 0;
    double last_t = 0;
    size_t bytes_acc = 0;
    unsigned int stats_frames = 0;
    int video_index = 0;
    std::string dev_names[] = {
        dev_name,
        "/dev/video0",
        "/dev/video1",
        "/dev/video2",
        "/dev/video3",
        "/dev/video4",
    };
    const int dev_name_count = sizeof(dev_names) / sizeof(dev_names[0]);

    while (nh.ok() &&
           v4l2.init_video(dev_names[video_index].c_str(), width, height, capture_format, fps) < 0) {
        ROS_WARN("Failed to init %s, try next...", dev_names[video_index].c_str());
        video_index = (video_index + 1) % dev_name_count;
        ros::spinOnce();
        sleep(1);
    }
    ROS_INFO("Camera init success: %s, capture_format=%s, fps=%d",
             dev_names[video_index].c_str(), capture_format_to_string(capture_format), fps);

    while (nh.ok()) {
        const bool need_publish =
            (v4l2.active_format == CaptureFormat::kMjpeg)
                ? (compressed_pub.getNumSubscribers() > 0 && enable_camera == 1)
                : (raw_pub.getNumSubscribers() > 0 && enable_camera == 1);

        if (v4l2.get_data(&frame_buf) < 0) {
            ROS_WARN("Get data failed, reinit camera...");
            v4l2.release_video();

            while (nh.ok() &&
                   v4l2.init_video(dev_names[video_index].c_str(), width, height, capture_format,
                                   fps) < 0) {
                video_index = (video_index + 1) % dev_name_count;
                ros::spinOnce();
                sleep(1);
            }
            continue;
        }

        if (need_publish && (frame_cnt % div == 0)) {
            if (v4l2.active_format == CaptureFormat::kMjpeg) {
                compressed_msg.header.stamp = ros::Time::now();
                compressed_msg.header.frame_id = frame_id;
                compressed_msg.format = capture_format_to_message_format(v4l2.active_format);
                compressed_msg.data.assign(frame_buf.start, frame_buf.start + frame_buf.length);
                compressed_pub.publish(compressed_msg);
            } else {
                raw_msg.header.stamp = ros::Time::now();
                raw_msg.header.frame_id = frame_id;
                raw_msg.height = height;
                raw_msg.width = width;
                raw_msg.encoding = capture_format_to_image_encoding(v4l2.active_format);
                raw_msg.is_bigendian = 0;
                raw_msg.step = width * 2;
                raw_msg.data.assign(frame_buf.start, frame_buf.start + frame_buf.length);
                raw_pub.publish(raw_msg);
            }
        }

        frame_cnt++;
        bytes_acc += static_cast<size_t>(frame_buf.length);
        stats_frames++;
        if (frame_cnt % 30 == 0) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            double t = tv.tv_sec + tv.tv_usec / 1000000.0;
            if (last_t > 0 && stats_frames > 0) {
                const double measured_fps = 30.0 / div / (t - last_t);
                const double avg_bytes = static_cast<double>(bytes_acc) / stats_frames;
                const double mb_per_sec =
                    (avg_bytes * measured_fps) / (1024.0 * 1024.0);
                ROS_INFO("capture stats: format=%s frame_bytes=%d avg_bytes=%.0f throughput=%.2f MiB/s subs=%d",
                         capture_format_to_string(v4l2.active_format), frame_buf.length, avg_bytes,
                         mb_per_sec,
                         (v4l2.active_format == CaptureFormat::kMjpeg)
                             ? compressed_pub.getNumSubscribers()
                             : raw_pub.getNumSubscribers());
            }
            last_t = t;
            bytes_acc = 0;
            stats_frames = 0;
        }

        ros::spinOnce();
        loop_rate.sleep();
    }

    v4l2.release_video();
    return 0;
}
