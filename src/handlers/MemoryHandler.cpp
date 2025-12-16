#include "MemoryHandler.h"
#include "../business/MemoryManager.h"
#include "../core/MethodDispatcher.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace MCP {

void MemoryHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("memory.read", Read);
    dispatcher.RegisterMethod("memory.write", Write);
    dispatcher.RegisterMethod("memory.search", Search);
    dispatcher.RegisterMethod("memory.get_info", GetInfo);
    dispatcher.RegisterMethod("memory.enumerate", Enumerate);
    dispatcher.RegisterMethod("memory.allocate", Allocate);
    dispatcher.RegisterMethod("memory.free", Free);
}

nlohmann::json MemoryHandler::Read(const nlohmann::json& params) {
    // 验证参数
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("size")) {
        throw InvalidParamsException("Missing required parameter: size");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    size_t size = params["size"].get<size_t>();
    std::string encoding = params.value("encoding", "hex");
    
    // 解析地址
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    // 读取内存
    auto& manager = MemoryManager::Instance();
    auto data = manager.Read(address, size);
    
    // 构建响应
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["size"] = data.size();
    result["data"] = EncodeData(data, encoding);
    result["encoding"] = encoding;
    
    return result;
}

nlohmann::json MemoryHandler::Write(const nlohmann::json& params) {
    // 检查写权限
    if (!PermissionChecker::Instance().CanWrite()) {
        throw PermissionDeniedException("Memory write requires write permission");
    }
    
    // 验证参数
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("data")) {
        throw InvalidParamsException("Missing required parameter: data");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    std::string dataStr = params["data"].get<std::string>();
    std::string encoding = params.value("encoding", "hex");
    
    // 解析地址和数据
    uint64_t address = StringUtils::ParseAddress(addressStr);
    auto data = DecodeData(dataStr, encoding);
    
    if (data.empty()) {
        throw InvalidParamsException("Empty data");
    }
    
    // 写入内存
    auto& manager = MemoryManager::Instance();
    size_t bytesWritten = manager.Write(address, data);
    
    // 构建响应
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["bytes_written"] = bytesWritten;
    
    return result;
}

nlohmann::json MemoryHandler::Search(const nlohmann::json& params) {
    // 验证参数
    if (!params.contains("pattern")) {
        throw InvalidParamsException("Missing required parameter: pattern");
    }
    
    std::string pattern = params["pattern"].get<std::string>();
    
    uint64_t startAddr = 0;
    uint64_t endAddr = 0;
    size_t maxResults = 1000;
    
    if (params.contains("start")) {
        startAddr = StringUtils::ParseAddress(params["start"].get<std::string>());
    }
    if (params.contains("end")) {
        endAddr = StringUtils::ParseAddress(params["end"].get<std::string>());
    }
    if (params.contains("max_results")) {
        maxResults = params["max_results"].get<size_t>();
    }
    
    // 搜索
    auto& manager = MemoryManager::Instance();
    auto results = manager.Search(pattern, startAddr, endAddr, maxResults);
    
    // 构建响应
    nlohmann::json resultArray = nlohmann::json::array();
    for (const auto& match : results) {
        nlohmann::json item;
        item["address"] = StringUtils::FormatAddress(match.address);
        item["data"] = StringUtils::BytesToHex(match.data);
        resultArray.push_back(item);
    }
    
    nlohmann::json result;
    result["pattern"] = pattern;
    result["count"] = results.size();
    result["results"] = resultArray;
    
    return result;
}

nlohmann::json MemoryHandler::GetInfo(const nlohmann::json& params) {
    // 验证参数
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    // 获取内存信息
    auto& manager = MemoryManager::Instance();
    auto info = manager.GetMemoryInfo(address);
    
    if (!info.has_value()) {
        throw InvalidAddressException("Invalid address: " + addressStr);
    }
    
    // 构建响应
    nlohmann::json result;
    result["queried_address"] = addressStr;
    result["base"] = StringUtils::FormatAddress(info->base);
    result["size"] = info->size;
    result["protection"] = info->protection;
    result["type"] = info->type;
    
    if (!info->info.empty()) {
        result["info"] = info->info;
    }
    
    return result;
}

nlohmann::json MemoryHandler::Enumerate(const nlohmann::json& params) {
    // 枚举内存区域
    auto& manager = MemoryManager::Instance();
    auto regions = manager.EnumerateRegions();
    
    // 构建响应
    nlohmann::json regionArray = nlohmann::json::array();
    for (const auto& region : regions) {
        nlohmann::json item;
        item["base"] = StringUtils::FormatAddress(region.base);
        item["size"] = region.size;
        item["protection"] = region.protection;
        item["type"] = region.type;
        
        if (!region.info.empty()) {
            item["info"] = region.info;
        }
        
        regionArray.push_back(item);
    }
    
    nlohmann::json result;
    result["count"] = regions.size();
    result["regions"] = regionArray;
    
    return result;
}

nlohmann::json MemoryHandler::Allocate(const nlohmann::json& params) {
    // 检查写权限
    if (!PermissionChecker::Instance().CanWrite()) {
        throw PermissionDeniedException("Memory allocation requires write permission");
    }
    
    // 验证参数
    if (!params.contains("size")) {
        throw InvalidParamsException("Missing required parameter: size");
    }
    
    size_t size = params["size"].get<size_t>();
    
    if (size == 0) {
        throw InvalidParamsException("Size cannot be zero");
    }
    
    // 分配内存
    auto& manager = MemoryManager::Instance();
    uint64_t address = manager.Allocate(size);
    
    if (address == 0) {
        throw MCPException("Failed to allocate memory");
    }
    
    // 构建响应
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["size"] = size;
    
    return result;
}

nlohmann::json MemoryHandler::Free(const nlohmann::json& params) {
    // 检查写权限
    if (!PermissionChecker::Instance().CanWrite()) {
        throw PermissionDeniedException("Memory free requires write permission");
    }
    
    // 验证参数
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    // 释放内存
    auto& manager = MemoryManager::Instance();
    bool success = manager.Free(address);
    
    if (!success) {
        throw MCPException("Failed to free memory at: " + addressStr);
    }
    
    // 构建响应
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    
    return result;
}

std::vector<uint8_t> MemoryHandler::DecodeData(const std::string& data, const std::string& encoding) {
    if (encoding == "hex") {
        return StringUtils::HexToBytes(data);
    } else if (encoding == "base64") {
        // Base64 解码（需要实现）
        throw InvalidParamsException("Base64 encoding not yet implemented");
    } else if (encoding == "ascii") {
        return std::vector<uint8_t>(data.begin(), data.end());
    } else {
        throw InvalidParamsException("Unknown encoding: " + encoding);
    }
}

std::string MemoryHandler::EncodeData(const std::vector<uint8_t>& data, const std::string& encoding) {
    if (encoding == "hex") {
        return StringUtils::BytesToHex(data);
    } else if (encoding == "base64") {
        // Base64 编码（需要实现）
        throw InvalidParamsException("Base64 encoding not yet implemented");
    } else if (encoding == "ascii") {
        return std::string(data.begin(), data.end());
    } else {
        throw InvalidParamsException("Unknown encoding: " + encoding);
    }
}

} // namespace MCP
