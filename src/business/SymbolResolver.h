#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace MCP {

/**
 * @brief 符号类型
 */
enum class SymbolType {
    Function,    // 函数
    Export,      // 导出符号
    Import,      // 导入符号
    Label,       // 标签
    Data         // 数据
};

/**
 * @brief 符号信息
 */
struct SymbolInfo {
    std::string name;           // 符号名称
    uint64_t address;           // 符号地址
    std::string module;         // 所属模块
    SymbolType type;            // 符号类型
    uint64_t size;              // 符号大小 (如果已知)
    std::string decorated;      // 修饰名 (C++ mangled name)
};

/**
 * @brief 模块信息
 */
struct ModuleInfo {
    std::string name;           // 模块名称
    std::string path;           // 模块路径
    uint64_t base;              // 基址
    uint64_t size;              // 大小
    uint64_t entry;             // 入口点
    bool isSystemModule;        // 是否为系统模块
};

/**
 * @brief 符号解析器
 * 封装 x64dbg 的符号解析功能
 */
class SymbolResolver {
public:
    /**
     * @brief 获取单例实例
     */
    static SymbolResolver& Instance();
    
    /**
     * @brief 根据地址获取符号名称
     * @param address 地址
     * @param includeOffset 是否包含偏移量 (如 "func+0x10")
     * @return 符号名称
     */
    std::optional<std::string> GetSymbolFromAddress(uint64_t address, bool includeOffset = true);
    
    /**
     * @brief 根据符号名称获取地址
     * @param symbol 符号名称 (支持 "module.func" 格式)
     * @return 符号地址
     */
    std::optional<uint64_t> GetAddressFromSymbol(const std::string& symbol);
    
    /**
     * @brief 获取符号详细信息
     * @param symbol 符号名称
     * @return 符号信息
     */
    std::optional<SymbolInfo> GetSymbolInfo(const std::string& symbol);
    
    /**
     * @brief 根据地址获取符号信息
     * @param address 地址
     * @return 符号信息
     */
    std::optional<SymbolInfo> GetSymbolInfoFromAddress(uint64_t address);
    
    /**
     * @brief 枚举模块的所有符号
     * @param moduleName 模块名称
     * @param typeFilter 类型过滤器 (可选)
     * @return 符号列表
     */
    std::vector<SymbolInfo> EnumerateSymbols(const std::string& moduleName,
                                            std::optional<SymbolType> typeFilter = std::nullopt);
    
    /**
     * @brief 搜索符号
     * @param pattern 搜索模式 (支持通配符 *)
     * @return 符号列表
     */
    std::vector<SymbolInfo> SearchSymbols(const std::string& pattern);
    
    /**
     * @brief 获取模块信息
     * @param moduleName 模块名称
     * @return 模块信息
     */
    std::optional<ModuleInfo> GetModuleInfo(const std::string& moduleName);
    
    /**
     * @brief 根据地址获取模块信息
     * @param address 地址
     * @return 模块信息
     */
    std::optional<ModuleInfo> GetModuleFromAddress(uint64_t address);
    
    /**
     * @brief 枚举所有已加载的模块
     * @return 模块列表
     */
    std::vector<ModuleInfo> EnumerateModules();
    
    /**
     * @brief 获取函数起始地址
     * @param address 函数内的任意地址
     * @return 函数起始地址
     */
    std::optional<uint64_t> GetFunctionStart(uint64_t address);
    
    /**
     * @brief 获取函数结束地址
     * @param address 函数起始地址
     * @return 函数结束地址
     */
    std::optional<uint64_t> GetFunctionEnd(uint64_t address);
    
    /**
     * @brief 设置用户标签
     * @param address 地址
     * @param label 标签名称
     * @return 是否成功
     */
    bool SetLabel(uint64_t address, const std::string& label);
    
    /**
     * @brief 删除用户标签
     * @param address 地址
     * @return 是否成功
     */
    bool DeleteLabel(uint64_t address);
    
    /**
     * @brief 设置注释
     * @param address 地址
     * @param comment 注释内容
     * @return 是否成功
     */
    bool SetComment(uint64_t address, const std::string& comment);
    
    /**
     * @brief 获取注释
     * @param address 地址
     * @return 注释内容
     */
    std::optional<std::string> GetComment(uint64_t address);
    
    /**
     * @brief 将符号类型转换为字符串
     */
    static std::string SymbolTypeToString(SymbolType type);

private:
    SymbolResolver() = default;
    ~SymbolResolver() = default;
    SymbolResolver(const SymbolResolver&) = delete;
    SymbolResolver& operator=(const SymbolResolver&) = delete;
    
    bool MatchPattern(const std::string& text, const std::string& pattern);
};

} // namespace MCP

