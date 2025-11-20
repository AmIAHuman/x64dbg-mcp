#include "TCPServer.h"
#include "../core/Logger.h"
#include "../core/ConfigManager.h"

#pragma comment(lib, "ws2_32.lib")

namespace MCP {

bool TCPServer::s_wsaInitialized = false;
std::mutex TCPServer::s_wsaMutex;

TCPServer::TCPServer()
    : m_listenSocket(INVALID_SOCKET)
    , m_running(false)
    , m_port(0)
{
    InitializeWSA();
}

TCPServer::~TCPServer() {
    Stop();
}

bool TCPServer::Start(const std::string& address, uint16_t port) {
    if (m_running) {
        Logger::Warning("Server is already running");
        return false;
    }
    
    m_address = address;
    m_port = port;
    
    // 创建监听 socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        Logger::Error("Failed to create socket: {}", WSAGetLastError());
        return false;
    }
    
    // 设置 socket 选项（允许地址重用）
    int optval = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, 
              reinterpret_cast<const char*>(&optval), sizeof(optval));
    
    // 绑定地址
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);
    
    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), 
            sizeof(serverAddr)) == SOCKET_ERROR) {
        Logger::Error("Failed to bind to {}:{} - {}", address, port, WSAGetLastError());
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }
    
    // 开始监听
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Logger::Error("Failed to listen: {}", WSAGetLastError());
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }
    
    m_running = true;
    
    // 启动接受线程
    m_acceptThread = std::thread(&TCPServer::AcceptThread, this);
    
    Logger::Info("TCP Server started on {}:{}", address, port);
    return true;
}

void TCPServer::Stop() {
    if (!m_running) {
        return;
    }
    
    Logger::Info("Stopping TCP Server...");
    
    m_running = false;
    
    // 关闭监听 socket
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    
    // 断开所有客户端
    m_connectionManager.DisconnectAll();
    
    // 等待接受线程结束
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    
    // 等待所有客户端线程结束
    for (auto& thread : m_clientThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_clientThreads.clear();
    
    Logger::Info("TCP Server stopped");
}

void TCPServer::SetMessageHandler(MessageCallback handler) {
    m_connectionManager.SetMessageCallback(handler);
}

void TCPServer::SetConnectionHandler(ConnectionCallback handler) {
    m_connectionManager.SetConnectionCallback(handler);
}

bool TCPServer::SendMessage(ClientId clientId, const std::string& message) {
    return m_connectionManager.SendMessage(clientId, message);
}

void TCPServer::BroadcastMessage(const std::string& message) {
    m_connectionManager.BroadcastMessage(message);
}

size_t TCPServer::GetClientCount() const {
    return m_connectionManager.GetClientCount();
}

void TCPServer::AcceptThread() {
    Logger::Debug("Accept thread started");
    
    while (m_running) {
        sockaddr_in clientAddr = {};
        int clientAddrLen = sizeof(clientAddr);
        
        SOCKET clientSocket = accept(m_listenSocket, 
                                     reinterpret_cast<sockaddr*>(&clientAddr),
                                     &clientAddrLen);
        
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                Logger::Error("Accept failed: {}", WSAGetLastError());
            }
            break;
        }
        
        // 获取客户端地址信息
        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
        uint16_t clientPort = ntohs(clientAddr.sin_port);
        
        // 检查最大连接数
        int maxConnections = ConfigManager::Instance().GetMaxConnections();
        if (maxConnections > 0 && 
            static_cast<int>(m_connectionManager.GetClientCount()) >= maxConnections) {
            Logger::Warning("Maximum connections reached, rejecting client from {}:{}", 
                          addrStr, clientPort);
            closesocket(clientSocket);
            continue;
        }
        
        // 添加客户端
        ClientId clientId = m_connectionManager.AddClient(clientSocket, addrStr, clientPort);
        
        // 启动客户端接收线程
        m_clientThreads.emplace_back(&TCPServer::ClientReceiveThread, this, clientId);
    }
    
    Logger::Debug("Accept thread stopped");
}

void TCPServer::ClientReceiveThread(ClientId clientId) {
    Logger::Debug("Client {} receive thread started", clientId);
    
    while (m_running) {
        m_connectionManager.ProcessClientReceive(clientId);
        
        // 检查客户端是否还在连接
        if (m_connectionManager.GetClientCount() == 0) {
            break;
        }
    }
    
    Logger::Debug("Client {} receive thread stopped", clientId);
}

void TCPServer::InitializeWSA() {
    std::lock_guard<std::mutex> lock(s_wsaMutex);
    
    if (s_wsaInitialized) {
        return;
    }
    
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        Logger::Error("WSAStartup failed: {}", result);
        return;
    }
    
    s_wsaInitialized = true;
    Logger::Debug("WSA initialized");
}

void TCPServer::CleanupWSA() {
    std::lock_guard<std::mutex> lock(s_wsaMutex);
    
    if (!s_wsaInitialized) {
        return;
    }
    
    WSACleanup();
    s_wsaInitialized = false;
    Logger::Debug("WSA cleaned up");
}

} // namespace MCP
