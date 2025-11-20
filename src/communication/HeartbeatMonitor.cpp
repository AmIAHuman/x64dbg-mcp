#include "HeartbeatMonitor.h"
#include "../core/Logger.h"
#include "../core/ResponseBuilder.h"

namespace MCP {

HeartbeatMonitor::HeartbeatMonitor(ConnectionManager& connectionManager)
    : m_connectionManager(connectionManager)
    , m_running(false)
    , m_intervalSeconds(30)
{
}

HeartbeatMonitor::~HeartbeatMonitor() {
    Stop();
}

void HeartbeatMonitor::Start(uint32_t intervalSeconds) {
    if (m_running) {
        Logger::Warning("HeartbeatMonitor is already running");
        return;
    }
    
    m_intervalSeconds = intervalSeconds;
    m_running = true;
    m_thread = std::thread(&HeartbeatMonitor::MonitorThread, this);
    
    Logger::Info("HeartbeatMonitor started with interval {} seconds", intervalSeconds);
}

void HeartbeatMonitor::Stop() {
    if (!m_running) {
        return;
    }
    
    Logger::Info("Stopping HeartbeatMonitor...");
    
    m_running = false;
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    Logger::Info("HeartbeatMonitor stopped");
}

void HeartbeatMonitor::MonitorThread() {
    Logger::Debug("Heartbeat monitor thread started");
    
    while (m_running) {
        // 等待指定间隔
        auto start = std::chrono::steady_clock::now();
        
        while (m_running) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
            
            if (elapsed >= m_intervalSeconds) {
                break;
            }
            
            // 短暂休眠，避免忙等待
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!m_running) {
            break;
        }
        
        // 发送心跳
        SendHeartbeat();
    }
    
    Logger::Debug("Heartbeat monitor thread stopped");
}

void HeartbeatMonitor::SendHeartbeat() {
    if (m_connectionManager.GetClientCount() == 0) {
        return;
    }
    
    // 创建心跳通知消息
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    json params = {
        {"timestamp", timestamp},
        {"type", "ping"}
    };
    
    std::string notification = ResponseBuilder::CreateNotification("system.heartbeat", params);
    
    Logger::Trace("Sending heartbeat to {} clients", m_connectionManager.GetClientCount());
    m_connectionManager.BroadcastMessage(notification);
}

} // namespace MCP
