/**
 * @file MCPHttpServer.cpp
 * @brief MCP HTTP Server implementation
 */

#define WIN32_LEAN_AND_MEAN
#include "MCPHttpServer.h"
#include "../core/Logger.h"
#include "../core/MethodDispatcher.h"
#include "../core/JSONRPCParser.h"
#include "../core/MCPToolRegistry.h"
#include "../core/MCPResourceRegistry.h"
#include "../core/MCPPromptRegistry.h"
#include <ws2tcpip.h>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#pragma comment(lib, "ws2_32.lib")

namespace MCP {

MCPHttpServer::MCPHttpServer() 
    : m_listenSocket(INVALID_SOCKET)
    , m_running(false)
    , m_requestId(0)
{
}

MCPHttpServer::~MCPHttpServer() {
    Stop();
}

bool MCPHttpServer::Start(const std::string& host, int port) {
    if (m_running) {
        Logger::Error("HTTP Server already running");
        return false;
    }

    m_host = host;
    m_port = port;

    // 初始化 WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::Error("WSAStartup failed");
        return false;
    }

    // 创建监听 socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        Logger::Error("Failed to create socket");
        WSACleanup();
        return false;
    }

    // 绑定地址
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        Logger::Error("Failed to bind socket");
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // 开始监听
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Logger::Error("Failed to listen");
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    m_running = true;
    m_serverThread = std::thread(&MCPHttpServer::ServerLoop, this);

    Logger::Info("MCP HTTP Server started on http://" + m_host + ":" + std::to_string(m_port));
    return true;
}

void MCPHttpServer::Stop() {
    if (!m_running) return;

    m_running = false;

    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    WSACleanup();
    Logger::Info("MCP HTTP Server stopped");
}

void MCPHttpServer::ServerLoop() {
    while (m_running) {
        SOCKET clientSocket = accept(m_listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                Logger::Error("Accept failed");
            }
            break;
        }

        // 为每个客户端创建新线程（简化版，实际应该用线程池）
        std::thread([this, clientSocket]() {
            HandleClient(clientSocket);
        }).detach();
    }
}

void MCPHttpServer::HandleClient(SOCKET clientSocket) {
    char buffer[4096];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        std::string request(buffer);
        
        // 检查是否是 SSE 请求
        bool isSSE = (request.find("GET /sse") != std::string::npos);
        
        HandleHttpRequest(clientSocket, request);
        
        // 只有非 SSE 请求才关闭 socket（SSE 需要保持连接）
        if (!isSSE) {
            closesocket(clientSocket);
        }
        // SSE 连接会在 HandleSSE 函数中由客户端断开或服务器停止时关闭
    } else {
        closesocket(clientSocket);
    }
}

void MCPHttpServer::HandleHttpRequest(SOCKET clientSocket, const std::string& request) {
    std::string method, path, body;
    
    if (!ParseHttpRequest(request, method, path, body)) {
        SendHttpResponse(clientSocket, 400, "{\"error\":\"Bad Request\"}");
        return;
    }

    Logger::Debug("HTTP Request: " + method + " " + path);

    if (method == "GET" && path == "/sse") {
        HandleSSE(clientSocket);
    }
    else if (method == "POST" && (path == "/message" || path == "/" || path == "/messages")) {
        // 支持多种 POST 路径：/, /message, /messages
        HandlePostMessage(clientSocket, body);
    }
    else if (method == "GET" && path == "/") {
        // 健康检查
        SendHttpResponse(clientSocket, 200, "{\"status\":\"ok\",\"service\":\"x64dbg-mcp\"}");
    }
    else {
        SendHttpResponse(clientSocket, 404, "{\"error\":\"Not Found\",\"path\":\"" + path + "\"}");
    }
}

