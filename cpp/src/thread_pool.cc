#include "thread_pool.h"
#include "logger.h"

// ============================================================
// 构造函数
// ============================================================
ThreadPool::ThreadPool(size_t threads)
    : running_(false),
      stop_(false) {
    workers_.reserve(threads);
}

// ============================================================
// 析构函数
// ============================================================
ThreadPool::~ThreadPool() {
    Stop();
}

// ============================================================
// 启动线程池
// ============================================================
bool ThreadPool::Start() {
    if (running_) {
        return true;
    }

    running_ = true;
    stop_ = false;

    // 创建工作线程
    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back(&ThreadPool::WorkerThread, this);
    }

    LOG_INFO("ThreadPool started with %zu workers", workers_.size());
    return true;
}

// ============================================================
// 停止线程池
// ============================================================
void ThreadPool::Stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }

    // 通知所有等待线程
    condition_.notify_all();

    // 等待所有线程完成
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
    running_ = false;

    LOG_INFO("ThreadPool stopped");
}

// ============================================================
// 获取任务队列大小
// ============================================================
size_t ThreadPool::GetTaskCount() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

// ============================================================
// 工作线程函数
// ============================================================
void ThreadPool::WorkerThread() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // 等待任务或停止信号
            condition_.wait(lock, [this]() {
                return stop_ || !tasks_.empty();
            });

            // 如果停止且没有任务，退出
            if (stop_ && tasks_.empty()) {
                return;
            }

            // 获取任务
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        // 执行任务
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("Task exception: %s", e.what());
        } catch (...) {
            LOG_ERROR("Unknown task exception");
        }
    }
}
