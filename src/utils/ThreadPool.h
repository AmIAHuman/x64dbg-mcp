#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

namespace MCP {

/**
 * @brief 简单的线程池实现
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * @param numThreads 线程数量，0 表示使用硬件并发数
     */
    explicit ThreadPool(size_t numThreads = 0);
    
    /**
     * @brief 析构函数，等待所有任务完成
     */
    ~ThreadPool();
    
    /**
     * @brief 提交任务到线程池
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param f 要执行的函数
     * @param args 函数参数
     * @return std::future 用于获取结果
     */
    template<typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    /**
     * @brief 获取线程池大小
     */
    size_t GetThreadCount() const { return m_workers.size(); }
    
    /**
     * @brief 获取待处理任务数
     */
    size_t GetQueueSize() const {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        return m_tasks.size();
    }
    
    /**
     * @brief 停止线程池
     */
    void Stop();

private:
    // 工作线程
    std::vector<std::thread> m_workers;
    
    // 任务队列
    std::queue<std::function<void()>> m_tasks;
    
    // 同步
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    
    // 停止标志
    std::atomic<bool> m_stop;
};

// 模板函数实现
template<typename F, typename... Args>
auto ThreadPool::Enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        if (m_stop) {
            throw std::runtime_error("Cannot enqueue on stopped ThreadPool");
        }
        
        m_tasks.emplace([task]() { (*task)(); });
    }
    
    m_condition.notify_one();
    return res;
}

} // namespace MCP
