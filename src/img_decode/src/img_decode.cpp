#include "img_decode.h"

#include <algorithm>
#include <cctype>
#include <thread>

namespace {

std::string normalize_input_format(const std::string &format_name) {
    std::string format = format_name;
    for (size_t i = 0; i < format.size(); ++i) {
        format[i] = static_cast<char>(tolower(format[i]));
    }

    if (format == "jpeg" || format == "mjpeg" || format == "mjpg") {
        return "mjpeg";
    }
    if (format == "yuyv" || format == "yuv" || format == "yuv422" ||
        format == "yuy2" || format == "yuv422_yuy2") {
        return "yuyv";
    }
    return "";
}

}  // namespace

ImgDecode::ImgDecode() : nh("~")
{
    string pub_raw_image_topic;

    nh.param<string>("sub_jpeg_image_topic", sub_jpeg_image_topic, "/image_raw/compressed");
    nh.param<string>("sub_raw_image_topic", sub_raw_image_topic, "/image_raw/yuv");
    nh.param<string>("pub_raw_image_topic", pub_raw_image_topic, "/camera/image_raw");
    nh.param<int>("fps_div", fps_div, 1);
    nh.param<double>("scale", scale, 0.5);
    nh.param<int>("width", width, 1280);
    nh.param<int>("height", height, 720);
    nh.param<string>("input_format", input_format, "mjpeg");

    input_format = normalize_input_format(input_format);
    if (input_format.empty()) {
        input_format = "mjpeg";
    }

    raw_image_pub = nh.advertise<sensor_msgs::Image>(pub_raw_image_topic, 10);

    ROS_INFO("img_decode init: input_format=%s sub_jpeg=%s sub_raw=%s pub_raw=%s scale=%.3f fps_div=%d size=%dx%d",
             input_format.c_str(), sub_jpeg_image_topic.c_str(), sub_raw_image_topic.c_str(),
             pub_raw_image_topic.c_str(), scale, fps_div, width, height);

#if(USE_ARM_LIB==1)
    if (input_format == "mjpeg") {
        mpp_decode.init(width, height, "mjpeg");
    }
#endif
}

void ImgDecode::compressed_image_callback(const sensor_msgs::CompressedImageConstPtr& msg)
{
    frame_cnt++;
    if(frame_cnt % fps_div != 0)
    {
        return;
    }

    const std::string frame_format = normalize_input_format(msg->format);
    if (frame_format != "mjpeg")
    {
        ROS_WARN_THROTTLE(2.0, "compressed input only supports mjpeg, got '%s'",
                          msg->format.c_str());
        return;
    }

    if (msg->data.empty())
    {
        ROS_WARN("compressed image data is empty");
        return;
    }

    int src_width = 0;
    int src_height = 0;

#if(USE_ARM_LIB==1)
    if (!mpp_decode.ensure_config(width, height, "mjpeg"))
    {
        ROS_WARN("mpp decode init failed for mjpeg");
        return;
    }

    DecodedFrame decoded;
    int ret = mpp_decode.decode((unsigned char*)msg->data.data(), msg->data.size(), decoded);
    if(ret < 0)
    {
        ROS_WARN("mjpeg decode error! size=%zu", msg->data.size());
        return;
    }

    src_width = decoded.width;
    src_height = decoded.height;
#else
    image = cv::imdecode(cv::Mat(msg->data), cv::IMREAD_COLOR);
    if (image.empty())
    {
        ROS_WARN("Failed to decode compressed image");
        return;
    }
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
    src_width = image.cols;
    src_height = image.rows;
#endif

    msg_pub.header = msg->header;
    msg_pub.height = std::max(1, static_cast<int>(src_height * scale));
    msg_pub.width = std::max(1, static_cast<int>(src_width * scale));
    msg_pub.encoding = "rgb8";
    msg_pub.step = msg_pub.width * 3;
    msg_pub.data.resize(msg_pub.height * msg_pub.width * 3);

#if(USE_ARM_LIB==1)
    int rga_ret = -1;
    if (decoded.format == MPP_FMT_RGB888 && !decoded.image.empty())
    {
        image = decoded.image;
        rga_ret = rga_resize(image, msg_pub.data.data(), scale);
    }
    else
    {
        ROS_WARN("Unsupported MPP output format=%d for mjpeg", decoded.format);
        return;
    }

    if (rga_ret != 0)
    {
        ROS_WARN("rga mjpeg post-process failed, ret=%d", rga_ret);
        return;
    }
#else
    cv::resize(image, image, cv::Size(), scale, scale);
    memcpy(msg_pub.data.data(), image.data, msg_pub.height * msg_pub.width * 3);
#endif

    raw_image_pub.publish(msg_pub);
}

