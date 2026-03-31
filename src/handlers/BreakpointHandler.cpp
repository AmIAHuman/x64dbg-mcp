#include "BreakpointHandler.h"
#include "../business/BreakpointManager.h"
#include "../core/MethodDispatcher.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include "../core/Logger.h"
#include "bridgemain.h"
#include "_dbgfunctions.h"

namespace MCP {

void BreakpointHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("breakpoint.set", Set);
    dispatcher.RegisterMethod("breakpoint.delete", Delete);
    dispatcher.RegisterMethod("breakpoint.enable", Enable);
    dispatcher.RegisterMethod("breakpoint.disable", Disable);
    dispatcher.RegisterMethod("breakpoint.toggle", Toggle);
    dispatcher.RegisterMethod("breakpoint.list", List);
    dispatcher.RegisterMethod("breakpoint.get", Get);
    dispatcher.RegisterMethod("breakpoint.delete_all", DeleteAll);
    dispatcher.RegisterMethod("breakpoint.set_condition", SetCondition);
    dispatcher.RegisterMethod("breakpoint.set_log", SetLog);
    dispatcher.RegisterMethod("breakpoint.reset_hitcount", ResetHitCount);
    dispatcher.RegisterMethod("breakpoint.set_return_override", SetReturnOverride);
}

nlohmann::json BreakpointHandler::Set(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Setting breakpoint requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("address")) {
        throw InvalidParamsException(
            "Missing required parameter: address. "
            "Example: {\"address\":\"0x401000\",\"type\":\"software\"}"
        );
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    std::string typeStr = params.value("type", "software");
    std::string name = params.value("name", "");
    
    auto& manager = BreakpointManager::Instance();
    bool success = false;
    
    if (typeStr == "software") {
        success = manager.SetSoftwareBreakpoint(address, name);
    }
    else if (typeStr == "hardware") {
        // 瑙ｆ瀽纭欢鏂偣鍙傛暟
        std::string hwCondStr = params.value("hw_condition", "execute");
        int hwSize = params.value("hw_size", 1);
        
        HardwareBreakpointCondition hwCond = HardwareBreakpointCondition::Execute;
        if (hwCondStr == "write") {
            hwCond = HardwareBreakpointCondition::Write;
        } else if (hwCondStr == "readwrite" || hwCondStr == "access") {
            hwCond = HardwareBreakpointCondition::ReadWrite;
        }
        
        HardwareBreakpointSize hwSz = HardwareBreakpointSize::Byte1;
        switch (hwSize) {
            case 2: hwSz = HardwareBreakpointSize::Byte2; break;
            case 4: hwSz = HardwareBreakpointSize::Byte4; break;
            case 8: hwSz = HardwareBreakpointSize::Byte8; break;
        }
        
        success = manager.SetHardwareBreakpoint(address, hwCond, hwSz, name);
    }
    else if (typeStr == "memory") {
        size_t memSize = params.value("mem_size", 1);
        success = manager.SetMemoryBreakpoint(address, memSize, name);
    }
    else {
        throw InvalidParamsException("Invalid breakpoint type: " + typeStr);
    }
    
    if (!success) {
        throw MCPException("Failed to set breakpoint at: " + addressStr);
    }
    
    // 濡傛灉鏈夋潯浠?璁剧疆鏉′欢
    if (params.contains("condition")) {
        std::string condition = params["condition"].get<std::string>();
        manager.SetCondition(address, condition);
    }
    
    // 鏋勫缓鍝嶅簲
    nlohmann::json result;
    result["address"] = StringUtils::FormatAddress(address);
    result["type"] = typeStr;
    result["enabled"] = true;
    
    if (!name.empty()) {
        result["name"] = name;
    }
    
    return result;
}

