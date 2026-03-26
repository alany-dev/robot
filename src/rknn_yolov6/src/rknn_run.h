#ifndef RknnRun_H
#define RknnRun_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if(USE_ARM_LIB==1)
    #include "RgaUtils.h"
    #include "im2d.h"
    #include "rga.h"
    #include "rknn_api.h"
#endif


#include <opencv2/opencv.hpp>

#include <condition_variable>
#include <thread>

#include <unistd.h>


#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/CameraInfo.h>
#include <opencv2/opencv.hpp>
#include <camera_info_manager/camera_info_manager.h>


#include "std_msgs/Header.h"
#include "std_msgs/String.h"
#include "Queue.h"

/**
 * 一帧图像在 RKNN 节点内部流转时携带的上下文。
 *
 * 之所以单独封装这个结构体，是因为推理线程和后处理线程分离后，
 * 两边都需要共享：
 * - 原始 RGB 图；
 * - 模型输入尺寸；
 * - 原图尺寸；
 * - 原始时间戳；
 * - RKNN 输出 tensor。
 */
typedef struct
{
#if(USE_ARM_LIB==1)
    // outputs:
    // RKNN 输出 tensor 数组。这里按经验预留 5 个槽位，
    // 必须保证不小于真实 io_num.n_output，否则会发生越界崩溃。
    rknn_output outputs[5];//io_num.n_output 注意：这里写小了会崩
#endif

    // orig_img:
    // 供后处理画框和后续发布使用的 RGB 原图。
    cv::Mat orig_img;//RGB

    // mod_size:
    // 模型输入尺寸，例如 640x640。
    cv::Size mod_size;

    // img_size:
    // 原始图像尺寸，例如 640x360。
    cv::Size img_size;

    // header:
    // 输入图像的原始时间戳和 frame_id，会被继续传递给检测图和检测框消息。
    std_msgs::Header header;//用于发布和接收一样的时间
}InferData;


/**
 * RKNN 推理节点。
 *
 * 输入：/camera/image_raw
 * 输出：
 * - /camera/image_det   画好框的 RGB 图像
 * - /ai_msg_det         结构化检测框结果
 *
 * 低时延设计重点：
 * 1. 订阅回调只负责把图像放进短队列，不在回调里直接推理；
 * 2. 推理线程和后处理线程拆开，减少串行等待；
 * 3. 短队列只保留最新帧，系统繁忙时允许丢旧帧；
 * 4. 图像与检测结果共用同一份 header，便于后续 ExactTime 同步。
 */
class RknnRun
{
public:
    RknnRun();

    ros::NodeHandle nh;

    // 模型与标签配置。
    std::string model_file,yaml_file;

    // 在线模式输入 / 输出话题。
    std::string sub_image_topic,pub_image_topic,pub_det_topic;

    // 离线图片测试模式的输入 / 输出目录。
    std::string offline_images_path,offline_output_path;

    // 推理运行参数。
    bool is_offline_image_mode,print_perf_detail,use_multi_npu_core,output_want_float;
    int cls_num;
    double conf_threshold,nms_threshold;
    std::vector<std::string> label_names;

    // 发布 / 订阅对象。
    ros::Subscriber image_sub;
    ros::Publisher image_pub;
    ros::Publisher det_pub;


#if(USE_ARM_LIB==1)
    // RKNN 上下文与输入输出信息。
    rknn_context   ctx;
    rknn_input_output_num io_num;
#endif

    // 量化模型输出反量化时需要的 scale / zero point。
    std::vector<float>    out_scales;
    std::vector<int32_t>  out_zps;


    /**
     * 图像订阅回调。
     *
     * @param msg 一帧 RGB 图像消息。这里只做入队，不直接推理。
     */
    void sub_image_callback(const sensor_msgs::ImageConstPtr& msg);

    // capture_data_queue:
    // 采集回调 -> 推理线程之间的短队列。
    Queue<sensor_msgs::ImageConstPtr> capture_data_queue;

    /**
     * 推理线程主循环。
     *
     * @return 正常结束时返回 0；异常时可能提前 shutdown。
     */
    int run_infer_thread();

    /**
     * 后处理线程主循环。
     *
     * @return 正常结束时返回 0；异常时可能提前退出。
     */
    int run_process_thread();

    /**
     * 根据下游是否真的在消费结果，决定是否订阅输入图像。
     */
    void run_check_thread();

    // process_data_queue:
    // 推理线程 -> 后处理线程之间的短队列。
    Queue<InferData> process_data_queue;
};

#endif // RknnRun_H
