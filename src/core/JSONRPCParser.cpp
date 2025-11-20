#include "JSONRPCParser.h"
#include "Exceptions.h"
#include "Logger.h"

namespace MCP {

JSONRPCRequest JSONRPCParser::ParseRequest(const std::string& raw) {
    json j;
    
    try {
        j = json::parse(raw);
    } catch (const json::exception& e) {
        Logger::Error("JSON parse error: {}", e.what());
        throw ParseErrorException(std::string("JSON parse error: ") + e.what());
    }
    
    // 检查是否为批量请求
    if (j.is_array()) {
        throw InvalidRequestException("Batch requests should use ParseBatchRequest");
    }
    
    if (!j.is_object()) {
        throw InvalidRequestException("Request must be an object");
    }
    
    JSONRPCRequest req;
    
    // 解析 jsonrpc 字段
    if (!j.contains("jsonrpc") || !j["jsonrpc"].is_string()) {
        throw InvalidRequestException("Missing or invalid 'jsonrpc' field");
    }
    req.jsonrpc = j["jsonrpc"].get<std::string>();
    if (req.jsonrpc != "2.0") {
        throw InvalidRequestException("Only JSON-RPC 2.0 is supported");
    }
    
    // 解析 method 字段
    if (!j.contains("method") || !j["method"].is_string()) {
        throw InvalidRequestException("Missing or invalid 'method' field");
    }
    req.method = j["method"].get<std::string>();
    
    // 解析 id 字段（可选，通知消息没有 id）
    if (j.contains("id")) {
        req.id = ParseRequestId(j["id"]);
    } else {
        req.id = nullptr;
    }
    
    // 解析 params 字段（可选）
    req.params = ParseParams(j);
    
    ValidateRequest(req);
    return req;
}

std::vector<JSONRPCRequest> JSONRPCParser::ParseBatchRequest(const std::string& raw) {
    json j;
    
    try {
        j = json::parse(raw);
    } catch (const json::exception& e) {
        throw ParseErrorException(std::string("JSON parse error: ") + e.what());
    }
    
    if (!j.is_array()) {
        throw InvalidRequestException("Batch request must be an array");
    }
    
    if (j.empty()) {
        throw InvalidRequestException("Batch request cannot be empty");
    }
    
    std::vector<JSONRPCRequest> requests;
    for (const auto& item : j) {
        // 递归解析每个请求
        std::string itemStr = item.dump();
        requests.push_back(ParseRequest(itemStr));
    }
    
    return requests;
}

void JSONRPCParser::ValidateRequest(const JSONRPCRequest& req) {
    // 验证方法名不为空
    if (req.method.empty()) {
        throw InvalidRequestException("Method name cannot be empty");
    }
    
    // 验证 params 类型（必须是对象或数组）
    if (!req.params.is_null() && !req.params.is_object() && !req.params.is_array()) {
        throw InvalidParamsException("Params must be an object or array");
    }
}

RequestId JSONRPCParser::ParseRequestId(const json& j) {
    if (j.is_null()) {
        return nullptr;
    } else if (j.is_number_integer()) {
        return j.get<int64_t>();
    } else if (j.is_string()) {
        return j.get<std::string>();
    } else {
        throw InvalidRequestException("Invalid request ID type");
    }
}

json JSONRPCParser::ParseParams(const json& j) {
    if (j.contains("params")) {
        return j["params"];
    }
    return json::object();
}

} // namespace MCP
