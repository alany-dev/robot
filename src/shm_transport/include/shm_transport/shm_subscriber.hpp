#ifndef __SHM_SUBSCRIBER_HPP__
#define __SHM_SUBSCRIBER_HPP__

#include <boost/interprocess/managed_shared_memory.hpp>
#include "ros/ros.h"
#include "std_msgs/UInt64.h"
#include "shm_object.hpp"

namespace shm_transport
{

class Topic;

template <class M>
class Subscriber;

template < class M >
class SubscriberCallbackHelper
{
  using Func = void (*)(const boost::shared_ptr< const M > &);

public:
  ~SubscriberCallbackHelper() {
  }

  /**
   * 处理 ROS 网络里收到的共享内存 handle。
   *
   * @param actual_msg std_msgs/UInt64：
   *                   - data: 共享内存中 ShmMessage 的 handle。
   */
  void callback(const std_msgs::UInt64::ConstPtr & actual_msg) {
    if (!pobj_) {
      // 如果订阅者先于发布者启动，这里延迟打开共享内存对象，避免初始化阶段直接失败。
      mng_shm * pshm = new mng_shm(boost::interprocess::open_only, name_.c_str());
      pobj_ = ShmObjectPtr(new ShmObject(pshm, name_));
    }

    // 根据 handle 找到共享内存中的真实消息块。
    ShmMessage * ptr = (ShmMessage *)pobj_->pshm_->get_address_from_handle(actual_msg->data);

    // 增加引用计数，通知发布端“这条消息正在被消费”。
    ptr->take();

    // 反序列化共享内存里的字节流，还原成普通 ROS 消息对象。
    boost::shared_ptr< M > msg(new M());
    ros::serialization::IStream in(ptr->data, ptr->len);
    ros::serialization::deserialize(in, *msg);

    // 反序列化完成后减少引用计数。
    ptr->release();

    // 最后再调用用户自己的业务回调。
    fp_(msg);
  }

private:
  /**
   * @param topic 逻辑 topic 名称。
   * @param fp    用户注册的最终消息回调函数。
   */
  SubscriberCallbackHelper(const std::string &topic, Func fp)
    : pobj_(), name_(topic), fp_(fp) {
    // change '/' in topic to '_'
    for (int i = 0; i < name_.length(); i++)
      if (name_[i] == '/')
        name_[i] = '_';
  }

  // pobj_:
  // 共享内存对象封装。
  ShmObjectPtr pobj_;

  // name_:
  // 对应共享内存对象名称。
  std::string name_;

  // fp_:
  // 用户侧真正关心的业务回调。
  Func fp_;

friend class Topic;
friend class Subscriber<M>;
};

template <class M>
class Subscriber
{
public:
  Subscriber() {
  }

  ~Subscriber() {
  }

  void shutdown() {
    sub_.shutdown();
  }

  std::string getTopic() const {
    return sub_.getTopic();
  }

  uint32_t getNumPublishers() const {
    return sub_.getNumPublishers();
  }

private:
  /**
   * 只有 Topic::subscribe 会调用这个构造函数。
   *
   * @param sub  实际订阅 handle 的 ros::Subscriber。
   * @param phlp 负责把 handle 还原为真实消息的辅助对象。
   */
  Subscriber(const ros::Subscriber & sub, SubscriberCallbackHelper< M > * phlp)
      : sub_(sub), phlp_(phlp) {
  }

  // sub_:
  // 实际订阅 std_msgs::UInt64 handle 的 ROS 订阅者。
  ros::Subscriber sub_;

  // phlp_:
  // 负责 handle -> 真实消息转换的辅助对象。
  boost::shared_ptr< SubscriberCallbackHelper< M > > phlp_;

friend class Topic;
};

} // namespace shm_transport

#endif // __SHM_SUBSCRIBER_HPP__
