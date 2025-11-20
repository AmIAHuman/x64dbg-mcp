#pragma once
#include "../communication/TCPServer.h"
#include "../communication/HeartbeatMonitor.h"
#include "../communication/ConnectionManager.h"
#include "MethodDispatcher.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

using json = nlohmann::json;

namespace MCP {

/**
 * @brief MCP 服务器管理器（单例）
 * 整合所有组件，提供统一的服务器接口
 */
class ServerManager {
public:
    /**
     * @brief 获取单例实例
     */
    static ServerManager& Instance();
    
    /**
     * @brief 初始化服务器
     * @param configPath 配置文件路径
     * @return 是否成功
     */
    bool Initialize(const std::string& configPath);
    
    /**
     * @brief 启动服务器
     * @return 是否成功
     */
    bool Start();
    
    /**
     * @brief 停止服务器
     */
    void Stop();
    
    /**
     * @brief 服务器是否正在运行
     */
    bool IsRunning() const;
    
    /**
     * @brief 关闭服务器（清理资源）
     */
    void Shutdown();
    
    /**
     * @brief 发送通知给所有客户端
     * @param method 方法名
     * @param params 参数
     */
    void SendNotification(const std::string& method, const json& params);

private:
    ServerManager() = default;
    ~ServerManager();
    ServerManager(const ServerManager&) = delete;
    ServerManager& operator=(const ServerManager&) = delete;
    
    void OnMessageReceived(ClientId clientId, const std::string& message);
    void OnConnectionChanged(ClientId clientId, bool connected);
    void ProcessRequest(ClientId clientId, const std::string& message);
    
    std::unique_ptr<TCPServer> m_tcpServer;
    std::unique_ptr<HeartbeatMonitor> m_heartbeatMonitor;
    
    bool m_initialized;
    bool m_running;
};

} // namespace MCP
