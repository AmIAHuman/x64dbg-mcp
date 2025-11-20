#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace MCP {

/**
 * @brief 反汇编操作的 JSON-RPC 处理器
 * 
 * 实现的方法：
 * - disassembly.at: 反汇编指定地址
 * - disassembly.range: 反汇编指令范围
 * - disassembly.function: 反汇编整个函数
 */
class DisassemblyHandler {
public:
    /**
     * @brief 注册所有反汇编相关的方法
     */
    static void RegisterMethods();

private:
    /**
     * @brief 反汇编指定地址
     * @param params { "address": "0x401000", "count": 10 }
     * @return { "instructions": [...] }
     */
    static nlohmann::json At(const nlohmann::json& params);
    
    /**
     * @brief 反汇编指令范围
     * @param params { "address": "0x401000", "count": 100 }
     * @return { "instructions": [...] }
     */
    static nlohmann::json Range(const nlohmann::json& params);
    
    /**
     * @brief 反汇编整个函数
     * @param params { "address": "0x401000" }
     * @return { "start": "0x401000", "end": "0x401234", "instructions": [...] }
     */
    static nlohmann::json Function(const nlohmann::json& params);
    
    // 辅助方法
    static nlohmann::json InstructionToJson(const struct InstructionInfo& instr);
};

/**
 * @brief 符号操作的 JSON-RPC 处理器
 * 
 * 实现的方法：
 * - symbol.resolve: 解析符号地址
 * - symbol.from_address: 从地址获取符号
 * - symbol.search: 搜索符号
 * - symbol.list: 列出模块符号
 * - symbol.modules: 列出所有模块
 * - symbol.set_label: 设置标签
 * - symbol.set_comment: 设置注释
 */
class SymbolHandler {
public:
    /**
     * @brief 注册所有符号相关的方法
     */
    static void RegisterMethods();

private:
    /**
     * @brief 解析符号地址
     * @param params { "symbol": "kernel32.CreateFileA" }
     * @return { "symbol": "kernel32.CreateFileA", "address": "0x76543210" }
     */
    static nlohmann::json Resolve(const nlohmann::json& params);
    
    /**
     * @brief 从地址获取符号
     * @param params { "address": "0x401000", "include_offset": true }
     * @return { "address": "0x401000", "symbol": "main+0x10" }
     */
    static nlohmann::json FromAddress(const nlohmann::json& params);
    
    /**
     * @brief 搜索符号
     * @param params { "pattern": "*Create*" }
     * @return { "symbols": [...] }
     */
    static nlohmann::json Search(const nlohmann::json& params);
    
    /**
     * @brief 列出模块符号
     * @param params { "module": "kernel32" }
     * @return { "symbols": [...] }
     */
    static nlohmann::json List(const nlohmann::json& params);
    
    /**
     * @brief 列出所有模块
     * @param params {}
     * @return { "modules": [...] }
     */
    static nlohmann::json Modules(const nlohmann::json& params);
    
    /**
     * @brief 设置标签
     * @param params { "address": "0x401000", "label": "my_label" }
     * @return { "success": true }
     */
    static nlohmann::json SetLabel(const nlohmann::json& params);
    
    /**
     * @brief 设置注释
     * @param params { "address": "0x401000", "comment": "Important function" }
     * @return { "success": true }
     */
    static nlohmann::json SetComment(const nlohmann::json& params);
    
    /**
     * @brief 获取注释
     * @param params { "address": "0x401000" }
     * @return { "comment": "Important function" }
     */
    static nlohmann::json GetComment(const nlohmann::json& params);
    
    // 辅助方法
    static nlohmann::json SymbolInfoToJson(const struct SymbolInfo& symbol);
    static nlohmann::json ModuleInfoToJson(const struct ModuleInfo& module);
};

} // namespace MCP
