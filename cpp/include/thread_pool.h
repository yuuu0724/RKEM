#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "common.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <functional>

// ============================================================
// 线程池
// ============================================================
// 管理多个工作线程
// 支持任务提交和结果获取
// 支持动态调整线程数量
// ============================================================

class ThreadPool {
public:
    // 构造函数
    // threads: 线程数量
    ThreadPool(size_t threads = 4);

    // 析构函数
    ~ThreadPool();

    // 启动线程池
    bool Start();

    // 停止线程池
    void Stop();

    // 提交任务
    // 返回 std::future 用于获取结果
    template<class F, class... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // 获取任务队列大小
    size_t GetTaskCount() const;

    // 获取线程数量
    size_t GetThreadCount() const { return workers_.size(); }

    // 检查是否正在运行
    bool IsRunning() const { return running_; }

private:
    // 工作线程函数
    void WorkerThread();

private:
    // 工作线程
    std::vector<std::thread> workers_;

    // 任务队列
    std::queue<std::function<void()>> tasks_;

    // 同步原语
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_;
};

// ============================================================
// 模板函数实现
// ============================================================
template<class F, class... Args>
auto ThreadPool::Submit(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 如果线程池已停止，抛出异常
        if (stop_) {
            throw std::runtime_error("Submit on stopped ThreadPool");
        }

        // 将任务添加到队列
        tasks_.emplace([task]() { (*task)(); });
    }

    // 通知一个等待线程
    condition_.notify_one();

    return res;
}

#endif // THREAD_POOL_H
