/**
 * @file ModuleHandler.h
 * @brief 模块管理方法处理器
 */

#pragma once

#include <nlohmann/json.hpp>

namespace MCP {

using json = nlohmann::json;

/**
 * @brief 模块管理方法处理器
 */
class ModuleHandler {
public:
    /**
     * @brief 注册所有模块方法
     */
    static void RegisterMethods();
    
    /**
     * @brief 获取模块列表
     */
    static json List(const json& params);
    
    /**
     * @brief 获取指定模块信息
     */
    static json Get(const json& params);
    
    /**
     * @brief 获取主模块信息
     */
    static json GetMain(const json& params);

    /**
     * @brief module.list_imports - List imported functions used by a module
     */
    static json ListImports(const json& params);
};

} // namespace MCP
