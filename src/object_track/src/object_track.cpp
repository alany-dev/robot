#include <ros/ros.h>
#include <std_msgs/String.h>
#include <geometry_msgs/Twist.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <tf/transform_listener.h>
#include <opencv2/opencv.hpp>
#include <json.hpp>
#include <cv_bridge/cv_bridge.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/exact_time.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <std_msgs/Bool.h>


#include "ai_msgs/Dets.h"

#include <thread>

using namespace nlohmann;

/**
 * 读取相机标定文件，并根据前级 img_decode 的缩放比例同步缩放内参。
 *
 * 当前链路里，usb_camera 采到 1280x720 后，img_decode 会把图像缩到 640x360，
 * 因此这里读取到原始标定参数后，也要把 fx / fy / cx / cy 一起缩小一半，
 * 否则激光雷达投影到图像上会整体偏移。
 *
 * @param calibration_file 标定文件路径，例如 fisheye.yml。
 * @param CameraMat        输出参数，相机内参矩阵 K。
 * @param DistCoeff        输出参数，畸变系数 D。
 */
void read_camera_info_from_file(std::string &calibration_file,cv::Mat &CameraMat,cv::Mat &DistCoeff)
{

    ROS_INFO("open file: %s",calibration_file.c_str());
    
	cv::FileStorage fs(calibration_file, cv::FileStorage::READ);
	
	if (!fs.isOpened())
	{
		ROS_ERROR("Cannot open file calibration file '%s'", calibration_file.c_str());
		ros::shutdown();
		return;
	}
    
    cv::Size ImageSize_raw;
    
    //fs["CameraExtrinsicMat"] >> CameraExtrinsicMat;
    fs["CameraMat"] >> CameraMat;
    fs["DistCoeff"] >> DistCoeff;
    fs["ImageSize"] >> ImageSize_raw;

    fs.release();
    
    // 检查标定数据是否有效
    if (CameraMat.empty() || DistCoeff.empty()) {
        ROS_ERROR("Failed to read camera calibration data from file: %s", calibration_file.c_str());
        ros::shutdown();
        return;
    }
    
   

    // 原始标定文件对应的是 1280x720 图像，而链路里的检测/跟踪图像是 640x360。
    // 因此这里把内参等比缩小到 1/2，和前级缩图保持一致。
    cv::Size ImageSize(ImageSize_raw.width/2,ImageSize_raw.height/2);

    CameraMat = CameraMat/2;//缩放内参
    CameraMat.at<double>(2,2) = 1;//右下角不缩放
    
   
}


/**
 * 视觉检测 + 激光距离融合跟踪节点。
 *
 * 输入：
 * - /camera/image_det：RKNN 已经画好检测框的图像
 * - /ai_msg_det：       RKNN 发布的结构化检测框
 * - /scan：             激光雷达点云
 *
 * 输出：
 * - /camera/image_det_track：叠加跟踪信息后的图像
 * - /cmd_vel：               机器人跟踪控制速度
 *
 * 关键思想：
 * 1. 图像和检测框按同一时间戳精确同步；
 * 2. 视觉负责“找到要跟踪的目标是谁”；
 * 3. 激光雷达负责“估这个目标离机器人多远”；
 * 4. 再把角度误差和距离误差转成线速度 / 角速度。
 */
