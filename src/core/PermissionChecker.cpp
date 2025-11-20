#include "PermissionChecker.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "../utils/StringUtils.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MCP {

PermissionChecker& PermissionChecker::Instance() {
    static PermissionChecker instance;
    return instance;
}

void PermissionChecker::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_allowedMethods.clear();
    
    auto& config = ConfigManager::Instance();
    
    try {
        // 从配置文件加载允许的方法列表
        json allowedMethods = config.Get<json>("permissions.allowed_methods", json::array());
        
        if (allowedMethods.is_array()) {
            for (const auto& method : allowedMethods) {
                if (method.is_string()) {
                    std::string methodStr = method.get<std::string>();
                    m_allowedMethods.insert(methodStr);
                    Logger::Debug("Added allowed method pattern: {}", methodStr);
                }
            }
        }
        
        Logger::Info("PermissionChecker initialized with {} allowed patterns", 
                    m_allowedMethods.size());
    } catch (const std::exception& e) {
        Logger::Error("Failed to load permissions from config: {}", e.what());
        // 默认允许所有方法（不安全，仅用于开发）
        m_allowedMethods.insert("*");
    }
}

bool PermissionChecker::IsMethodAllowed(const std::string& method) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 检查精确匹配
    if (m_allowedMethods.count(method) > 0) {
        return true;
    }
    
    // 检查通配符模式
    for (const auto& pattern : m_allowedMethods) {
        if (MatchesPattern(pattern, method)) {
            return true;
        }
    }
    
    Logger::Warning("Method not allowed: {}", method);
    return false;
}

bool PermissionChecker::IsMemoryWriteAllowed() const {
    return ConfigManager::Instance().IsMemoryWriteAllowed();
}

bool PermissionChecker::IsRegisterWriteAllowed() const {
    return ConfigManager::Instance().IsRegisterWriteAllowed();
}

bool PermissionChecker::IsScriptExecutionAllowed() const {
    return ConfigManager::Instance().IsScriptExecutionAllowed();
}

bool PermissionChecker::IsBreakpointModificationAllowed() const {
    return ConfigManager::Instance().Get<bool>("permissions.allow_breakpoint_modification", true);
}

bool PermissionChecker::CanWrite() const {
    // 通用写入权限检查,返回是否允许写入操作
    return ConfigManager::Instance().Get<bool>("permissions.allow_write", true);
}

void PermissionChecker::AddAllowedMethod(const std::string& method) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedMethods.insert(method);
    Logger::Debug("Added allowed method: {}", method);
}

void PermissionChecker::RemoveAllowedMethod(const std::string& method) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedMethods.erase(method);
    Logger::Debug("Removed allowed method: {}", method);
}

void PermissionChecker::ClearAllowedMethods() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allowedMethods.clear();
    Logger::Warning("Cleared all allowed methods");
}

bool PermissionChecker::MatchesPattern(const std::string& pattern, const std::string& method) const {
    // 支持简单的通配符匹配
    // "debug.*" 匹配所有 debug 开头的方法
    // "*" 匹配所有方法
    
    if (pattern == "*") {
        return true;
    }
    
    // 使用 StringUtils 的通配符匹配
    return StringUtils::WildcardMatch(pattern, method);
}

} // namespace MCP