void ImgDecode::raw_image_callback(const sensor_msgs::ImageConstPtr& msg)
{
    frame_cnt++;
    if(frame_cnt % fps_div != 0)
    {
        return;
    }

    const std::string frame_format = normalize_input_format(msg->encoding);
    if (frame_format != "yuyv")
    {
        ROS_WARN_THROTTLE(2.0, "raw input only supports yuyv, got '%s'", msg->encoding.c_str());
        return;
    }

    if (msg->data.empty())
    {
        ROS_WARN("raw image data is empty");
        return;
    }

    msg_pub.header = msg->header;
    msg_pub.height = std::max(1, static_cast<int>(msg->height * scale));
    msg_pub.width = std::max(1, static_cast<int>(msg->width * scale));
    msg_pub.encoding = "rgb8";
    msg_pub.step = msg_pub.width * 3;
    msg_pub.data.resize(msg_pub.height * msg_pub.width * 3);

#if(USE_ARM_LIB==1)
    const int wstride = msg->step / 2;
    const int rga_ret = rga_resize_yuyv422_to_rgb(const_cast<unsigned char*>(msg->data.data()),
                                                  msg->width, msg->height, wstride, msg->height,
                                                  msg_pub.data.data(), msg_pub.width,
                                                  msg_pub.height);
    if (rga_ret != 0)
    {
        ROS_WARN("rga yuyv post-process failed, ret=%d", rga_ret);
        return;
    }
#else
    cv::Mat yuyv(msg->height, msg->width, CV_8UC2,
                 const_cast<unsigned char*>(msg->data.data()), msg->step);
    cv::cvtColor(yuyv, image, cv::COLOR_YUV2RGB_YUY2);
    cv::resize(image, image, cv::Size(msg_pub.width, msg_pub.height));
    memcpy(msg_pub.data.data(), image.data, msg_pub.height * msg_pub.width * 3);
#endif

    raw_image_pub.publish(msg_pub);
}

void ImgDecode::run_check_thread()
{
    int last_subscribers = 0;
    ros::Rate check_rate(10);

    while(ros::ok())
    {
        int subscribers = raw_image_pub.getNumSubscribers();
        if(subscribers==0 && last_subscribers>0)
        {
            ROS_INFO("decode image subscribers = 0, src sub shutdown");
            compressed_image_sub.shutdown();
            raw_image_sub.shutdown();
        }
        else if(subscribers>0 && last_subscribers==0)
        {
            ROS_INFO("decode image subscribers > 0, src sub start, input_format=%s",
                     input_format.c_str());
            if (input_format == "mjpeg") {
                compressed_image_sub = nh.subscribe(sub_jpeg_image_topic, 10,
                                                    &ImgDecode::compressed_image_callback, this);
            } else if (input_format == "yuyv") {
                raw_image_sub = nh.subscribe(sub_raw_image_topic, 10,
                                             &ImgDecode::raw_image_callback, this);
            } else {
                ROS_WARN("unsupported input_format=%s", input_format.c_str());
            }
        }

        last_subscribers = subscribers;
        check_rate.sleep();
    }
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "img_decode");

    ImgDecode img_decode;
    std::thread check_thread(&ImgDecode::run_check_thread, &img_decode);
    ros::spin();

    return 0;
}
