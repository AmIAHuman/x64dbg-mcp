#include "ServerManager.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "PermissionChecker.h"
#include "JSONRPCParser.h"
#include "ResponseBuilder.h"
#include "Exceptions.h"

namespace MCP {

ServerManager& ServerManager::Instance() {
    static ServerManager instance;
    return instance;
}

ServerManager::~ServerManager() {
    Shutdown();
}

bool ServerManager::Initialize(const std::string& configPath) {
    if (m_initialized) {
        Logger::Warning("ServerManager already initialized");
        return true;
    }
    
    Logger::Info("Initializing MCP Server...");
    
    // 加载配置
    auto& config = ConfigManager::Instance();
    if (!config.Load(configPath)) {
        Logger::Error("Failed to load configuration from: {}", configPath);
        return false;
    }
    
    // 初始化日志系统
    if (config.IsLoggingEnabled()) {
        std::string logFile = config.GetLogFile();
        std::string logLevelStr = config.GetLogLevel();
        
        LogLevel logLevel = LogLevel::Info;
        if (logLevelStr == "trace") logLevel = LogLevel::Trace;
        else if (logLevelStr == "debug") logLevel = LogLevel::Debug;
        else if (logLevelStr == "warning") logLevel = LogLevel::Warning;
        else if (logLevelStr == "error") logLevel = LogLevel::Error;
        
        if (!Logger::Initialize(logFile, logLevel, true)) {
            return false;
        }
    }
    
    // 初始化权限检查器
    PermissionChecker::Instance().Initialize();
    
    // 注册默认方法
    MethodDispatcher::Instance().RegisterDefaultMethods();
    
    // 创建 TCP 服务器
    m_tcpServer = std::make_unique<TCPServer>();
    
    // 设置回调
    m_tcpServer->SetMessageHandler(
        std::bind(&ServerManager::OnMessageReceived, this, 
                 std::placeholders::_1, std::placeholders::_2)
    );
    
    m_tcpServer->SetConnectionHandler(
        std::bind(&ServerManager::OnConnectionChanged, this,
                 std::placeholders::_1, std::placeholders::_2)
    );
    
    m_initialized = true;
    Logger::Info("MCP Server initialized successfully");
    return true;
}

bool ServerManager::Start() {
    if (!m_initialized) {
        Logger::Error("ServerManager not initialized");
        return false;
    }
    
    if (m_running) {
        Logger::Warning("ServerManager already running");
        return true;
    }
    
    Logger::Info("Starting MCP Server...");
    
    auto& config = ConfigManager::Instance();
    std::string address = config.GetServerAddress();
    uint16_t port = config.GetServerPort();
    
    // 启动 TCP 服务器
    if (!m_tcpServer->Start(address, port)) {
        Logger::Error("Failed to start TCP server");
        return false;
    }
    
    // 启动心跳监控（如果启用）
    if (config.Get<bool>("features.enable_heartbeat", true)) {
        uint32_t interval = config.Get<int>("features.heartbeat_interval_seconds", 30);
        m_heartbeatMonitor = std::make_unique<HeartbeatMonitor>(
            const_cast<ConnectionManager&>(
                *reinterpret_cast<const ConnectionManager*>(&m_tcpServer)
            )
        );
        // 注意：这里需要访问 TCPServer 内部的 ConnectionManager
        // 在实际实现中，应该提供适当的接口
    }
    
    m_running = true;
    Logger::Info("MCP Server started on {}:{}", address, port);
    return true;
}

void ServerManager::Stop() {
    if (!m_running) {
        return;
    }
    
    Logger::Info("Stopping MCP Server...");
    
    // 停止心跳监控
    if (m_heartbeatMonitor) {
        m_heartbeatMonitor->Stop();
        m_heartbeatMonitor.reset();
    }
    
    // 停止 TCP 服务器
    if (m_tcpServer) {
        m_tcpServer->Stop();
    }
    
    m_running = false;
    Logger::Info("MCP Server stopped");
}

bool ServerManager::IsRunning() const {
    return m_running;
}

void ServerManager::Shutdown() {
    Stop();
    
    if (m_tcpServer) {
        m_tcpServer.reset();
    }
    
    Logger::Shutdown();
    m_initialized = false;
}

void ServerManager::SendNotification(const std::string& method, const json& params) {
    if (!m_running || !m_tcpServer) {
        return;
    }
    
    std::string notification = ResponseBuilder::CreateNotification(method, params);
    m_tcpServer->BroadcastMessage(notification);
}

void ServerManager::OnMessageReceived(ClientId clientId, const std::string& message) {
    Logger::Trace("Received message from client {}: {}", clientId, message);
    ProcessRequest(clientId, message);
}

void ServerManager::OnConnectionChanged(ClientId clientId, bool connected) {
    if (connected) {
        Logger::Info("Client {} connected", clientId);
    } else {
        Logger::Info("Client {} disconnected", clientId);
    }
}

void ServerManager::ProcessRequest(ClientId clientId, const std::string& message) {
    try {
        // 解析请求
        JSONRPCRequest request = JSONRPCParser::ParseRequest(message);
        
        // 分发请求
        JSONRPCResponse response = MethodDispatcher::Instance().Dispatch(request);
        
        // 发送响应（通知消息不需要响应）
        if (!request.IsNotification()) {
            std::string responseStr = ResponseBuilder::Serialize(response);
            m_tcpServer->SendMessage(clientId, responseStr);
        }
        
    } catch (const ::MCP::ParseErrorException&) {
        Logger::Error("Parse error from client {}", clientId);
        JSONRPCResponse errorResponse = ResponseBuilder::CreateErrorResponse(
            RequestId(), -32700, "Parse error"
        );
        std::string responseStr = ResponseBuilder::Serialize(errorResponse);
        m_tcpServer->SendMessage(clientId, responseStr);
        
    } catch (const ::std::exception&) {
        Logger::Error("Exception processing request from client {}", clientId);
    }
}

} // namespace MCP
