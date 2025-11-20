#include "ThreadPool.h"

namespace MCP {

ThreadPool::ThreadPool(size_t numThreads)
    : m_stop(false)
{
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 2; // 默认值
        }
    }
    
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_condition.wait(lock, [this] {
                        return m_stop || !m_tasks.empty();
                    });
                    
                    if (m_stop && m_tasks.empty()) {
                        return;
                    }
                    
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    Stop();
}

void ThreadPool::Stop() {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    
    m_condition.notify_all();
    
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace MCP
