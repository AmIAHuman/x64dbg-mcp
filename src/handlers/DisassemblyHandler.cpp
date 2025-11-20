#include "DisassemblyHandler.h"
#include "../business/DisassemblyEngine.h"
#include "../business/SymbolResolver.h"
#include "../core/MethodDispatcher.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"

namespace MCP {

// ==================== DisassemblyHandler ====================

void DisassemblyHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("disassembly.at", At);
    dispatcher.RegisterMethod("disassembly.range", Range);
    dispatcher.RegisterMethod("disassembly.function", Function);
}

nlohmann::json DisassemblyHandler::At(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    size_t count = params.value("count", 1);
    
    auto& engine = DisassemblyEngine::Instance();
    
    nlohmann::json instrArray = nlohmann::json::array();
    
    if (count == 1) {
        auto instr = engine.DisassembleAt(address);
        instrArray.push_back(InstructionToJson(instr));
    } else {
        auto instructions = engine.DisassembleRange(address, count);
        for (const auto& instr : instructions) {
            instrArray.push_back(InstructionToJson(instr));
        }
    }
    
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["count"] = instrArray.size();
    result["instructions"] = instrArray;
    
    return result;
}

nlohmann::json DisassemblyHandler::Range(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("count")) {
        throw InvalidParamsException("Missing required parameter: count");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    size_t count = params["count"].get<size_t>();
    
    auto& engine = DisassemblyEngine::Instance();
    auto instructions = engine.DisassembleRange(address, count);
    
    nlohmann::json instrArray = nlohmann::json::array();
    for (const auto& instr : instructions) {
        instrArray.push_back(InstructionToJson(instr));
    }
    
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["count"] = instructions.size();
    result["instructions"] = instrArray;
    
    return result;
}

nlohmann::json DisassemblyHandler::Function(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& engine = DisassemblyEngine::Instance();
    auto& resolver = SymbolResolver::Instance();
    
    // 获取函数起始地址
    auto funcStart = resolver.GetFunctionStart(address);
    if (!funcStart.has_value()) {
        funcStart = address;
    }
    
    // 反汇编函数
    auto instructions = engine.DisassembleFunction(funcStart.value());
    
    nlohmann::json instrArray = nlohmann::json::array();
    for (const auto& instr : instructions) {
        instrArray.push_back(InstructionToJson(instr));
    }
    
    nlohmann::json result;
    result["start"] = StringUtils::FormatAddress(funcStart.value());
    result["count"] = instructions.size();
    result["instructions"] = instrArray;
    
    // 如果可以获取函数结束地址
    if (!instructions.empty()) {
        auto lastInstr = instructions.back();
        result["end"] = StringUtils::FormatAddress(lastInstr.address + lastInstr.size);
    }
    
    return result;
}

nlohmann::json DisassemblyHandler::InstructionToJson(const InstructionInfo& instr) {
    nlohmann::json json;
    
    json["address"] = StringUtils::FormatAddress(instr.address);
    json["bytes"] = StringUtils::BytesToHex(instr.bytes);
    json["mnemonic"] = instr.mnemonic;
    json["operands"] = instr.operands;
    json["instruction"] = instr.full;
    json["size"] = instr.size;
    
    if (!instr.comment.empty()) {
        json["comment"] = instr.comment;
    }
    
    if (instr.isBranch) {
        json["is_branch"] = true;
    }
    
    if (instr.isCall) {
        json["is_call"] = true;
    }
    
    if (instr.isRet) {
        json["is_ret"] = true;
    }
    
    if (instr.branchTarget.has_value()) {
        json["branch_target"] = StringUtils::FormatAddress(instr.branchTarget.value());
    }
    
    return json;
}

// ==================== SymbolHandler ====================

void SymbolHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("symbol.resolve", Resolve);
    dispatcher.RegisterMethod("symbol.from_address", FromAddress);
    dispatcher.RegisterMethod("symbol.search", Search);
    dispatcher.RegisterMethod("symbol.list", List);
    dispatcher.RegisterMethod("symbol.modules", Modules);
    dispatcher.RegisterMethod("symbol.set_label", SetLabel);
    dispatcher.RegisterMethod("symbol.set_comment", SetComment);
    dispatcher.RegisterMethod("symbol.get_comment", GetComment);
}

nlohmann::json SymbolHandler::Resolve(const nlohmann::json& params) {
    if (!params.contains("symbol")) {
        throw InvalidParamsException("Missing required parameter: symbol");
    }
    
    std::string symbol = params["symbol"].get<std::string>();
    
    auto& resolver = SymbolResolver::Instance();
    auto address = resolver.GetAddressFromSymbol(symbol);
    
    if (!address.has_value()) {
        throw InvalidParamsException("Symbol not found: " + symbol);
    }
    
    nlohmann::json result;
    result["symbol"] = symbol;
    result["address"] = StringUtils::FormatAddress(address.value());
    
    // 获取详细信息
    auto info = resolver.GetSymbolInfoFromAddress(address.value());
    if (info.has_value()) {
        result["module"] = info->module;
        result["type"] = resolver.SymbolTypeToString(info->type);
    }
    
    return result;
}

