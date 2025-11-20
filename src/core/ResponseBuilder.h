#pragma once
#include "JSONRPCParser.h"
#include <nlohmann/json.hpp>
#include <exception>

using json = nlohmann::json;

namespace MCP {

// 前向声明
class MCPException;

/**
 * @brief JSON-RPC 响应构建器
 */
class ResponseBuilder {
public:
    /**
     * @brief 创建成功响应
     * @param id 请求 ID
     * @param result 结果数据
     * @return JSON-RPC 响应对象
     */
    static JSONRPCResponse CreateSuccessResponse(const RequestId& id, const json& result);
    
    /**
     * @brief 创建错误响应
     * @param id 请求 ID
     * @param code 错误码
     * @param message 错误消息
     * @param data 附加数据
     * @return JSON-RPC 响应对象
     */
    static JSONRPCResponse CreateErrorResponse(
        const RequestId& id,
        int code,
        const std::string& message,
        const json& data = json());
    
    /**
     * @brief 从异常创建错误响应
     * @param id 请求 ID
     * @param ex 异常对象
     * @return JSON-RPC 响应对象
     */
    static JSONRPCResponse CreateErrorResponseFromException(
        const RequestId& id,
        const std::exception& ex);
    
    /**
     * @brief 从 MCP 异常创建错误响应
     * @param id 请求 ID
     * @param ex MCP 异常对象
     * @return JSON-RPC 响应对象
     */
    static JSONRPCResponse CreateErrorResponseFromMCPException(
        const RequestId& id,
        const MCPException& ex);
    
    /**
     * @brief 创建通知消息（服务器主动推送）
     * @param method 方法名
     * @param params 参数
     * @return JSON 字符串
     */
    static std::string CreateNotification(const std::string& method, const json& params);
    
    /**
     * @brief 将响应序列化为 JSON 字符串
     * @param response 响应对象
     * @return JSON 字符串
     */
    static std::string Serialize(const JSONRPCResponse& response);
    
    /**
     * @brief 序列化批量响应
     * @param responses 响应对象数组
     * @return JSON 字符串
     */
    static std::string SerializeBatch(const std::vector<JSONRPCResponse>& responses);
};

} // namespace MCP
