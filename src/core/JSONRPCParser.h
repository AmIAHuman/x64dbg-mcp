#pragma once
#include <string>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MCP {

/**
 * @brief JSON-RPC 请求 ID 类型
 */
using RequestId = std::variant<int64_t, std::string, std::nullptr_t>;

/**
 * @brief JSON-RPC 错误对象
 */
struct JSONRPCError {
    int code;
    std::string message;
    json data;
    
    JSONRPCError(int c, const std::string& msg, const json& d = json())
        : code(c), message(msg), data(d) {}
    
    json ToJson() const {
        json j = {
            {"code", code},
            {"message", message}
        };
        if (!data.is_null()) {
            j["data"] = data;
        }
        return j;
    }
};

/**
 * @brief JSON-RPC 请求对象
 */
struct JSONRPCRequest {
    std::string jsonrpc;
    RequestId id;
    std::string method;
    json params;
    
    bool IsNotification() const {
        return std::holds_alternative<std::nullptr_t>(id);
    }
};

/**
 * @brief JSON-RPC 响应对象
 */
struct JSONRPCResponse {
    std::string jsonrpc;
    RequestId id;
    json result;
    std::optional<JSONRPCError> error;
    
    bool IsError() const {
        return error.has_value();
    }
    
    json ToJson() const {
        json j = {
            {"jsonrpc", jsonrpc}
        };
        
        // 添加 ID
        if (std::holds_alternative<int64_t>(id)) {
            j["id"] = std::get<int64_t>(id);
        } else if (std::holds_alternative<std::string>(id)) {
            j["id"] = std::get<std::string>(id);
        } else {
            j["id"] = nullptr;
        }
        
        // 添加结果或错误
        if (error.has_value()) {
            j["error"] = error->ToJson();
        } else {
            j["result"] = result;
        }
        
        return j;
    }
};

/**
 * @brief JSON-RPC 解析器
 */
class JSONRPCParser {
public:
    /**
     * @brief 解析 JSON-RPC 请求
     * @param raw 原始 JSON 字符串
     * @return 解析后的请求对象
     * @throws ParseErrorException JSON 解析错误
     * @throws InvalidRequestException 请求格式错误
     */
    static JSONRPCRequest ParseRequest(const std::string& raw);
    
    /**
     * @brief 解析 JSON-RPC 批量请求
     * @param raw 原始 JSON 字符串
     * @return 解析后的请求对象数组
     */
    static std::vector<JSONRPCRequest> ParseBatchRequest(const std::string& raw);
    
    /**
     * @brief 验证请求对象
     * @param req 请求对象
     * @throws InvalidRequestException 请求无效
     */
    static void ValidateRequest(const JSONRPCRequest& req);
    
private:
    static RequestId ParseRequestId(const json& j);
    static json ParseParams(const json& j);
};

} // namespace MCP