void MCPHttpServer::HandleSSE(SOCKET clientSocket) {
    // 发送 SSE 响应头
    std::string headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    
    send(clientSocket, headers.c_str(), headers.length(), 0);

    Logger::Info("SSE connection established, waiting for client messages...");

    // 设置 socket 为非阻塞模式
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    // 读取客户端发送的消息（有些 MCP 客户端通过 SSE 连接发送请求）
    char buffer[4096];
    std::string accumulated;
    int heartbeatCounter = 0;
    
    while (m_running) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            accumulated += buffer;
            
            // 查找完整的 JSON 消息（按行分隔）
            size_t pos;
            while ((pos = accumulated.find('\n')) != std::string::npos) {
                std::string line = accumulated.substr(0, pos);
                accumulated = accumulated.substr(pos + 1);
                
                // 跳过空行
                if (line.empty() || line == "\r") continue;
                
                Logger::Debug("SSE received: " + line);
                
                // 解析并处理 JSON-RPC 请求
                std::string method, requestId;
                if (ParseJsonRpcRequest(line, method, requestId)) {
                    std::string response = HandleMCPMethod(method, requestId, line);
                    SendSSEEvent(clientSocket, "message", response);
                }
            }
        } else if (bytesReceived == 0) {
            // 客户端正常关闭
            Logger::Debug("SSE client disconnected");
            break;
        } else {
            // WSAEWOULDBLOCK 表示没有数据可读，这是正常的
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                Logger::Error("SSE recv error: " + std::to_string(error));
                break;
            }
        }
        
        // 每 15 秒发送一次心跳（保持连接活跃）
        if (++heartbeatCounter >= 150) {  // 150 * 100ms = 15s
            SendSSEEvent(clientSocket, "ping", "{}");
            heartbeatCounter = 0;
        }
        
        // 短暂休眠避免 CPU 占用过高
        Sleep(100);
    }
    
    // SSE 连接结束，关闭 socket
    Logger::Info("Closing SSE connection");
    closesocket(clientSocket);
}

void MCPHttpServer::HandlePostMessage(SOCKET clientSocket, const std::string& body) {
    Logger::Debug("POST body received: " + body);
    
    std::string method, requestId;
    if (!ParseJsonRpcRequest(body, method, requestId)) {
        Logger::Error("Failed to parse JSON-RPC request");
        SendHttpResponse(clientSocket, 400, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
        return;
    }

    Logger::Info("MCP Method: " + method + ", ID: " + requestId);

    std::string response = HandleMCPMethod(method, requestId, body);
    
    // 如果是通知（没有响应），返回 204 No Content
    if (response.empty()) {
        Logger::Debug("No response needed (notification)");
        SendHttpResponse(clientSocket, 204, "");
    } else {
        Logger::Debug("Sending response: " + response);
        SendHttpResponse(clientSocket, 200, response);
    }
}

bool MCPHttpServer::ParseJsonRpcRequest(const std::string& json, 
                                        std::string& method, 
                                        std::string& requestId) {
    // 简单解析（实际应该用 JSON 库）
    size_t methodPos = json.find("\"method\":");
    if (methodPos != std::string::npos) {
        size_t start = json.find("\"", methodPos + 9) + 1;
        size_t end = json.find("\"", start);
        if (end != std::string::npos) {
            method = json.substr(start, end - start);
        }
    }
    
    size_t idPos = json.find("\"id\":");
    if (idPos != std::string::npos) {
        size_t start = idPos + 5;
        // 跳过空格
        while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) {
            start++;
        }
        
        // 检查是否是字符串 ID（以引号开头）
        bool isStringId = (json[start] == '"');
        if (isStringId) {
            start++; // 跳过开始的引号
            size_t end = json.find("\"", start);
            if (end != std::string::npos) {
                requestId = "\"" + json.substr(start, end - start) + "\""; // 保留引号
            }
        } else {
            // 数字 ID 或 null
            size_t end = json.find_first_of(",}\r\n", start);
            if (end != std::string::npos) {
                std::string idValue = json.substr(start, end - start);
                // 去除尾部空格
                while (!idValue.empty() && (idValue.back() == ' ' || idValue.back() == '\t')) {
                    idValue.pop_back();
                }
                requestId = idValue.empty() ? "null" : idValue;
            }
        }
    }
    
    // 如果没有找到 ID，使用 null
    if (requestId.empty()) {
        requestId = "null";
    }

    return !method.empty();
}

