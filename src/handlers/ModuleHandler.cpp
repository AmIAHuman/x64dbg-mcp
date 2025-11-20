/**
 * @file ModuleHandler.cpp
 * @brief 模块管理方法处理器实现
 */

#include "ModuleHandler.h"
#include "../core/MethodDispatcher.h"
#include "../core/Exceptions.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"
#include <_scriptapi_module.h>
#include <bridgelist.h>

namespace MCP {

void ModuleHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("module.list", List);
    dispatcher.RegisterMethod("module.get", Get);
    dispatcher.RegisterMethod("module.get_main", GetMain);
    
    Logger::Info("Registered module.* methods");
}

json ModuleHandler::List(const json& params) {
    json result = json::object();
    json modules = json::array();
    
    // 使用 Script API 获取模块列表
    BridgeList<Script::Module::ModuleInfo> moduleList;
    
    if (!Script::Module::GetList(&moduleList)) {
        result["modules"] = modules;
        result["count"] = 0;
        return result;
    }
    
    // 转换为 JSON 数组
    for (size_t i = 0; i < moduleList.Count(); i++) {
        const auto& mod = moduleList[i];
        json module = json::object();
        module["base"] = StringUtils::FormatAddress(mod.base);
        module["size"] = mod.size;
        module["entry"] = StringUtils::FormatAddress(mod.entry);
        module["name"] = mod.name;
        module["path"] = mod.path;
        module["section_count"] = mod.sectionCount;
        
        modules.push_back(module);
    }
    
    result["modules"] = modules;
    result["count"] = moduleList.Count();
    
    return result;
}

json ModuleHandler::Get(const json& params) {
    if (!params.contains("name") && !params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: name or address");
    }
    
    Script::Module::ModuleInfo info;
    bool success = false;
    
    if (params.contains("name")) {
        std::string name = params["name"].get<std::string>();
        success = Script::Module::InfoFromName(name.c_str(), &info);
    } else {
        std::string addressStr = params["address"].get<std::string>();
        duint address = StringUtils::ParseAddress(addressStr);
        success = Script::Module::InfoFromAddr(address, &info);
    }
    
    if (!success) {
        throw MCPException("Module not found", -32000);
    }
    
    json result = json::object();
    result["base"] = StringUtils::FormatAddress(info.base);
    result["size"] = info.size;
    result["entry"] = StringUtils::FormatAddress(info.entry);
    result["section_count"] = info.sectionCount;
    result["name"] = info.name;
    result["path"] = info.path;
    
    return result;
}

json ModuleHandler::GetMain(const json& params) {
    Script::Module::ModuleInfo info;
    
    if (!Script::Module::GetMainModuleInfo(&info)) {
        throw MCPException("Failed to get main module info", -32000);
    }
    
    json result = json::object();
    result["base"] = StringUtils::FormatAddress(info.base);
    result["size"] = info.size;
    result["entry"] = StringUtils::FormatAddress(info.entry);
    result["section_count"] = info.sectionCount;
    result["name"] = info.name;
    result["path"] = info.path;
    
    return result;
}

} // namespace MCP
