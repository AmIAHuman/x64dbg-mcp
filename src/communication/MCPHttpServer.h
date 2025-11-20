/**
 * @file MCPHttpServer.h
 * @brief MCP HTTP Server with SSE (Server-Sent Events) support
 * 
 * 实现 MCP 协议的 HTTP 服务器，使用 SSE 作为传输层。
 * VSCode/Claude Desktop 可以直接通过 HTTP 连接此服务器。
 */

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include <winsock2.h>

namespace MCP {

/**
 * @brief MCP HTTP Server 实现
 * 
 * 提供 HTTP 接口：
 * - GET /sse - SSE 连接，接收来自 MCP 客户端的消息
 * - POST /message - 发送消息到 MCP 客户端
 */
class MCPHttpServer {
public:
    struct Tool {
        std::string name;
        std::string description;
        std::string x64dbg_method;
        std::string params_template;
    };

    MCPHttpServer();
    ~MCPHttpServer();

    // 启动服务器
    bool Start(const std::string& host = "127.0.0.1", int port = 3000);
    
    // 停止服务器
    void Stop();
    
    // 是否正在运行
    bool IsRunning() const { return m_running; }
    
    // 获取服务器地址
    std::string GetAddress() const { return m_host + ":" + std::to_string(m_port); }

private:
    // 初始化工具列表
    void InitializeTools();
    
    // 服务器主循环
    void ServerLoop();
    
    // 处理客户端连接
    void HandleClient(SOCKET clientSocket);
    
    // 处理 HTTP 请求
    void HandleHttpRequest(SOCKET clientSocket, const std::string& request);
    
    // 处理 SSE 连接
    void HandleSSE(SOCKET clientSocket);
    
    // 处理 POST 消息
    void HandlePostMessage(SOCKET clientSocket, const std::string& body);
    
    // 处理 MCP 方法调用
    std::string HandleMCPMethod(const std::string& method, const std::string& requestId, const std::string& body);
    
    // 解析 JSON-RPC 请求
    bool ParseJsonRpcRequest(const std::string& json, std::string& method, std::string& requestId);
    
    // 调用 x64dbg 工具
    std::string CallX64dbgTool(const std::string& toolName, const std::string& arguments);
    
    // JSON 字符串转义
    std::string QuoteJson(const std::string& str);
    
    // 解析 HTTP 请求
    bool ParseHttpRequest(const std::string& request, std::string& method, 
                         std::string& path, std::string& body);
    
    // 发送 HTTP 响应
    void SendHttpResponse(SOCKET socket, int statusCode, const std::string& body,
                         const std::string& contentType = "application/json");
    
    // 发送 SSE 事件
    void SendSSEEvent(SOCKET socket, const std::string& event, const std::string& data);

    std::string m_host;
    int m_port;
    SOCKET m_listenSocket;
    std::atomic<bool> m_running;
    std::thread m_serverThread;
    std::vector<Tool> m_tools;
    int m_requestId;
};

} // namespace MCP
