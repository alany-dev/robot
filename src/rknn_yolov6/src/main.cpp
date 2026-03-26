#include <ros/ros.h>

#include "postprocess.h"
#include "rknn_run.h"

int main(int argc, char** argv)
{
    ros::init(argc, argv, "rknn_yolov6");

    RknnRun rknn_run;

    // 线程拆分方式：
    // 1. infer_thread   专门负责取图、resize、喂 NPU、拿 tensor；
    // 2. process_thread 专门负责后处理、画框、发布消息；
    // 3. check_thread   根据订阅者数量决定是否继续订阅上游图像。
    //
    // 这样做的目的，是避免把“推理 + 后处理 + 发布”全部串在一个回调线程里，
    // 导致一旦某一段变慢就把整条链路拖长。
    std::thread infer_thread(&RknnRun::run_infer_thread, &rknn_run);
    std::thread process_thread(&RknnRun::run_process_thread, &rknn_run);
    std::thread check_thread(&RknnRun::run_check_thread, &rknn_run);
    
    ros::spin();

    return 0;
}


