#ifndef __SHM_TOPIC_HPP__
#define __SHM_TOPIC_HPP__

#include "ros/ros.h"
#include "shm_publisher.hpp"
#include "shm_subscriber.hpp"

namespace shm_transport
{

class Topic
{
public:
  Topic(const ros::NodeHandle & parent) {
    nh_ = parent;
  }

  ~Topic() {
  }

  /**
   * 创建一个共享内存发布者。
   *
   * @param topic      逻辑 topic 名称。
   * @param queue_size ROS handle 话题的队列深度。
   * @param mem_size   共享内存总大小（字节）。
   */
  template < class M >
  Publisher advertise(const std::string & topic, uint32_t queue_size, uint32_t mem_size) {
    ros::Publisher pub = nh_.advertise< std_msgs::UInt64 >(topic, queue_size);
    return Publisher(pub, topic, mem_size);
  }

  /**
   * 创建一个共享内存订阅者。
   *
   * @param topic      逻辑 topic 名称。
   * @param queue_size ROS handle 话题的队列深度。
   * @param fp         用户业务回调，收到的参数已经是反序列化后的真实消息。
   */
  template < class M >
  Subscriber< M > subscribe(const std::string & topic, uint32_t queue_size, void(*fp)(const boost::shared_ptr< const M > &)) {
    SubscriberCallbackHelper< M > * phlp = new SubscriberCallbackHelper< M >(topic, fp);
    ros::Subscriber sub = nh_.subscribe(topic, queue_size, &SubscriberCallbackHelper< M >::callback, phlp);
    return Subscriber< M >(sub, phlp);
  }

private:
  // nh_:
  // 用于创建“发送 handle 的 ROS topic”发布者/订阅者的 NodeHandle。
  ros::NodeHandle nh_;
};

} // namespace shm_transport

#endif // __SHM_TOPIC_HPP__

