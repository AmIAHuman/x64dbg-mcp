#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace MCP {

/**
 * @brief 断点操作的 JSON-RPC 处理器
 * 
 * 实现的方法：
 * - breakpoint.set: 设置断点
 * - breakpoint.delete: 删除断点
 * - breakpoint.enable: 启用断点
 * - breakpoint.disable: 禁用断点
 * - breakpoint.toggle: 切换断点状态
 * - breakpoint.list: 列出所有断点
 * - breakpoint.get: 获取断点信息
 * - breakpoint.delete_all: 删除所有断点
 * - breakpoint.set_condition: 设置条件断点
 * - breakpoint.set_log: 设置日志断点
 */
class BreakpointHandler {
public:
    /**
     * @brief 注册所有断点相关的方法
     */
    static void RegisterMethods();

private:
    /**
     * @brief 设置断点
     * @param params {
     *   "address": "0x401000",
     *   "type": "software|hardware|memory",
     *   "name": "my_breakpoint",
     *   "condition": "eax==1",  // 可选
     *   "hw_condition": "execute|write|readwrite",  // 硬件断点
     *   "hw_size": 1|2|4|8,  // 硬件断点大小
     *   "mem_size": 4096  // 内存断点大小
     * }
     * @return { "address": "0x401000", "type": "software", "enabled": true }
     */
    static nlohmann::json Set(const nlohmann::json& params);
    
    /**
     * @brief 删除断点
     * @param params { "address": "0x401000", "type": "software" }
     * @return { "success": true }
     */
    static nlohmann::json Delete(const nlohmann::json& params);
    
    /**
     * @brief 启用断点
     * @param params { "address": "0x401000" }
     * @return { "success": true }
     */
    static nlohmann::json Enable(const nlohmann::json& params);
    
    /**
     * @brief 禁用断点
     * @param params { "address": "0x401000" }
     * @return { "success": true }
     */
    static nlohmann::json Disable(const nlohmann::json& params);
    
    /**
     * @brief 切换断点状态
     * @param params { "address": "0x401000" }
     * @return { "enabled": true }
     */
    static nlohmann::json Toggle(const nlohmann::json& params);
    
    /**
     * @brief 列出所有断点
     * @param params { "type": "software|hardware|memory" }  // 可选
     * @return { "breakpoints": [...] }
     */
    static nlohmann::json List(const nlohmann::json& params);
    
    /**
     * @brief 获取断点信息
     * @param params { "address": "0x401000" }
     * @return { "address": "0x401000", "type": "software", ... }
     */
    static nlohmann::json Get(const nlohmann::json& params);
    
    /**
     * @brief 删除所有断点
     * @param params { "type": "software|hardware|memory" }  // 可选
     * @return { "deleted_count": 10 }
     */
    static nlohmann::json DeleteAll(const nlohmann::json& params);
    
    /**
     * @brief 设置条件断点
     * @param params { "address": "0x401000", "condition": "eax==1" }
     * @return { "success": true }
     */
    static nlohmann::json SetCondition(const nlohmann::json& params);
    
    /**
     * @brief 设置日志断点
     * @param params { "address": "0x401000", "message": "Hit at {rip}" }
     * @return { "success": true }
     */
    static nlohmann::json SetLog(const nlohmann::json& params);
    
    /**
     * @brief 重置断点命中计数
     * @param params { "address": "0x401000" }
     * @return { "success": true, "hit_count": 0 }
     */
    static nlohmann::json ResetHitCount(const nlohmann::json& params);

    /**
     * @brief Set breakpoint with automatic return value override
     * @param params { "address": "0x401000", "return_value": 0, "action": "continue"|"pause" }
     * @return { "success": true }
     */
    static nlohmann::json SetReturnOverride(const nlohmann::json& params);

    // 辅助方法
    static nlohmann::json BreakpointInfoToJson(const struct BreakpointInfo& bp);
};

} // namespace MCP