nlohmann::json SymbolHandler::FromAddress(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    bool includeOffset = params.value("include_offset", true);
    
    auto& resolver = SymbolResolver::Instance();
    auto symbol = resolver.GetSymbolFromAddress(address, includeOffset);
    
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    
    if (symbol.has_value()) {
        result["symbol"] = symbol.value();
    } else {
        result["symbol"] = nullptr;
    }
    
    return result;
}

nlohmann::json SymbolHandler::Search(const nlohmann::json& params) {
    if (!params.contains("pattern")) {
        throw InvalidParamsException("Missing required parameter: pattern");
    }
    
    std::string pattern = params["pattern"].get<std::string>();
    
    auto& resolver = SymbolResolver::Instance();
    auto symbols = resolver.SearchSymbols(pattern);
    
    nlohmann::json symbolArray = nlohmann::json::array();
    for (const auto& symbol : symbols) {
        symbolArray.push_back(SymbolInfoToJson(symbol));
    }
    
    nlohmann::json result;
    result["pattern"] = pattern;
    result["count"] = symbols.size();
    result["symbols"] = symbolArray;
    
    return result;
}

nlohmann::json SymbolHandler::List(const nlohmann::json& params) {
    if (!params.contains("module")) {
        throw InvalidParamsException("Missing required parameter: module");
    }
    
    std::string moduleName = params["module"].get<std::string>();
    
    auto& resolver = SymbolResolver::Instance();
    auto symbols = resolver.EnumerateSymbols(moduleName);
    
    nlohmann::json symbolArray = nlohmann::json::array();
    for (const auto& symbol : symbols) {
        symbolArray.push_back(SymbolInfoToJson(symbol));
    }
    
    nlohmann::json result;
    result["module"] = moduleName;
    result["count"] = symbols.size();
    result["symbols"] = symbolArray;
    
    return result;
}

nlohmann::json SymbolHandler::Modules(const nlohmann::json& params) {
    auto& resolver = SymbolResolver::Instance();
    auto modules = resolver.EnumerateModules();
    
    nlohmann::json moduleArray = nlohmann::json::array();
    for (const auto& module : modules) {
        moduleArray.push_back(ModuleInfoToJson(module));
    }
    
    nlohmann::json result;
    result["count"] = modules.size();
    result["modules"] = moduleArray;
    
    return result;
}

nlohmann::json SymbolHandler::SetLabel(const nlohmann::json& params) {
    // 检查写权限
    if (!PermissionChecker::Instance().CanWrite()) {
        throw PermissionDeniedException("Setting label requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("label")) {
        throw InvalidParamsException("Missing required parameter: label");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    std::string label = params["label"].get<std::string>();
    
    auto& resolver = SymbolResolver::Instance();
    bool success = resolver.SetLabel(address, label);
    
    if (!success) {
        throw MCPException("Failed to set label at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["label"] = label;
    
    return result;
}

nlohmann::json SymbolHandler::SetComment(const nlohmann::json& params) {
    // 检查写权限
    if (!PermissionChecker::Instance().CanWrite()) {
        throw PermissionDeniedException("Setting comment requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("comment")) {
        throw InvalidParamsException("Missing required parameter: comment");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    std::string comment = params["comment"].get<std::string>();
    
    auto& resolver = SymbolResolver::Instance();
    bool success = resolver.SetComment(address, comment);
    
    if (!success) {
        throw MCPException("Failed to set comment at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["comment"] = comment;
    
    return result;
}

nlohmann::json SymbolHandler::GetComment(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& resolver = SymbolResolver::Instance();
    auto comment = resolver.GetComment(address);
    
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    
    if (comment.has_value()) {
        result["comment"] = comment.value();
    } else {
        result["comment"] = nullptr;
    }
    
    return result;
}

nlohmann::json SymbolHandler::SymbolInfoToJson(const SymbolInfo& symbol) {
    nlohmann::json json;
    
    json["name"] = symbol.name;
    json["address"] = StringUtils::FormatAddress(symbol.address);
    json["module"] = symbol.module;
    
    // 符号类型
    switch (symbol.type) {
        case SymbolType::Function:
            json["type"] = "function";
            break;
        case SymbolType::Export:
            json["type"] = "export";
            break;
        case SymbolType::Import:
            json["type"] = "import";
            break;
        case SymbolType::Label:
            json["type"] = "label";
            break;
        case SymbolType::Data:
            json["type"] = "data";
            break;
    }
    
    if (symbol.size > 0) {
        json["size"] = symbol.size;
    }
    
    if (!symbol.decorated.empty()) {
        json["decorated"] = symbol.decorated;
    }
    
    return json;
}

nlohmann::json SymbolHandler::ModuleInfoToJson(const ModuleInfo& module) {
    nlohmann::json json;
    
    json["name"] = module.name;
    json["path"] = module.path;
    json["base"] = StringUtils::FormatAddress(module.base);
    json["size"] = module.size;
    json["entry"] = StringUtils::FormatAddress(module.entry);
    json["is_system"] = module.isSystemModule;
    
    return json;
}

} // namespace MCP