std::string MCPHttpServer::HandleMCPMethod(const std::string& method, const std::string& requestId, const std::string& body) {
    if (method == "initialize") {
        Logger::Info("Handling initialize request");
        return "{\"jsonrpc\":\"2.0\",\"id\":" + requestId + 
               ",\"result\":{\"protocolVersion\":\"2024-11-05\","
               "\"capabilities\":{\"tools\":{}},"
               "\"serverInfo\":{\"name\":\"x64dbg-mcp\",\"version\":\"1.0.1\"}}}";
    }
    else if (method == "notifications/initialized") {
        // 这是客户端发的通知，不需要响应
        Logger::Debug("Received initialized notification from client");
        return ""; // 不返回响应
    }
    else if (method == "tools/list") {
        auto& registry = MCPToolRegistry::Instance();
        json result = registry.GenerateToolsListResponse();
        
        Logger::Info("Handling tools/list request, have {} tools", result["tools"].size());
        
        json response = {
            {"jsonrpc", "2.0"},
            {"id", requestId == "null" ? nullptr : json::parse(requestId)},
            {"result", result}
        };
        
        return response.dump();
    }
    else if (method == "tools/call") {
        Logger::Info("Handling tools/call request");
        
        try {
            // 解析请求 body
            json requestJson = json::parse(body);
            
            if (!requestJson.contains("params")) {
                return json({
                    {"jsonrpc", "2.0"},
                    {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                    {"error", {
                        {"code", -32602},
                        {"message", "Invalid params: missing params object"}
                    }}
                }).dump();
            }
            
            json params = requestJson["params"];
            
            if (!params.contains("name") || !params["name"].is_string()) {
                return json({
                    {"jsonrpc", "2.0"},
                    {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                    {"error", {
                        {"code", -32602},
                        {"message", "Invalid params: missing or invalid tool name"}
                    }}
                }).dump();
            }
            
            std::string toolName = params["name"].get<std::string>();
            json arguments = params.value("arguments", json::object());
            
            Logger::Info("Calling tool: {} with args: {}", toolName, arguments.dump());
            
            // 调用工具
            std::string result = CallMCPTool(toolName, arguments);
            
            return json({
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"result", {
                    {"content", json::array({
                        {
                            {"type", "text"},
                            {"text", result}
                        }
                    })}
                }}
            }).dump();
            
        } catch (const json::exception& e) {
            Logger::Error("JSON parse error in tools/call: {}", e.what());
            return json({
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"error", {
                    {"code", -32700},
                    {"message", std::string("Parse error: ") + e.what()}
                }}
            }).dump();
        } catch (const std::exception& e) {
            Logger::Error("Exception in tools/call: {}", e.what());
            return json({
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"error", {
                    {"code", -32603},
                    {"message", std::string("Internal error: ") + e.what()}
                }}
            }).dump();
        }
    }
    // Resources API
    else if (method == "resources/list") {
        auto& registry = MCPResourceRegistry::Instance();
        json result = registry.GenerateResourcesListResponse();
        
        Logger::Info("Handling resources/list request, have {} resources", result["resources"].size());
        
        json response = {
            {"jsonrpc", "2.0"},
            {"id", requestId == "null" ? nullptr : json::parse(requestId)},
            {"result", result}
        };
        
        return response.dump();
    }
    else if (method == "resources/templates/list") {
        auto& registry = MCPResourceRegistry::Instance();
        json result = registry.GenerateTemplatesListResponse();
        
        Logger::Info("Handling resources/templates/list request, have {} templates", 
                     result["resourceTemplates"].size());
        
        json response = {
            {"jsonrpc", "2.0"},
            {"id", requestId == "null" ? nullptr : json::parse(requestId)},
            {"result", result}
        };
        
        return response.dump();
    }
    else if (method == "resources/read") {
        Logger::Info("Handling resources/read request");
        
        try {
            json requestJson = json::parse(body);
            
            if (!requestJson.contains("params") || !requestJson["params"].contains("uri")) {
                return json({
                    {"jsonrpc", "2.0"},
                    {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                    {"error", {
                        {"code", -32602},
                        {"message", "Invalid params: missing uri"}
                    }}
                }).dump();
            }
            
            std::string uri = requestJson["params"]["uri"].get<std::string>();
            Logger::Info("Reading resource: {}", uri);
            
            auto& registry = MCPResourceRegistry::Instance();
            MCPResourceContent content = registry.ReadResource(uri);
            
            json response = {
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"result", {
                    {"contents", json::array({content.ToMCPFormat()})}
                }}
            };
            
            return response.dump();
            
        } catch (const std::exception& e) {
            Logger::Error("Exception in resources/read: {}", e.what());
            return json({
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"error", {
                    {"code", -32603},
                    {"message", std::string("Internal error: ") + e.what()}
                }}
            }).dump();
        }
    }
    // Prompts API
    else if (method == "prompts/list") {
        auto& registry = MCPPromptRegistry::Instance();
        json result = registry.GeneratePromptsListResponse();
        
        Logger::Info("Handling prompts/list request, have {} prompts", result["prompts"].size());
        
        json response = {
            {"jsonrpc", "2.0"},
            {"id", requestId == "null" ? nullptr : json::parse(requestId)},
            {"result", result}
        };
        
        return response.dump();
    }
    else if (method == "prompts/get") {
        Logger::Info("Handling prompts/get request");
        
        try {
            json requestJson = json::parse(body);
            
            if (!requestJson.contains("params") || !requestJson["params"].contains("name")) {
                return json({
                    {"jsonrpc", "2.0"},
                    {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                    {"error", {
                        {"code", -32602},
                        {"message", "Invalid params: missing name"}
                    }}
                }).dump();
            }
            
            std::string promptName = requestJson["params"]["name"].get<std::string>();
            json arguments = requestJson["params"].value("arguments", json::object());
            
            Logger::Info("Getting prompt: {} with args: {}", promptName, arguments.dump());
            
            auto& registry = MCPPromptRegistry::Instance();
            MCPPromptResult promptResult = registry.GetPrompt(promptName, arguments);
            
            json response = {
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"result", promptResult.ToMCPFormat()}
            };
            
            return response.dump();
            
        } catch (const std::exception& e) {
            Logger::Error("Exception in prompts/get: {}", e.what());
            return json({
                {"jsonrpc", "2.0"},
                {"id", requestId == "null" ? nullptr : json::parse(requestId)},
                {"error", {
                    {"code", -32603},
                    {"message", std::string("Internal error: ") + e.what()}
                }}
            }).dump();
        }
    }
    else {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + requestId + 
               ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
    }
}

