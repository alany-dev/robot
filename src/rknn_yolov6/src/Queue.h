#ifndef Queue_H
#define Queue_H

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

/**
 * 为低时延视觉链路定制的短队列。
 *
 * 和普通“尽量不丢帧”的队列不同，这里明确偏向 freshness（最新性）：
 * - 队列长度达到 2 时，直接丢弃最旧帧；
 * - 这样在推理或后处理暂时变慢时，系统不会无限积压旧画面；
 * - 机器人控制拿到的总是更接近当前时刻的结果。
 */
template<typename T>
class Queue
{
public:
    /**
     * 推入一条数据。
     *
     * @param value 要入队的数据对象。
     */
    void push(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 队列里只保留 2 个元素：
        // 如果上游持续更快，先把最旧的那条扔掉，再放入最新数据。
        if(queue_.size()>=2)
            queue_.pop();
        
        queue_.push(value);
        cond_var_.notify_one();
    }

    /**
     * 阻塞等待并返回一条数据。
     */
    T wait_and_pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(queue_.empty())
        {
            cond_var_.wait(lock);
        }

        auto value = queue_.front();
        queue_.pop();
        return value;
    }

    /**
     * 阻塞等待并通过引用返回一条数据。
     *
     * @param value 输出参数，用于承接出队元素。
     */
    void wait_and_pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(queue_.empty())
        {
            cond_var_.wait(lock);
        }
        value = queue_.front();
        queue_.pop();
    }

private:
    // queue_:
    // 存放待处理元素的 FIFO 队列，但长度会被 push() 主动限制。
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
};

#endif
