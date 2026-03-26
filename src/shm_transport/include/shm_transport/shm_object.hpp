#ifndef __SHM_OBJECT_HPP__
#define __SHM_OBJECT_HPP__

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/atomic/atomic.hpp>

namespace shm_transport
{

// some typedef for short
typedef boost::atomic< uint32_t > atomic_uint32_t;
typedef boost::interprocess::managed_shared_memory mng_shm;
typedef boost::shared_ptr< boost::interprocess::managed_shared_memory > mng_shm_ptr;
typedef boost::interprocess::interprocess_mutex ipc_mutex;

// MsgListHead:
// 共享内存里维护的一条双向链表，用来串起当前 topic 上尚未回收的消息块。
// 发布端会把新消息追加到链表尾部；当共享内存不够时，会优先尝试释放链表头最老的消息。
class MsgListHead
{
public:
  MsgListHead() : next(0), prev(0) { }
  ~MsgListHead() { }

    // 把新节点插到链表尾部。
    void addLast(MsgListHead * lc, const mng_shm_ptr & pshm) {
    long hc = pshm->get_handle_from_address(lc);
    long hn = pshm->get_handle_from_address(this), hp = this->prev;
    MsgListHead * ln = this, * lp = (MsgListHead *)pshm->get_address_from_handle(hp);
    lc->next = hn;
    lc->prev = hp;
    lp->next = hc;
    ln->prev = hc;
  }

    // 把某个节点从链表中摘掉，但不负责释放其内存。
    void remove(MsgListHead * lc, const mng_shm_ptr & pshm) {
    long hc = pshm->get_handle_from_address(lc);
    long hn = lc->next, hp = lc->prev;
    MsgListHead * ln = (MsgListHead *)pshm->get_address_from_handle(hn);
    MsgListHead * lp = (MsgListHead *)pshm->get_address_from_handle(hp);
    lp->next = hn;
    ln->prev = hp;
  }

    // 释放链表头部最老的一条消息。
    // 注意：调用前必须确保这条消息没有任何订阅者仍在使用。
    void releaseFirst(const mng_shm_ptr & pshm) {
    long hc = next;
    MsgListHead * lc = (MsgListHead *)pshm->get_address_from_handle(hc);
    if (lc == this)
      return;
    long hn = lc->next, hp = lc->prev;
    MsgListHead * ln = (MsgListHead *)pshm->get_address_from_handle(hn), * lp = this;
    lp->next = hn;
    ln->prev = hp;
    pshm->deallocate(lc);
  }

  long getFirstHandle() {
    return next;
  }

public:
  long next;
  long prev;
};

class ShmObject
{
public:
  /**
   * 打开或创建某个 topic 对应的共享内存区域。
   *
   * @param pshm 共享内存管理器。
   * @param name 共享内存对象名。这里通常由 topic 名把 '/' 替换成 '_' 得到。
   */
  ShmObject(mng_shm * pshm, std::string name)
      : pshm_(pshm), name_(name) {
    pref_ = pshm_->find_or_construct< atomic_uint32_t >("ref")(0);
    plck_ = pshm_->find_or_construct< ipc_mutex >("lck")();
    pmsg_ = pshm_->find_or_construct< MsgListHead >("lst")();

    pref_->fetch_add(1, boost::memory_order_relaxed);
    if (pmsg_->next == 0) {
      long handle = pshm_->get_handle_from_address(pmsg_);
      pmsg_->next = handle;
      pmsg_->prev = handle;
    }
  }

  ~ShmObject() {
    // 当最后一个发布者 / 订阅者退出时，顺带把这块共享内存对象清掉。
    if (pref_->fetch_sub(1, boost::memory_order_relaxed) == 1) {
      boost::interprocess::shared_memory_object::remove(name_.c_str());
      //printf("shm file <%s> removed\n", name_.c_str());
    }
  }

public:
  // pshm_:
  // 指向当前 topic 对应的 managed_shared_memory 管理器。
  mng_shm_ptr pshm_;

  // name_:
  // 共享内存对象名称。
  std::string name_;

  // pref_:
  // 共享内存里维护的引用计数，统计当前仍连接这个 topic 的发布者 + 订阅者数量。
  atomic_uint32_t * pref_;

  // plck_:
  // 共享内存里的互斥锁，用于保护连接和链表修改。
  ipc_mutex * plck_;

  // pmsg_:
  // 共享内存里那条“未回收消息链表”的头结点。
  MsgListHead * pmsg_;
};
typedef boost::shared_ptr< ShmObject > ShmObjectPtr;

// ShmMessage:
// 真正存放在共享内存里的消息块。消息头后面紧跟 data[0] 柔性数组，
// 其内容是原始 ROS 消息序列化后的字节流。
class ShmMessage
{
public:
  // 发布端调用：
  // 1. 把自己插进“消息链表”尾部；
  // 2. 把引用计数置 0，等待订阅者 take()。
  void construct(const ShmObjectPtr & so) {
    // insert into message list
    so->pmsg_->addLast(&lst, so->pshm_);
    // set reference count
    ref = 0;
  }

  // 订阅端拿到 handle 后先调用 take() 增加引用计数。
  void take() {
    ref.fetch_add(1, boost::memory_order_relaxed);
  }

  // 订阅端反序列化完成后调用 release() 减少引用计数。
  // 注意：这里只减引用，不直接销毁；真正的销毁仍由发布端决定。
  void release() {
    // just decrease the ref counter.
    // do not deallocate here, publisher will do that
    ref.fetch_sub(1, boost::memory_order_relaxed);
  }

public:
  // lst:
  // 把当前消息挂进共享内存消息链表所需的链表节点。
  MsgListHead     lst;

  // ref:
  // 当前仍在使用这条消息的订阅者数量。
  atomic_uint32_t ref;

  // len:
  // data 区域里真实的序列化字节数。
  uint32_t        len;

  // data:
  // 柔性数组起点，后面实际跟着 len 字节的 ROS 序列化消息数据。
  uint8_t         data[0];
};

} // namespace shm_transport

#endif // __SHM_PUBLISHER_HPP__
