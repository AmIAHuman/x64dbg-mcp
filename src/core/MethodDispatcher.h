#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "JSONRPCParser.h"

using json = nlohmann::json;

namespace MCP {

/**
 * @brief 方法处理函数类型
 * @param params 参数对象
 * @return 结果对象
 */
using MethodHandler = std::function<json(const json& params)>;

/**
 * @brief 方法分发器（单例）
 */
class MethodDispatcher {
public:
    /**
     * @brief 获取单例实例
     */
    static MethodDispatcher& Instance();
    
    /**
     * @brief 注册方法处理器
     * @param method 方法名
     * @param handler 处理函数
     */
    void RegisterMethod(const std::string& method, MethodHandler handler);
    
    /**
     * @brief 取消注册方法
     * @param method 方法名
     */
    void UnregisterMethod(const std::string& method);
    
    /**
     * @brief 检查方法是否已注册
     * @param method 方法名
     * @return 是否已注册
     */
    bool IsMethodRegistered(const std::string& method) const;
    
    /**
     * @brief 分发请求到对应的处理器
     * @param request JSON-RPC 请求对象
     * @return JSON-RPC 响应对象
     */
    JSONRPCResponse Dispatch(const JSONRPCRequest& request);
    
    /**
     * @brief 批量分发请求
     * @param requests JSON-RPC 请求对象数组
     * @return JSON-RPC 响应对象数组
     */
    std::vector<JSONRPCResponse> DispatchBatch(const std::vector<JSONRPCRequest>& requests);
    
    /**
     * @brief 注册所有默认方法
     */
    void RegisterDefaultMethods();
    
    /**
     * @brief 获取已注册方法列表
     * @return 方法名列表
     */
    std::vector<std::string> GetRegisteredMethods() const;

private:
    MethodDispatcher() = default;
    ~MethodDispatcher() = default;
    MethodDispatcher(const MethodDispatcher&) = delete;
    MethodDispatcher& operator=(const MethodDispatcher&) = delete;
    
    json ExecuteMethod(const std::string& method, const json& params);
    
    std::unordered_map<std::string, MethodHandler> m_handlers;
    mutable std::mutex m_mutex;
};

} // namespace MCP
