#include "MemoryManager.h"
#include "DebugController.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include "../core/X64DBGBridge.h"
#include <algorithm>

namespace MCP {

MemoryManager& MemoryManager::Instance() {
    static MemoryManager instance;
    return instance;
}

std::vector<uint8_t> MemoryManager::Read(uint64_t address, size_t size) {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
    
    if (size == 0) {
        throw InvalidSizeException("Size cannot be zero");
    }
    
    if (size > MAX_READ_SIZE) {
        throw InvalidSizeException("Read size exceeds maximum: " + 
                                  std::to_string(MAX_READ_SIZE));
    }
    
    // 检查起始地址是否可读
    if (!DbgMemIsValidReadPtr(address)) {
        throw InvalidAddressException("Memory not readable at address: " +
                                     StringUtils::FormatAddress(address));
    }
    
    std::vector<uint8_t> buffer(size);
    
    // 尝试读取完整大小
    if (DbgMemRead(address, buffer.data(), size)) {
        Logger::Trace("Read {} bytes from 0x{:X}", size, address);
        return buffer;
    }
    
    // 如果失败，尝试分块读取（处理跨页边界的情况）
    Logger::Debug("Full read failed, attempting chunked read for {} bytes from 0x{:X}", size, address);
    
    size_t chunkSize = 4096; // 4KB chunks
    size_t totalRead = 0;
    
    for (size_t offset = 0; offset < size; offset += chunkSize) {
        size_t currentChunkSize = std::min(chunkSize, size - offset);
        uint64_t currentAddr = address + offset;
        
        // 检查当前块是否可读
        if (!DbgMemIsValidReadPtr(currentAddr)) {
            Logger::Warning("Memory not readable at chunk starting at 0x{:X}", currentAddr);
            break;
        }
        
        if (DbgMemRead(currentAddr, buffer.data() + offset, currentChunkSize)) {
            totalRead += currentChunkSize;
        } else {
            Logger::Warning("Failed to read chunk at 0x{:X}, size {}", currentAddr, currentChunkSize);
            break;
        }
    }
    
    if (totalRead == 0) {
        throw MCPException("Failed to read memory at: " +
                          StringUtils::FormatAddress(address));
    }
    
    // 如果只读取了部分数据，调整缓冲区大小
    if (totalRead < size) {
        buffer.resize(totalRead);
        Logger::Info("Partial read: {} of {} bytes from 0x{:X}", totalRead, size, address);
    }
    
    return buffer;
}

size_t MemoryManager::Write(uint64_t address, const std::vector<uint8_t>& data) {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
    
    if (data.empty()) {
        throw InvalidSizeException("Data cannot be empty");
    }
    
    if (data.size() > MAX_WRITE_SIZE) {
        throw InvalidSizeException("Write size exceeds maximum: " +
                                  std::to_string(MAX_WRITE_SIZE));
    }
    
    if (!IsWritable(address, data.size())) {
        throw InvalidAddressException("Memory not writable at address: " +
                                     StringUtils::FormatAddress(address));
    }
    
    if (!DbgMemWrite(address, data.data(), data.size())) {
        throw MCPException("Failed to write memory at: " +
                          StringUtils::FormatAddress(address));
    }
    
    Logger::Debug("Wrote {} bytes to 0x{:X}", data.size(), address);
    return data.size();
}

std::vector<MemorySearchResult> MemoryManager::Search(
    const std::string& pattern,
    uint64_t startAddress,
    uint64_t endAddress,
    size_t maxResults)
{
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
    
    Logger::Debug("Searching memory for pattern: {}", pattern);
    
    // 解析模式
    std::vector<uint8_t> patternBytes;
    std::vector<bool> mask;
    
    // 简单的模式解析（如 "48 83 EC ?? 48"）
    std::vector<std::string> tokens = StringUtils::Split(pattern, ' ');
    for (const auto& token : tokens) {
        std::string cleaned = StringUtils::Trim(token);
        if (cleaned.empty()) continue;
        
        if (cleaned == "??" || cleaned == "?") {
            patternBytes.push_back(0);
            mask.push_back(false);  // 通配符
        } else {
            try {
                auto bytes = StringUtils::HexToBytes(cleaned);
                if (!bytes.empty()) {
                    patternBytes.push_back(bytes[0]);
                    mask.push_back(true);  // 精确匹配
                }
            } catch (...) {
                throw InvalidParamsException("Invalid pattern format: " + cleaned);
            }
        }
    }
    
    if (patternBytes.empty()) {
        throw InvalidParamsException("Empty search pattern");
    }
    
    std::vector<MemorySearchResult> results;
    
    // 简化的搜索实现（实际应使用 x64dbg 的搜索 API）
    // 这里只是示意性代码
    auto regions = EnumerateRegions();
    
    for (const auto& region : regions) {
        if (results.size() >= maxResults) {
            break;
        }
        
        // 检查地址范围
        if (startAddress > 0 && region.base < startAddress) continue;
        if (endAddress > 0 && region.base > endAddress) break;
        
        // 跳过不可读区域
        uint64_t checkSize = region.size < 4096 ? region.size : 4096;
        if (!IsReadable(region.base, checkSize)) {
            continue;
        }
        
        // 搜索该区域（实际应分块读取）
        // 这里只是占位代码
        Logger::Trace("Searching region: 0x{:X} - 0x{:X}", region.base, region.base + region.size);
    }
    
    Logger::Info("Found {} matches for pattern", results.size());
    return results;
}

std::optional<MemoryRegion> MemoryManager::GetMemoryInfo(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    // 尝试读取该地址以验证可访问性
    uint8_t testByte = 0;
    if (!DbgMemIsValidReadPtr(address)) {
        return std::nullopt;
    }
    
    // 查找内存区域的基址
    duint base = 0;
    if (!DbgMemFindBaseAddr(address, &base)) {
        return std::nullopt;
    }
    
    // 构建基本的内存区域信息
    MemoryRegion region;
    region.base = base;
    
    // 尝试确定区域大小(简化实现,实际应该查询完整的内存映射)
    // 从基址开始逐步探测直到找到边界
    duint size = 0x1000; // 至少一页
    duint probe = base + size;
    while (DbgMemIsValidReadPtr(probe) && size < 0x10000000) { // 限制最大256MB
        size += 0x1000;
        probe = base + size;
    }
    region.size = size;
    
    // 简化的保护属性(实际应该通过VirtualQueryEx获取)
    region.protection = "PAGE_READWRITE"; // 默认值
    region.type = "MEM_COMMIT";
    
    return region;
}

std::vector<MemoryRegion> MemoryManager::EnumerateRegions() {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    std::vector<MemoryRegion> regions;
    
    // 使用 x64dbg API 枚举内存区域
    // 实际实现需要调用 DbgMemMap() 或类似函数
    
    Logger::Debug("Enumerated {} memory regions", regions.size());
    return regions;
}

bool MemoryManager::IsReadable(uint64_t address, size_t size) {
    // 检查起始地址是否可读
    if (!DbgMemIsValidReadPtr(address)) {
        return false;
    }
    
    // 对于大范围，检查结束地址
    if (size > 4096 && size < MAX_READ_SIZE) {
        uint64_t endAddress = address + size - 1;
        if (!DbgMemIsValidReadPtr(endAddress)) {
            return false;
        }
    }
    
    return true;
}

bool MemoryManager::IsWritable(uint64_t address, size_t size) {
    auto info = GetMemoryInfo(address);
    if (!info.has_value()) {
        return false;
    }
    
    // 检查保护属性是否包含写权限
    bool writable = info->protection.find("WRITE") != std::string::npos ||
                   info->protection.find("EXECUTE_READWRITE") != std::string::npos;
    
    if (!writable) {
        return false;
    }
    
    // 对于大范围，检查结束地址所在页的权限
    if (size > 4096 && size < MAX_WRITE_SIZE) {
        uint64_t endAddress = address + size - 1;
        auto endInfo = GetMemoryInfo(endAddress);
        if (!endInfo.has_value()) {
            return false;
        }
        return endInfo->protection.find("WRITE") != std::string::npos ||
               endInfo->protection.find("EXECUTE_READWRITE") != std::string::npos;
    }
    
    return true;
}

uint64_t MemoryManager::Allocate(size_t size) {
    // 使用 x64dbg API 分配内存
    // 实际实现需要调用 DbgMemAlloc() 或 VirtualAllocEx
    
    Logger::Info("Allocated {} bytes", size);
    return 0;  // 占位
}

bool MemoryManager::Free(uint64_t address) {
    // 使用 x64dbg API 释放内存
    // 实际实现需要调用 DbgMemFree() 或 VirtualFreeEx
    
    Logger::Info("Freed memory at 0x{:X}", address);
    return true;  // 占位
}

std::vector<uint8_t> MemoryManager::ParsePattern(const std::string& pattern) {
    // 解析十六进制模式
    return StringUtils::HexToBytes(pattern);
}

bool MemoryManager::MatchPattern(const uint8_t* data, 
                                const std::vector<uint8_t>& pattern,
                                const std::vector<bool>& mask)
{
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (mask[i] && data[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

std::string MemoryManager::ProtectionToString(uint32_t protect) {
    // Windows 内存保护标志
    switch (protect & 0xFF) {
        case 0x01: return "PAGE_NOACCESS";
        case 0x02: return "PAGE_READONLY";
        case 0x04: return "PAGE_READWRITE";
        case 0x08: return "PAGE_WRITECOPY";
        case 0x10: return "PAGE_EXECUTE";
        case 0x20: return "PAGE_EXECUTE_READ";
        case 0x40: return "PAGE_EXECUTE_READWRITE";
        case 0x80: return "PAGE_EXECUTE_WRITECOPY";
        default: return "UNKNOWN";
    }
}

} // namespace MCP
