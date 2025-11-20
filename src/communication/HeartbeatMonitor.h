#pragma once
#include "ConnectionManager.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace MCP {

/**
 * @brief 心跳监控器
 */
class HeartbeatMonitor {
public:
    HeartbeatMonitor(ConnectionManager& connectionManager);
    ~HeartbeatMonitor();
    
    /**
     * @brief 启动心跳监控
     * @param intervalSeconds 心跳间隔（秒）
     */
    void Start(uint32_t intervalSeconds = 30);
    
    /**
     * @brief 停止心跳监控
     */
    void Stop();
    
    /**
     * @brief 是否正在运行
     */
    bool IsRunning() const { return m_running; }

private:
    void MonitorThread();
    void SendHeartbeat();
    
    ConnectionManager& m_connectionManager;
    std::thread m_thread;
    std::atomic<bool> m_running;
    uint32_t m_intervalSeconds;
};

} // namespace MCP