bool MCPHttpServer::ParseHttpRequest(const std::string& request, 
                                     std::string& method, 
                                     std::string& path, 
                                     std::string& body) {
    // 解析请求行
    size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) return false;
    
    method = request.substr(0, firstSpace);
    
    size_t secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) return false;
    
    path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    
    // 提取 body（在 \r\n\r\n 之后）
    size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        body = request.substr(bodyStart + 4);
    }
    
    return true;
}

void MCPHttpServer::SendHttpResponse(SOCKET socket, int statusCode, 
                                     const std::string& body,
                                     const std::string& contentType) {
    std::string statusText = (statusCode == 200) ? "OK" : 
                            (statusCode == 404) ? "Not Found" : "Bad Request";
    
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.length() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    
    std::string responseStr = response.str();
    send(socket, responseStr.c_str(), responseStr.length(), 0);
}

void MCPHttpServer::SendSSEEvent(SOCKET socket, const std::string& event, const std::string& data) {
    std::ostringstream sse;
    sse << "event: " << event << "\r\n"
        << "data: " << data << "\r\n"
        << "\r\n";
    
    std::string sseStr = sse.str();
    send(socket, sseStr.c_str(), sseStr.length(), 0);
}

std::string MCPHttpServer::CallMCPTool(const std::string& toolName, const nlohmann::json& arguments) {
    auto& registry = MCPToolRegistry::Instance();
    
    // 查找工具定义
    auto toolOpt = registry.FindTool(toolName);
    if (!toolOpt.has_value()) {
        Logger::Error("Tool not found: {}", toolName);
        return "Error: Tool '" + toolName + "' not found";
    }
    
    const MCPToolDefinition& tool = toolOpt.value();
    
    // 验证参数
    std::string validationError = tool.ValidateArguments(arguments);
    if (!validationError.empty()) {
        Logger::Error("Tool argument validation failed: {}", validationError);
        return "Error: " + validationError;
    }
    
    Logger::Info("Executing tool: {} -> method: {}", toolName, tool.jsonrpcMethod);
    
    // 构建 JSON-RPC 请求发送给 MethodDispatcher
    try {
        auto& dispatcher = MethodDispatcher::Instance();
        
        // 构建请求
        JSONRPCRequest request;
        request.jsonrpc = "2.0";
        request.method = tool.jsonrpcMethod;
        request.id = ++m_requestId;
        request.params = tool.TransformToJSONRPC(arguments);
        
        // 调用分发器
        JSONRPCResponse response = dispatcher.Dispatch(request);
        
        if (response.error.has_value()) {
            Logger::Error("Tool execution failed: {}", response.error->message);
            return "Error: " + response.error->message;
        }
        
        // 返回格式化的结果
        return response.result.dump(2);  // 美化输出
        
    } catch (const std::exception& e) {
        Logger::Error("Exception calling tool: {}", e.what());
        return "Error: " + std::string(e.what());
    }
}


} // namespace MCP