class ObjectTrack 
{
public:
    //如果没有破浪线nh.param后面的参数要加上节点名字，否则获取不到launch中的参数，所以最好加上波浪线
    ObjectTrack() : nh("~")
    {
        // 标定文件与坐标系名称。
        nh.param<std::string>("calibration_file", calibration_file, "fisheye.yaml");
        nh.param<std::string>("camera_frame", camera_frame, "camera_link");
        nh.param<std::string>("laser_frame", laser_frame, "laser_link");
        nh.param<std::string>("pub_image_topic", pub_image_topic, "/camera/image_det_track");

        // 输入话题：激光、检测图像、结构化检测框。
        nh.param<std::string>("sub_laser_topic", sub_laser_topic, "/scan");
        //nh.param<std::string>("sub_camera_info_topic", sub_camera_info_topic, "/camera/camera_info");
        nh.param<std::string>("sub_image_topic", sub_image_topic, "/camera/image_det");
        nh.param<std::string>("sub_det_topic", sub_det_topic,  "/ai_msg_det");
        nh.param<std::string>("tracking_class_name", tracking_class_name,  "person");

        // 当前节点默认按照 640x360 图像工作，与前级 img_decode 的 scale=0.5 对齐。
        nh.param<int>("width", width,  640);
        nh.param<int>("height",height, 360);
        nh.param<int>("object_area_threshold",object_area_threshold, 20);

        // 跟踪控制参数：
        // - kp_angular: 目标中心偏离图像中心时的角速度比例；
        // - kp_linear:  距离误差到线速度的比例；
        // - max_angular / max_linear: 限幅，防止速度指令过大；
        // - keep_distance: 理想跟随距离。
        nh.param<double>("kp_angular",  kp_angular,   0.01);
        nh.param<double>("kp_linear",   kp_linear,  0.5);
        nh.param<double>("max_angular", max_angular,1.5);
        nh.param<double>("max_linear",  max_linear, 0.1);
        nh.param<double>("keep_distance", keep_distance, 0.3);
        nh.param<double>("valid_distance_min",  valid_distance_min, 0.05);
        nh.param<double>("valid_distance_max",  valid_distance_max, 5);

        nh.param<bool>("control_linear",  control_linear, true);

        // 雷达与相机存在安装偏差时，可用这些参数做运行期补偿。
        nh.param<double>("offset_x", offset_x, 0.0);
        nh.param<double>("offset_y", offset_y, 0.0);
        nh.param<double>("offset_z", offset_z, 0.0);
        nh.param<double>("offset_roll", offset_roll, 0.0);
        nh.param<double>("offset_pitch", offset_pitch, 0.0);
        nh.param<double>("offset_yaw", offset_yaw, 0.0);

      

        // 先把缩放后的相机内参读入内存。
        read_camera_info_from_file(calibration_file, K, D);

        ROS_INFO("waitForTransform laser_link");
        // 获取 camera_link 和 laser_link 之间的固定外参。
        // 由于安装位置是固定的，这里只需要读一次。
        try
        {
            listener.waitForTransform(camera_frame, laser_frame, ros::Time(0), ros::Duration(10.0));
            listener.lookupTransform(camera_frame,  laser_frame, ros::Time(0), laser_transform);
        }
        catch (tf::TransformException ex) 
        {
            ROS_ERROR("%s", ex.what());
        }
        ROS_INFO("waitForTransform ok");

        // 发布控制速度和最终调试图像。
        cmd_vel_pub = nh.advertise<geometry_msgs::Twist>("/cmd_vel", 10);
        image_pub = nh.advertise<sensor_msgs::Image>(pub_image_topic, 10);

        
        //camera_info_sub = nh.subscribe(sub_camera_info_topic, 10, &ObjectTrack::camera_info_callback, this);

        // 运行期允许动态打开 / 关闭跟踪和激光处理。
        enable_tracking_sub = nh.subscribe("/enable_tracking", 10, &ObjectTrack::enable_tracking_callback, this);
        enable_lidar_sub = nh.subscribe("/enable_lidar", 10, &ObjectTrack::enable_lidar_callback, this);
    }

    /**
     * 打开或关闭跟踪功能。
     *
     * @param msg std_msgs/Bool：
     *            - true:  开始输出跟踪控制，并在无人查看时主动订阅上游结果；
     *            - false: 停止跟踪，速度清零。
     */
    void enable_tracking_callback(const std_msgs::Bool::ConstPtr& msg)
    {
        enable_tracking = msg->data;

        int subscribers = image_pub.getNumSubscribers();//获取目标图像的订阅者个数
        // 如果当前没有人查看最终跟踪图像，就由 enable_tracking 直接决定要不要继续订阅上游。
        if(subscribers==0)//如果没有人订阅，说明没有客户端查看，此时才对源话题的状态做修改
        {
            if (msg->data)
            {
                if(image_sub)
                    image_sub->subscribe(); // 开启图像话题的订阅
                if(det_sub)
                    det_sub->subscribe();   // 开启检测结果话题的订阅
            }
            else
            {
                if(image_sub)
                    image_sub->unsubscribe();  // 停止图像话题的订阅
                if(det_sub)
                    det_sub->unsubscribe();    // 停止检测结果话题的订阅
            }
        }

        // 跟踪状态一旦切换，先把速度指令清零，避免沿用上一次控制结果。
        cmd_vel_pub.publish(geometry_msgs::Twist());

        // 关联的激光投影和目标距离也一起清空，避免沿用旧数据。
        pixel_points.clear();
        object_distance = 0;
    }