nlohmann::json BreakpointHandler::Delete(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Deleting breakpoint requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    std::optional<BreakpointType> typeFilter;
    if (params.contains("type")) {
        std::string typeStr = params["type"].get<std::string>();
        if (typeStr == "software") {
            typeFilter = BreakpointType::Software;
        } else if (typeStr == "hardware") {
            typeFilter = BreakpointType::Hardware;
        } else if (typeStr == "memory") {
            typeFilter = BreakpointType::Memory;
        }
    }
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.DeleteBreakpoint(address, typeFilter);
    
    if (!success) {
        throw MCPException("Failed to delete breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    
    return result;
}

nlohmann::json BreakpointHandler::Enable(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Enabling breakpoint requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.EnableBreakpoint(address);
    
    if (!success) {
        throw MCPException("Failed to enable breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    
    return result;
}

nlohmann::json BreakpointHandler::Disable(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Disabling breakpoint requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.DisableBreakpoint(address);
    
    if (!success) {
        throw MCPException("Failed to disable breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    
    return result;
}

nlohmann::json BreakpointHandler::Toggle(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Toggling breakpoint requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.ToggleBreakpoint(address);
    
    if (!success) {
        throw MCPException("Failed to toggle breakpoint at: " + addressStr);
    }
    
    // 鑾峰彇褰撳墠鐘舵€?
    auto bp = manager.GetBreakpoint(address);
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["enabled"] = bp.has_value() ? bp->enabled : false;
    
    return result;
}

nlohmann::json BreakpointHandler::List(const nlohmann::json& params) {
    std::optional<BreakpointType> typeFilter;
    
    if (params.contains("type")) {
        std::string typeStr = params["type"].get<std::string>();
        if (typeStr == "software") {
            typeFilter = BreakpointType::Software;
        } else if (typeStr == "hardware") {
            typeFilter = BreakpointType::Hardware;
        } else if (typeStr == "memory") {
            typeFilter = BreakpointType::Memory;
        }
    }
    
    auto& manager = BreakpointManager::Instance();
    auto breakpoints = manager.ListBreakpoints(typeFilter);
    
    nlohmann::json bpArray = nlohmann::json::array();
    for (const auto& bp : breakpoints) {
        bpArray.push_back(BreakpointInfoToJson(bp));
    }
    
    nlohmann::json result;
    result["count"] = breakpoints.size();
    result["breakpoints"] = bpArray;
    
    return result;
}

nlohmann::json BreakpointHandler::Get(const nlohmann::json& params) {
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& manager = BreakpointManager::Instance();
    auto bp = manager.GetBreakpoint(address);
    
    if (!bp.has_value()) {
        throw InvalidAddressException("No breakpoint at address: " + addressStr);
    }
    
    return BreakpointInfoToJson(bp.value());
}

nlohmann::json BreakpointHandler::DeleteAll(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Deleting breakpoints requires write permission");
    }
    
    std::optional<BreakpointType> typeFilter;
    
    if (params.contains("type")) {
        std::string typeStr = params["type"].get<std::string>();
        if (typeStr == "software") {
            typeFilter = BreakpointType::Software;
        } else if (typeStr == "hardware") {
            typeFilter = BreakpointType::Hardware;
        } else if (typeStr == "memory") {
            typeFilter = BreakpointType::Memory;
        }
    }
    
    auto& manager = BreakpointManager::Instance();
    size_t deletedCount = manager.DeleteAllBreakpoints(typeFilter);
    
    nlohmann::json result;
    result["deleted_count"] = deletedCount;
    
    return result;
}

nlohmann::json BreakpointHandler::SetCondition(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Setting breakpoint condition requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("condition")) {
        throw InvalidParamsException("Missing required parameter: condition");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    std::string condition = params["condition"].get<std::string>();
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.SetCondition(address, condition);
    
    if (!success) {
        throw MCPException("Failed to set condition for breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["condition"] = condition;
    
    return result;
}

nlohmann::json BreakpointHandler::SetLog(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Setting log breakpoint requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    if (!params.contains("log_text")) {
        throw InvalidParamsException("Missing required parameter: log_text");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    std::string message = params["log_text"].get<std::string>();
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.SetLogBreakpoint(address, message);
    
    if (!success) {
        throw MCPException("Failed to set log breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["message"] = message;
    
    return result;
}

nlohmann::json BreakpointHandler::ResetHitCount(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Resetting breakpoint hit count requires write permission");
    }
    
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    
    auto& manager = BreakpointManager::Instance();
    bool success = manager.ResetHitCount(address);
    
    if (!success) {
        throw MCPException("Failed to reset hit count for breakpoint at: " + addressStr);
    }
    
    nlohmann::json result;
    result["success"] = true;
    result["address"] = StringUtils::FormatAddress(address);
    result["hit_count"] = 0;
    
    return result;
}

nlohmann::json BreakpointHandler::BreakpointInfoToJson(const BreakpointInfo& bp) {
    nlohmann::json json;
    
    json["address"] = StringUtils::FormatAddress(bp.address);
    
    // 绫诲瀷
    switch (bp.type) {
        case BreakpointType::Software:
            json["type"] = "software";
            break;
        case BreakpointType::Hardware:
            json["type"] = "hardware";
            
            // 纭欢鏂偣鐗规湁灞炴€?
            switch (bp.condition) {
                case HardwareBreakpointCondition::Execute:
                    json["hw_condition"] = "execute";
                    break;
                case HardwareBreakpointCondition::Write:
                    json["hw_condition"] = "write";
                    break;
                case HardwareBreakpointCondition::ReadWrite:
                    json["hw_condition"] = "readwrite";
                    break;
            }
            json["hw_size"] = static_cast<int>(bp.size);
            break;
        case BreakpointType::Memory:
            json["type"] = "memory";
            break;
    }
    
    json["enabled"] = bp.enabled;
    json["hit_count"] = bp.hitCount;
    
    if (!bp.name.empty()) {
        json["name"] = bp.name;
    }
    
    if (!bp.module.empty()) {
        json["module"] = bp.module;
    }
    
    if (!bp.condition_expr.empty()) {
        json["condition"] = bp.condition_expr;
    }
    
    if (bp.isLogBreakpoint) {
        json["is_log_breakpoint"] = true;
        json["log_message"] = bp.logMessage;
    }
    
    return json;
}

nlohmann::json BreakpointHandler::SetReturnOverride(const nlohmann::json& params) {
    if (!PermissionChecker::Instance().IsBreakpointModificationAllowed()) {
        throw PermissionDeniedException("Setting breakpoint requires write permission");
    }

    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }

    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);

    uint64_t returnValue = 0;
    if (params.contains("return_value")) {
        if (params["return_value"].is_number()) {
            returnValue = params["return_value"].get<uint64_t>();
        } else if (params["return_value"].is_string()) {
            returnValue = StringUtils::ParseAddress(params["return_value"].get<std::string>());
        }
    }

    std::string action = "continue";
    if (params.contains("action") && params["action"].is_string()) {
        action = params["action"].get<std::string>();
    }

    auto& manager = BreakpointManager::Instance();

    // Set breakpoint if one doesn't already exist
    if (!manager.HasBreakpoint(address)) {
        manager.SetBreakpoint(address, BreakpointType::Software);
    }

    // Build command string: step out, set return value, optionally resume
    std::string command = "rtr;rax=" + StringUtils::FormatAddress(returnValue);
    if (action == "continue") {
        command += ";run";
    }

    // Get BP reference and set command text
    BP_REF bpRef;
    memset(&bpRef, 0, sizeof(bpRef));

    int bpType = DbgGetBpxTypeAt(static_cast<duint>(address));
    BPXTYPE type = bp_none;
    if (bpType & bp_normal) {
        type = bp_normal;
    } else if (bpType & bp_hardware) {
        type = bp_hardware;
    } else if (bpType & bp_memory) {
        type = bp_memory;
    }

    if (!DbgFunctions()->BpRefVa(&bpRef, type, static_cast<duint>(address))) {
        throw MCPException("Failed to get breakpoint reference", -32000);
    }

    if (!DbgFunctions()->BpSetFieldText(&bpRef, bpf_commandtext, command.c_str())) {
        throw MCPException("Failed to set breakpoint command", -32000);
    }

    Logger::Info("Set return override at 0x{:X}: rax={}, action={}",
        address, StringUtils::FormatAddress(returnValue), action);

    return {
        {"success", true},
        {"address", StringUtils::FormatAddress(address)},
        {"return_value", StringUtils::FormatAddress(returnValue)},
        {"action", action},
        {"command", command}
    };
}

} // namespace MCP

