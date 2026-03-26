# ROS 高带宽数据共享内存低时延通信优化

## 1. 这个功能到底解决什么问题

标准 ROS1 话题在传输大图像、雷达等高带宽数据时，瓶颈通常不是算子本身，而是多次序列化、拷贝和进程间传输带来的时延与 CPU 开销。本仓库的共享内存模块没有试图“替代 ROS”，而是采用更实用的混合方案：真正的大数据写入共享内存，ROS 话题只发布一个句柄。这样既保留了 ROS 的解耦和发现机制，又把大对象搬运的成本压到最低。

## 2. 先看哪些文件

建议按下面顺序阅读：

- `src/shm_transport/include/shm_transport/shm_object.hpp`
- `src/shm_transport/include/shm_transport/shm_topic.hpp`
- `src/shm_transport/include/shm_transport/shm_publisher.hpp`
- `src/shm_transport/include/shm_transport/shm_subscriber.hpp`
- `src/shm_transport/test/shm_talker.cpp`
- `src/shm_transport/test/shm_listener.cpp`
- `src/img_decode/src/shm_img_decode.cpp`
- `src/img_decode/src/shm_img_decode_test.cpp`
- `src/ydlidar/src/shm_ydlidar_node.cpp`

## 3. 启动路径 / 数据流 / 控制流

主链路可以概括成四步：

1. 发布端把 ROS 消息序列化后写入共享内存对象。
2. 发布端通过普通 ROS 话题发布一个 `std_msgs::UInt64` 句柄。
3. 订阅端收到句柄后，定位共享内存中的真实消息块并反序列化。
4. 订阅端处理完成后递减引用计数，允许共享内存复用或回收。

典型验证命令如下：

```bash
catkin_make
source devel/setup.bash
rosrun shm_transport shm_talker
rosrun shm_transport shm_listener
```

图像链路里的共享内存版本可参考：

```bash
rosrun img_decode shm_img_decode
rosrun img_decode shm_img_decode_test _image_topic:=/camera/image_raw
```

## 4. 亮点实现细节

### 4.1 混合传输而不是全盘重写

最值得学的点是架构取舍。作者没有另起一套发现、订阅、同步协议，而是把“控制面”继续留给 ROS，把“数据面”挪到共享内存。这个设计让共享内存模块很容易嵌入现有 ROS 节点，也方便逐步替换热点链路。

### 4.2 `ShmObject` 不只是缓存区，而是带生命周期管理的消息池

`ShmObject` 内部维护引用计数、锁，以及共享内存消息块的链表关系。它不是简单地 `malloc` 一段内存，而是在共享内存区域里维护一个可回收的消息池。这个设计决定了它可以长期运行，而不是只适合 demo。

### 4.3 内存紧张时的退化策略很务实

发布端遇到 `bad_alloc` 时，不是立刻崩溃，也不是盲目扩大共享内存，而是尝试回收最旧且引用计数为 0 的消息。如果回收失败，当前消息直接丢弃。这个策略体现的是实时系统思路：优先保证系统持续运行，其次才是绝对不丢包。

### 4.4 订阅端按需打开共享内存

订阅端不是开机就把所有资源全部拉起，而是在收到句柄后按需解析共享内存对象。图像解码模块里的 `shm_img_decode.cpp` 更进一步，根据下游订阅者数量决定是否继续订阅上游压缩图像，这属于非常典型的懒订阅优化，能显著减少空转解码。

### 4.5 共享内存不仅用于 demo，而是被接入真实链路

如果只看 `shm_talker/shm_listener`，很容易误以为这只是实验代码。实际上作者把它接进了图像解码链路和雷达节点，说明这套机制已经被当作真实系统优化手段使用，而不是孤立样例。

## 5. 调试与验证方法

- 先用 `shm_talker/shm_listener` 验证最小链路，确认共享内存句柄收发正常。
- 再用 `shm_img_decode_test.cpp` 观察图像链路的延迟和帧率。
- 对比共享内存版与普通 ROS 版 talker/listener，重点看 CPU 占用、平均延迟、峰值延迟。
- 如果怀疑共享内存回收异常，优先检查引用计数是否有泄漏，尤其是订阅端异常退出的场景。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先只读 `shm_publisher.hpp`、`shm_subscriber.hpp` 和测试程序，建立“ROS 只传句柄”的总体认知。

### 2 小时

把 `ShmObject` 和消息池回收策略读明白，再顺着 `shm_img_decode.cpp` 看它如何接入真实视觉链路。到这一步，你应该能解释为什么共享内存优化对图像特别有效。

### 1 天

自己给图像或雷达链路增加时间戳统计，测量普通 ROS 版与共享内存版的端到端差异；同时检查异常路径，比如共享内存不足、订阅端慢处理、下游无人订阅时的资源占用。

## 7. 你学完后应该能回答什么问题

- 为什么这里没有抛弃 ROS，而是保留 ROS 话题发布句柄。
- `ShmObject` 如何同时解决存储、引用计数和回收问题。
- 当共享内存不足时，系统为什么优先选择丢旧消息或丢当前消息，而不是阻塞。
- 图像链路里的懒订阅为什么是低时延系统常见优化。
- 这套方案为什么比“简单加大队列”更适合实时机器人系统。