    /**
     * 打开或关闭激光雷达订阅。
     *
     * @param msg std_msgs/Bool：
     *            - true:  订阅 /scan，并把点投影到图像里参与距离估计；
     *            - false: 关闭 /scan 订阅，只保留纯视觉框选。
     */
    void enable_lidar_callback(const std_msgs::Bool::ConstPtr& msg)
    {
        if (msg->data) 
        {
            scan_sub = nh.subscribe(sub_laser_topic, 10, &ObjectTrack::scan_callback, this);
            ROS_INFO("enable_lidar true");
        }
        else
        {
            scan_sub.shutdown();

            //雷达点云和距离清0
            pixel_points.clear();
            object_distance = 0;

            ROS_INFO("enable_lidar false");
        }
    }
            
    /**
     * 图像与检测框的精确同步回调。
     *
     * @param image_msg 已带检测框的 RGB 图像，时间戳来自 RKNN 输入 header。
     * @param dets_msg  与 image_msg 同一时间戳下的结构化检测结果。
     */
    void sync_callback(const sensor_msgs::ImageConstPtr &image_msg,const ai_msgs::DetsConstPtr &dets_msg)//const std_msgs::StringPtr &msg)
    {
        // 直接包裹消息里的像素 buffer，在这张图上继续画跟踪信息。
        cv::Mat image(image_msg->height, image_msg->width, CV_8UC3, (char *)image_msg->data.data());

        // 1. 从当前所有检测框里选出“面积最大且类别匹配”的目标。
        // 对跟随场景来说，面积更大的目标通常是离机器人更近、也更可能是主目标的那个。
        int max_area = object_area_threshold;//必须大于20像素
        int object_index = -1;
        
        for (int i = 0; i < dets_msg->dets.size(); i++) 
        {
            ai_msgs::Det det = dets_msg->dets[i];
            int x1 = det.x1;
            int y1 = det.y1;
            int x2 = det.x2;
            int y2 = det.y2;
            std::string cls_name = det.cls_name;
            int area = (x2 - x1) * (y2 - y1);
            if (cls_name == tracking_class_name && area > max_area) 
            {
                max_area = area;
                object_index = i;
                object_det = det;
            }
        }

        // 2. 如果成功找到了目标框，就把像素偏差转成角速度、把距离偏差转成线速度。
        if (object_index != -1)
        {
            int x1 = object_det.x1;
            int y1 = object_det.y1;
            int x2 = object_det.x2;
            int y2 = object_det.y2;

            int object_x = (x1 + x2) / 2;
            int object_y = (y1 + y2) / 2;

            //ROS_INFO("object_x,y=%d,%d",object_x,object_y);

            // 图像中心偏差 -> 角速度：
            // 目标在图像左侧就左转，目标在右侧就右转。
            // 例如宽度 640 时，中心是 320，若目标中心在 200，则误差为 120 像素。
            
            cmd_vel.angular.z = 0.01 * (width/2 - object_x);//0.01 * (320-最极端0) = 最大3.2

            //角速度限幅
            if(cmd_vel.angular.z>max_angular)
                cmd_vel.angular.z = max_angular;
            else if(cmd_vel.angular.z<-max_angular)
                cmd_vel.angular.z = -max_angular;

            // 距离误差 -> 线速度：
            // 只有当激光估计到的距离落在合法区间内时，才允许前后运动。
            if(object_distance>=valid_distance_min && object_distance<=valid_distance_max)//5cm~5米之间认为识别正常
                cmd_vel.linear.x = 0.5 * (object_distance - keep_distance);//0.5 * (0.5-0.3) = 0.1
            else//否则线速度给0
                cmd_vel.linear.x = 0;

            //线速度限幅
            if(cmd_vel.linear.x>max_linear)
                cmd_vel.linear.x = max_linear;
            else if(cmd_vel.linear.x<-max_linear)
                cmd_vel.linear.x = -max_linear;

            //如果不控制线速度，则清0
            if(control_linear==false)
                cmd_vel.linear.x = 0;

            //ROS_INFO("distance: %.2f m control: %d linear.x: %.2f m/s",object_distance,control_linear,cmd_vel.linear.x);

            
            // 在图上叠加当前跟踪目标和控制信息，便于调试“为什么转 / 为什么停”。
            rectangle(image, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0,255,0), 3);

            putText(image, cv::format("[px:%d,dis:%.2f] [vx:%.2f,vz:%.2f]",object_x,object_distance,cmd_vel.linear.x,cmd_vel.angular.z), cv::Point(20,20), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,0), 2);
        }
        else
        {
            // 没找到目标时，立即停止机器人，避免盲目沿用上一次速度。
            cmd_vel.linear.x = 0;
            cmd_vel.angular.z = 0;

            putText(image, "[No target found]", cv::Point(20,20), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,0), 2);
        }

        // 只有明确打开跟踪开关时才真正输出控制速度。
        if(enable_tracking)//如果使能跟踪打开
            cmd_vel_pub.publish(cmd_vel);

        // 把激光投影点一并画出来，便于观察“当前距离估计到底来自哪些点”。
        for(int i=0; i < pixel_points.size(); i++)
        {
            // 确保半径在合理范围内
            double radius = 2.0 / std::abs(pixel_points[i].z);
            radius = std::max(1.0, std::min(radius, 10.0)); // 限制半径在1-10像素之间
            
            cv::circle(image, cv::Point(pixel_points[i].x, pixel_points[i].y),
                radius, cv::Scalar(0, 255, 0), -1);//绘制投影点云
        }

        //cv::imshow("image",image);
        //cv::waitKey(1);

        // 当前 image 直接包裹了 message buffer，这里 memcpy 主要是保持原有写法，
        // 确保最终发布出去的是已经叠加完跟踪信息的图像。
        memcpy((char *)image_msg->data.data(), image.data, image_msg->data.size());
        image_pub.publish(image_msg);
    }

    /**
     * 处理一帧激光数据，并估计当前被跟踪目标的距离。
     *
     * @param msg LaserScan：
     *            - ranges:         每个角度采样点的距离值；
     *            - angle_min:      第一束激光的角度；
     *            - angle_increment:相邻两束激光的角度间隔；
     *            - range_min/max:  有效距离区间。
     */
    void scan_callback(const sensor_msgs::LaserScanConstPtr& msg)
    {
        float min_distance = msg->range_max;//最小距离初始值
        std::vector<cv::Point3d> camera_points;
        for (int i = 0; i < msg->ranges.size(); i++)
        {
            float range = msg->ranges[i];//读取距离
            if (std::isnan(range) || range >= msg->range_max || range <= msg->range_min)
                continue;
            

            // 当前激光束对应的角度 = 起始角 + 序号 * 角度分辨率。
            float angle = msg->angle_min + i * msg->angle_increment;//读取角度
            tf::Vector3 point(range * cos(angle), range * sin(angle), 0.0);//得到点云坐标

            // 把雷达坐标系下的点先变换到相机坐标系。
            point = laser_transform * point;//转换到相机坐标系下
            
            // 应用手动偏移
            point.setX(point.x() + offset_x);
            point.setY(point.y() + offset_y);
            point.setZ(point.z() + offset_z);
            
            // 应用旋转偏移（简单的欧拉角旋转）
            if (offset_roll != 0.0 || offset_pitch != 0.0 || offset_yaw != 0.0) {
                tf::Matrix3x3 rotation;
                rotation.setRPY(offset_roll, offset_pitch, offset_yaw);
                point = rotation * point;
            }
            
            // if(point.z() > 0) //z值必须是负数，在相机的下方
            //     continue;

            cv::Point3d camera_point(point.x(), point.y(), point.z());
            camera_points.push_back(camera_point);//相机坐标系下的点坐标
        }

        // if(camera_info_ok==false)
        //     return;

        // 注释：原本使用鱼眼投影，但相机是普通广角 130 度相机，不是鱼眼相机，
        // 因此这里改用普通相机投影函数 cv::projectPoints。
        
        // 检查相机标定矩阵是否有效
        if (K.empty() || D.empty()) {
            ROS_ERROR("Camera calibration matrices K or D are empty!");
            return;
        }
        
        // 检查点云数据是否为空
        if (camera_points.empty()) {
            ROS_WARN("No valid camera points to project");
            return;
        }
        
        std::vector<cv::Point2d> pixels;
        // projectPoints 参数说明：
        // - camera_points: 相机坐标系下的 3D 点；
        // - rvec:          旋转向量，这里复用了手工偏移角；
        // - tvec:          平移向量，这里为 0，因为平移偏移已提前加到点上；
        // - K:             相机内参矩阵；
        // - D:             畸变系数；
        // - pixels:        输出参数，投影后的像素坐标。
        cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);  // 旋转向量
        cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);  // 平移向量
        rvec.at<double>(0) = offset_roll;
        rvec.at<double>(1) = offset_pitch;
        rvec.at<double>(2) = offset_yaw;
        
        try {
            cv::projectPoints(camera_points, rvec, tvec, K, D, pixels);
        } catch (cv::Exception& e) {
            ROS_ERROR("cv::projectPoints failed: %s", e.what());
            ROS_ERROR("K matrix size: %dx%d, D matrix size: %dx%d", K.rows, K.cols, D.rows, D.cols);
            ROS_ERROR("camera_points size: %d", (int)camera_points.size());
            return;
        }
        
        // 取出当前视觉侧选中的目标框，用它在投影点里筛选“属于该目标”的雷达点。
        int x1 = object_det.x1;
        int y1 = object_det.y1;
        int x2 = object_det.x2;
        int y2 = object_det.y2;
        std::vector<cv::Point3d> points;
        for(int i=0; i < pixels.size(); i++)
        {
             int u=pixels[i].x;
            int v=pixels[i].y;

            cv::Point3d camera_point = camera_points[i];
            if (u >= 0 && u < width && v >= 0 && v < height)//如果点云落在图像内
            {
                // 当前实现只要求投影点落在目标框的 x 范围内：
                // 不强约束 y，是为了减弱地面起伏和机身抖动造成的竖向误差。
                if (u > x1 && u < x2)//点云落在检测框的x范围内，不考察高度y，因为路面崎岖的话高度方向会抖动
                {
                    float distance = sqrt(camera_point.x * camera_point.x + camera_point.y * camera_point.y + camera_point.z * camera_point.z);//相机坐标系下的距离
                    if (distance < min_distance)
                    {
                        min_distance = distance;
                    }
                }

                camera_point.x = pixels[i].x;
                camera_point.y = pixels[i].y;
                points.push_back(camera_point);
            }
        }

        // 记录这次投影成功的像素点和最小目标距离，供 sync_callback 画图和控制。
        pixel_points = points;
        object_distance = min_distance;

        ROS_INFO("object_distance: %.2f m",object_distance);

    }

    /**
     * 根据最终调试图像的订阅者数量，决定是否订阅上游图像和检测框。
     *
     * @param image_sub 图像 message_filters 订阅者指针。
     * @param det_sub   检测框 message_filters 订阅者指针。
     */
    void run_check_thread(message_filters::Subscriber<sensor_msgs::Image> *image_sub, message_filters::Subscriber<ai_msgs::Dets> *det_sub)
    {
        this->image_sub = image_sub;
        this->det_sub = det_sub;
        int last_subscribers = 0;
        while(ros::ok())
        {
            int subscribers = image_pub.getNumSubscribers();
            // 没有人查看最终跟踪图时，直接停掉上游同步输入，避免继续做融合计算。
            if(subscribers==0 && last_subscribers>0)
            {
                ROS_INFO("track image subscribers = 0, src sub shutdown");
                image_sub->unsubscribe();  // 停止图像话题的订阅
                det_sub->unsubscribe();    // 停止检测结果话题的订阅
            }
            // 重新出现消费者时再恢复同步输入。
            else if(subscribers>0 && last_subscribers==0)
            {
                ROS_INFO("track image subscribers > 0, src sub start");
                image_sub->subscribe(); // 开启图像话题的订阅
                det_sub->subscribe();   // 开启检测结果话题的订阅
            }

            last_subscribers = subscribers;

            usleep(100*1000);//100ms
        }
    }

    // void camera_info_callback(const sensor_msgs::CameraInfoConstPtr& msg)
    // {
    //     //校准雷达点并投影到像素坐标
    //     memcpy(K.data,(char *)msg->K.data(),3*3*sizeof(double));
    //     memcpy(D.data,(char *)msg->D.data(),1*4*sizeof(double));
    //     camera_info_ok = true;

    //     camera_info_sub.shutdown();//订阅到一次camera info之后，取消订阅camera info
    // }
    
    ros::NodeHandle nh;

    // 配置参数。
    std::string camera_frame,laser_frame,pub_image_topic,calibration_file;
    std::string sub_laser_topic,sub_image_topic,sub_det_topic,tracking_class_name;
    int width,height,object_area_threshold;
    double kp_angular,kp_linear,max_angular,max_linear,keep_distance,valid_distance_min,valid_distance_max;
    bool control_linear;
    
    // 雷达-相机坐标偏移参数
    double offset_x, offset_y, offset_z, offset_roll, offset_pitch, offset_yaw;


    // 运行期控制和数据输入。
    ros::Subscriber scan_sub,enable_tracking_sub,enable_lidar_sub;
    //ros::Subscriber camera_info_sub;

    // 输出控制速度和调试图像。
    ros::Publisher cmd_vel_pub;
    ros::Publisher image_pub;

    // TF 相关对象，用于激光 -> 相机坐标变换。
    tf::TransformListener listener;
    tf::StampedTransform laser_transform;

    //sensor_msgs::CameraInfo camera_info;

    // 缩放后图像对应的相机内参与畸变系数。
    cv::Mat K,D;
    //bool camera_info_ok=false;

    // 当前被选中的目标框、激光投影点，以及估计到的目标距离。
    ai_msgs::Det object_det;
    std::vector<cv::Point3d> pixel_points;
    float object_distance=0;

    // 当前输出的控制速度和跟踪使能状态。
    geometry_msgs::Twist cmd_vel;
    bool enable_tracking = false;

    // 保存 message_filters 订阅者指针，便于 run_check_thread 动态开关订阅。
    message_filters::Subscriber<sensor_msgs::Image> *image_sub;
    message_filters::Subscriber<ai_msgs::Dets> *det_sub;
};




