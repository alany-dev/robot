#include <iostream>
#include <fstream>
#include "v4l2.h"
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

bool g_running = true;

void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        cout << "\n收到退出信号，正在清理资源..." << endl;
        g_running = false;
    }
}

int main(int argc, char** argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 默认参数
    const char* dev_name = "/dev/video0";
    int width = 1280;
    int height = 720;
    int save_count = 10;  // 保存10张图片

    cout << "=== V4L2 接口测试程序 ===" << endl;
    cout << "设备: " << dev_name << endl;
    cout << "分辨率: " << width << "x" << height << endl;
    cout << "保存图片数量: " << save_count << endl;
    cout << "保存目录: ./test_images/" << endl;
    cout << "=========================" << endl;

    // 创建保存目录
    mkdir("test_images", 0755);

    // 创建 V4l2 对象
    V4l2 v4l2;

    // 初始化摄像头
    cout << "\n正在初始化摄像头..." << endl;
    int ret = v4l2.init_video(dev_name, width, height);
    if (ret < 0)
    {
        cerr << "摄像头初始化失败！错误码: " << ret << endl;
        return -1;
    }
    cout << "摄像头初始化成功！" << endl;

    cout << "\n开始采集图像..." << endl;

    FrameBuf frame_buf;
    int saved_count = 0;

    while (g_running && saved_count < save_count)
    {
        // 获取图像数据
        ret = v4l2.get_data(&frame_buf);
        if (ret < 0)
        {
            cerr << "获取图像数据失败！错误码: " << ret << endl;
            usleep(100000);
            continue;
        }

        string filename = "test_images/image_" + to_string(saved_count) + ".jpg";
        ofstream file(filename, ios::binary);
        if (file.is_open())
        {
            file.write(reinterpret_cast<const char*>(frame_buf.start), frame_buf.length);
            file.close();
            saved_count++;
            cout << "已保存: " << filename << " (大小: " << frame_buf.length << " 字节)" << endl;
        }
        else
        {
            cerr << "文件保存失败: " << filename << endl;
        }
    }

    // 释放资源
    cout << "\n正在释放摄像头资源..." << endl;
    v4l2.release_video();
    
    cout << "\n测试完成！已保存 " << saved_count << " 张图片" << endl;
    return 0;
}
