#include "SymbolResolver.h"
#include "DebugController.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include "../core/X64DBGBridge.h"
#include <algorithm>
#include <cstdio>

#ifdef X64DBG_SDK_AVAILABLE
#include "_scriptapi_function.h"
#include "_scriptapi_label.h"
#include "_scriptapi_symbol.h"
#endif

namespace MCP {

SymbolResolver& SymbolResolver::Instance() {
    static SymbolResolver instance;
    return instance;
}

std::optional<std::string> SymbolResolver::GetSymbolFromAddress(uint64_t address, bool includeOffset) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    SYMBOLINFO symInfo = {};
    
    if (!DbgGetSymbolInfoAt(address, &symInfo)) {
        // 尝试获取模块+偏移
        auto moduleInfo = GetModuleFromAddress(address);
        if (moduleInfo.has_value()) {
            uint64_t offset = address - moduleInfo->base;
            char offsetStr[32];
            snprintf(offsetStr, sizeof(offsetStr), "+0x%llX", (unsigned long long)offset);
            return moduleInfo->name + offsetStr;
        }
        return std::nullopt;
    }
    
    std::string result = symInfo.undecoratedSymbol ? symInfo.undecoratedSymbol : 
                        (symInfo.decoratedSymbol ? symInfo.decoratedSymbol : "");
    
    // 如果需要偏移量,计算符号偏移
    if (includeOffset) {
        auto symbolAddr = GetAddressFromSymbol(result);
        if (symbolAddr.has_value() && symbolAddr.value() != address) {
            uint64_t offset = address - symbolAddr.value();
            char offsetStr[32];
            snprintf(offsetStr, sizeof(offsetStr), "+0x%llX", (unsigned long long)offset);
            result += offsetStr;
        }
    }
    
    Logger::Trace("Resolved symbol at 0x{:X}: {}", address, result);
    return result;
}

std::optional<uint64_t> SymbolResolver::GetAddressFromSymbol(const std::string& symbol) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    bool success = false;
    uint64_t address = DbgEval(symbol.c_str(), &success);
    
    if (!success) {
        Logger::Warning("Symbol not found: {}", symbol);
        return std::nullopt;
    }
    
    Logger::Trace("Resolved symbol '{}' to 0x{:X}", symbol, address);
    return address;
}

std::optional<SymbolInfo> SymbolResolver::GetSymbolInfo(const std::string& symbol) {
    auto address = GetAddressFromSymbol(symbol);
    if (!address.has_value()) {
        return std::nullopt;
    }
    
    return GetSymbolInfoFromAddress(address.value());
}

std::optional<SymbolInfo> SymbolResolver::GetSymbolInfoFromAddress(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    SYMBOLINFO symInfo = {};
    
    if (!DbgGetSymbolInfoAt(address, &symInfo)) {
        return std::nullopt;
    }
    
    SymbolInfo info;
    info.name = symInfo.undecoratedSymbol ? symInfo.undecoratedSymbol : "";
    info.address = symInfo.addr;
    info.module = "";  // SYMBOLINFO doesn't have module
    info.size = 0;  // SYMBOLINFO doesn't have size
    info.decorated = symInfo.decoratedSymbol ? symInfo.decoratedSymbol : "";
    
    // 确定符号类型
    switch (symInfo.type) {
        case SYMBOLTYPE::sym_import:
            info.type = SymbolType::Import;
            break;
        case SYMBOLTYPE::sym_export:
            info.type = SymbolType::Export;
            break;
        default:
            info.type = SymbolType::Function;
            break;
    }
    
    return info;
}

std::vector<SymbolInfo> SymbolResolver::EnumerateSymbols(const std::string& moduleName,
                                                         std::optional<SymbolType> typeFilter)
{
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    std::vector<SymbolInfo> symbols;
    
    // 使用 x64dbg API 枚举符号
    // 实际实现需要调用 DbgEnumSymbols 或类似函数
    
    Logger::Debug("Enumerated {} symbols from module '{}'", symbols.size(), moduleName);
    return symbols;
}

std::vector<SymbolInfo> SymbolResolver::SearchSymbols(const std::string& pattern) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    std::vector<SymbolInfo> results;
    
    // 枚举所有模块的符号并匹配模式
    auto modules = EnumerateModules();
    
    for (const auto& module : modules) {
        auto symbols = EnumerateSymbols(module.name);
        
        for (const auto& symbol : symbols) {
            if (MatchPattern(symbol.name, pattern)) {
                results.push_back(symbol);
            }
        }
    }
    
    Logger::Debug("Found {} symbols matching pattern '{}'", results.size(), pattern);
    return results;
}

