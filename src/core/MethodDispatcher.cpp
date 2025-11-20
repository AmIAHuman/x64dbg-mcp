#include "MethodDispatcher.h"
#include "PermissionChecker.h"
#include "ResponseBuilder.h"
#include "Exceptions.h"
#include "Logger.h"

namespace MCP {

MethodDispatcher& MethodDispatcher::Instance() {
    static MethodDispatcher instance;
    return instance;
}

void MethodDispatcher::RegisterMethod(const std::string& method, MethodHandler handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[method] = handler;
    Logger::Debug("Registered method: {}", method);
}

void MethodDispatcher::UnregisterMethod(const std::string& method) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers.erase(method);
    Logger::Debug("Unregistered method: {}", method);
}

bool MethodDispatcher::IsMethodRegistered(const std::string& method) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_handlers.count(method) > 0;
}

JSONRPCResponse MethodDispatcher::Dispatch(const JSONRPCRequest& request) {
    Logger::Debug("Dispatching method: {}", request.method);
    
    try {
        // 检查权限
        if (!PermissionChecker::Instance().IsMethodAllowed(request.method)) {
            throw PermissionDeniedException("Method not allowed: " + request.method);
        }
        
        // 执行方法
        json result = ExecuteMethod(request.method, request.params);
        
        // 构建成功响应
        return ResponseBuilder::CreateSuccessResponse(request.id, result);
        
    } catch (const MCPException& ex) {
        Logger::Error("MCP Exception in {}: {}", request.method, ex.what());
        return ResponseBuilder::CreateErrorResponseFromMCPException(request.id, ex);
        
    } catch (const std::exception& ex) {
        Logger::Error("Exception in {}: {}", request.method, ex.what());
        return ResponseBuilder::CreateErrorResponseFromException(request.id, ex);
    }
}

std::vector<JSONRPCResponse> MethodDispatcher::DispatchBatch(
    const std::vector<JSONRPCRequest>& requests)
{
    std::vector<JSONRPCResponse> responses;
    responses.reserve(requests.size());
    
    Logger::Debug("Dispatching batch of {} requests", requests.size());
    
    for (const auto& request : requests) {
        // 通知消息不返回响应
        if (request.IsNotification()) {
            try {
                ExecuteMethod(request.method, request.params);
            } catch (const std::exception& ex) {
                Logger::Error("Exception in notification {}: {}", request.method, ex.what());
            }
            continue;
        }
        
        responses.push_back(Dispatch(request));
    }
    
    return responses;
}

void MethodDispatcher::RegisterDefaultMethods() {
    Logger::Info("Registering default methods...");
    
    // 注册系统方法
    RegisterMethod("system.info", [](const json& params) -> json {
        return {
            {"name", "x64dbg MCP Server"},
            {"version", "1.0.1"},
            {"protocol_version", "2.0"},
            {"capabilities", {
                {"debug", true},
                {"memory", true},
                {"registers", true},
                {"breakpoints", true},
                {"disassembly", true},
                {"symbols", true}
            }}
        };
    });
    
    RegisterMethod("system.methods", [this](const json& params) -> json {
        auto methods = GetRegisteredMethods();
        return {{"methods", methods}};
    });
    
    // 注册 ping 方法（用于测试连接）
    RegisterMethod("system.ping", [](const json& params) -> json {
        return {{"pong", true}};
    });
    
    // 其他方法将在各自的 Handler 中注册
    // 例如: DebugHandler::RegisterMethods(dispatcher);
    
    Logger::Info("Default methods registered");
}

std::vector<std::string> MethodDispatcher::GetRegisteredMethods() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> methods;
    methods.reserve(m_handlers.size());
    
    for (const auto& pair : m_handlers) {
        methods.push_back(pair.first);
    }
    
    return methods;
}

json MethodDispatcher::ExecuteMethod(const std::string& method, const json& params) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_handlers.find(method);
    if (it == m_handlers.end()) {
        throw MethodNotFoundException("Method not found: " + method);
    }
    
    try {
        return it->second(params);
    } catch (const MCPException&) {
        throw; // 重新抛出 MCP 异常
    } catch (const std::exception& ex) {
        throw MCPException("Error executing method: " + std::string(ex.what()));
    }
}

} // namespace MCP