int main(int argc, char** argv) 
{
    ros::init(argc, argv, "object_track");

    ObjectTrack object_track;

    // 这里把图像和检测框的队列深度都设成 1，是因为前级 RKNN 已经保证两者 header 完全一致，
    // 当前节点更关心拿到“最新且同一时间戳”的一组消息，而不是保留大量历史结果。
    message_filters::Subscriber<sensor_msgs::Image> image_sub(object_track.nh, object_track.sub_image_topic, 1);
    message_filters::Subscriber<ai_msgs::Dets> det_sub(object_track.nh,  object_track.sub_det_topic, 1);

    // 用 ExactTime 的原因：
    // rknn_yolov6 在发布 image_det 和 ai_msg_det 时复用了同一个 header，
    // 因此这里不需要 ApproximateTime，直接精确同步即可。
    typedef message_filters::sync_policies::ExactTime<sensor_msgs::Image, ai_msgs::Dets> SyncPolicy;
    message_filters::Synchronizer<SyncPolicy> sync(SyncPolicy(10), image_sub, det_sub);
    sync.registerCallback(boost::bind(&ObjectTrack::sync_callback,&object_track, _1, _2));

    // 默认先不订阅上游，等有人真的订阅 /camera/image_det_track 再开启。
    image_sub.unsubscribe();  // 停止图像话题的订阅
    det_sub.unsubscribe();    // 停止检测结果话题的订阅


    // 独立线程按订阅者数量动态打开 / 关闭上游同步输入。
    std::thread check_thread(&ObjectTrack::run_check_thread, &object_track, &image_sub, &det_sub);
    ros::spin();

    return 0;
}
