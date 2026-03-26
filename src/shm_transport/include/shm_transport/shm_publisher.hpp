#ifndef __SHM_PUBLISHER_HPP__
#define __SHM_PUBLISHER_HPP__

#include <boost/interprocess/managed_shared_memory.hpp>
#include "ros/ros.h"
#include "std_msgs/UInt64.h"
#include "shm_object.hpp"

namespace shm_transport
{

class Topic;

class Publisher
{
public:
  Publisher() {
  }

  ~Publisher() {
  }

  /**
   * 把 ROS 消息写入共享内存，并通过原始 ROS topic 发送一个 handle。
   *
   * 这里的流程不是“消息完全不走 ROS”，而是：
   * 1. 大消息序列化后放进共享内存；
   * 2. ROS topic 只发送一个 uint64 handle；
   * 3. 订阅者拿到 handle 后再去共享内存里取真实数据。
   *
   * @param msg 任意可序列化的 ROS 消息类型。
   */
  template < class M >
  void publish(const M & msg) const {
    if (!pobj_)
      return;

#define RETRY 2
    // 先计算这条消息序列化后需要多少字节，再尝试在共享内存里申请连续空间。
    uint32_t serlen = ros::serialization::serializationLength(msg);
    ShmMessage * ptr = NULL;
    // bad_alloc 可能发生在共享内存已满、且最老消息尚未被释放时。
    int attempt = 0;
    for (; attempt < RETRY && ptr == NULL; attempt++) {
      try {
        ptr = (ShmMessage *)pobj_->pshm_->allocate(sizeof(ShmMessage) + serlen);
      } catch (boost::interprocess::bad_alloc e) {
        pobj_->plck_->lock();
        // ROS_INFO("bad_alloc happened, releasing the oldest and trying again...");
        ShmMessage * first_msg =
          (ShmMessage *)pobj_->pshm_->get_address_from_handle(pobj_->pmsg_->getFirstHandle());
        // 如果最老消息仍在被订阅者使用，就不能回收，只能放弃这次发布。
        if (first_msg->ref != 0) {
          pobj_->plck_->unlock();
          ROS_WARN("the oldest is in use, abandon this message <%p>...", &msg);
          break;
        }
        // 如果最老消息已经无人引用，就先释放它，再尝试重新申请空间。
        pobj_->pmsg_->releaseFirst(pobj_->pshm_);
        pobj_->plck_->unlock();
      }
    }
    if (ptr) {
      // 把这条消息挂进共享内存消息链表。
      pobj_->plck_->lock();
      ptr->construct(pobj_);
      pobj_->plck_->unlock();

      // 序列化真实 ROS 消息内容到共享内存 data 区域。
      ptr->len = serlen;
      ros::serialization::OStream out(ptr->data, serlen);
      ros::serialization::serialize(out, msg);

      // 发布到 ROS 网络上的并不是真实大消息，而是这块共享内存的 handle。
      std_msgs::UInt64 actual_msg;
      actual_msg.data = pobj_->pshm_->get_handle_from_address(ptr);
      pub_.publish(actual_msg);
    } else if (attempt >= RETRY) {
      ROS_WARN("bad_alloc happened %d times, abandon this message <%p>...", attempt, &msg);
    } else {

    }
#undef RETRY
  }

  void shutdown() {
    pub_.shutdown();
  }

  std::string getTopic() const {
    return pub_.getTopic();
  }

  uint32_t getNumSubscribers() const {
    return pub_.getNumSubscribers();
  }

private:
  /**
   * 只有 Topic::advertise 会调用这个构造函数。
   *
   * @param pub      真正用于在 ROS 网络里发送 handle 的 ros::Publisher。
   * @param topic    逻辑 topic 名称。
   * @param mem_size 为这个 topic 预先申请的共享内存总大小（字节）。
   */
  Publisher(const ros::Publisher & pub, const std::string & topic, uint32_t mem_size)
      : pub_(pub) {
    // change '/' in topic to '_'
    std::string t = topic;
    for (int i = 0; i < t.length(); i++)
      if (t[i] == '/')
        t[i] = '_';
    mng_shm * pshm = new mng_shm(boost::interprocess::open_or_create, t.c_str(), mem_size);
    pobj_ = ShmObjectPtr(new ShmObject(pshm, t));
  }

  // pub_:
  // 实际发送 handle 的 ROS 发布者。
  ros::Publisher pub_;

  // pobj_:
  // 对应共享内存对象的封装。
  ShmObjectPtr   pobj_;

friend class Topic;
};

} // namespace shm_transport

#endif // __SHM_PUBLISHER_HPP__