std::optional<ModuleInfo> SymbolResolver::GetModuleInfo(const std::string& moduleName) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    // 枚举所有模块并查找匹配的
    auto modules = EnumerateModules();
    
    for (const auto& module : modules) {
        if (StringUtils::ToLower(module.name) == StringUtils::ToLower(moduleName)) {
            return module;
        }
    }
    
    return std::nullopt;
}

std::optional<ModuleInfo> SymbolResolver::GetModuleFromAddress(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    // 使用 x64dbg API 获取模块
    char moduleName[MAX_MODULE_SIZE] = {0};
    if (!DbgGetModuleAt(address, moduleName)) {
        return std::nullopt;
    }
    
    return GetModuleInfo(moduleName);
}

std::vector<ModuleInfo> SymbolResolver::EnumerateModules() {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    std::vector<ModuleInfo> modules;
    
    // 使用 x64dbg API 枚举模块
    // 实际实现需要调用 DbgEnumModules 或类似函数
    
    Logger::Debug("Enumerated {} modules", modules.size());
    return modules;
}

std::optional<uint64_t> SymbolResolver::GetFunctionStart(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
#ifdef XDBG_SDK_AVAILABLE
    // 使用Script::Function::GetInfo来获取函数信息
    Script::Function::FunctionInfo info = {};
    duint start = 0;
    if (Script::Function::Get(address, &start, nullptr, nullptr)) {
        return start;
    }
#else
    uint64_t start = DbgGetFunctionStart(address);
    
    if (start == 0) {
        return std::nullopt;
    }
    
    return start;
#endif
    return std::nullopt;
}

std::optional<uint64_t> SymbolResolver::GetFunctionEnd(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
#ifdef XDBG_SDK_AVAILABLE
    // 使用Script::Function::GetInfo来获取函数信息
    Script::Function::FunctionInfo info = {};
    duint end = 0;
    if (Script::Function::Get(address, nullptr, &end, nullptr)) {
        return end;
    }
#else
    uint64_t end = DbgGetFunctionEnd(address);
    
    if (end == 0) {
        return std::nullopt;
    }
    
    return end;
#endif
    return std::nullopt;
}

bool SymbolResolver::SetLabel(uint64_t address, const std::string& label) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    if (!DbgSetLabelAt(address, label.c_str())) {
        Logger::Error("Failed to set label at 0x{:X}", address);
        return false;
    }
    
    Logger::Debug("Set label at 0x{:X}: {}", address, label);
    return true;
}

bool SymbolResolver::DeleteLabel(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    // 设置空标签即删除
    if (!DbgSetLabelAt(address, "")) {
        return false;
    }
    
    Logger::Debug("Deleted label at 0x{:X}", address);
    return true;
}

bool SymbolResolver::SetComment(uint64_t address, const std::string& comment) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    if (!DbgSetCommentAt(address, comment.c_str())) {
        Logger::Error("Failed to set comment at 0x{:X}", address);
        return false;
    }
    
    Logger::Debug("Set comment at 0x{:X}: {}", address, comment);
    return true;
}

std::optional<std::string> SymbolResolver::GetComment(uint64_t address) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }
    
    char comment[4096] = {0};
    
    if (!DbgGetCommentAt(address, comment)) {
        return std::nullopt;
    }
    
    if (comment[0] == '\0') {
        return std::nullopt;
    }
    
    return std::string(comment);
}

std::string SymbolResolver::SymbolTypeToString(SymbolType type) {
    switch (type) {
        case SymbolType::Function:
            return "Function";
        case SymbolType::Export:
            return "Export";
        case SymbolType::Import:
            return "Import";
        case SymbolType::Label:
            return "Label";
        case SymbolType::Data:
            return "Data";
        default:
            return "Unknown";
    }
}

bool SymbolResolver::MatchPattern(const std::string& text, const std::string& pattern) {
    // 简单的通配符匹配 (支持 * 通配符)
    if (pattern.empty()) {
        return text.empty();
    }
    
    if (pattern == "*") {
        return true;
    }
    
    // 转换为小写进行不区分大小写的匹配
    std::string lowerText = StringUtils::ToLower(text);
    std::string lowerPattern = StringUtils::ToLower(pattern);
    
    // 简单实现: 检查是否包含 (不支持完整的通配符语法)
    if (lowerPattern.find('*') != std::string::npos) {
        // 去除通配符后检查包含
        std::string cleaned = lowerPattern;
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '*'), cleaned.end());
        return lowerText.find(cleaned) != std::string::npos;
    }
    
    return lowerText == lowerPattern;
}

} // namespace MCP
