#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>

namespace MCP {

/**
 * @brief 权限检查器
 */
class PermissionChecker {
public:
    /**
     * @brief 获取单例实例
     */
    static PermissionChecker& Instance();
    
    /**
     * @brief 初始化权限配置
     */
    void Initialize();
    
    /**
     * @brief 检查方法是否被允许
     * @param method 方法名
     * @return 是否允许
     */
    bool IsMethodAllowed(const std::string& method) const;
    
    /**
     * @brief 检查是否允许内存写入
     */
    bool IsMemoryWriteAllowed() const;
    
    /**
     * @brief 检查是否允许寄存器写入
     */
    bool IsRegisterWriteAllowed() const;
    
    /**
     * @brief 检查是否允许脚本执行
     */
    bool IsScriptExecutionAllowed() const;
    
    /**
     * @brief 检查是否允许断点修改
     */
    bool IsBreakpointModificationAllowed() const;
    
    /**
     * @brief 检查是否允许写入操作 (通用方法)
     * @return 是否允许写入
     */
    bool CanWrite() const;
    
    /**
     * @brief 添加允许的方法
     * @param method 方法名或模式（支持通配符）
     */
    void AddAllowedMethod(const std::string& method);
    
    /**
     * @brief 移除允许的方法
     * @param method 方法名
     */
    void RemoveAllowedMethod(const std::string& method);
    
    /**
     * @brief 清空允许列表
     */
    void ClearAllowedMethods();

private:
    PermissionChecker() = default;
    ~PermissionChecker() = default;
    PermissionChecker(const PermissionChecker&) = delete;
    PermissionChecker& operator=(const PermissionChecker&) = delete;
    
    bool MatchesPattern(const std::string& pattern, const std::string& method) const;
    
    std::unordered_set<std::string> m_allowedMethods;
    mutable std::mutex m_mutex;
};

} // namespace MCP
