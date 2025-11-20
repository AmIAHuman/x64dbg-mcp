#pragma once
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace MCP {

/**
 * @brief 请求验证器
 */
class RequestValidator {
public:
    /**
     * @brief 验证参数是否包含必需字段
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段不存在
     */
    static void RequireField(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为字符串
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireString(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为数字
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireNumber(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为整数
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireInteger(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为布尔
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireBoolean(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为对象
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireObject(const json& params, const std::string& fieldName);
    
    /**
     * @brief 验证字段类型为数组
     * @param params 参数对象
     * @param fieldName 字段名
     * @throws InvalidParamsException 字段类型错误
     */
    static void RequireArray(const json& params, const std::string& fieldName);
    
    /**
     * @brief 获取字符串字段（带默认值）
     * @param params 参数对象
     * @param fieldName 字段名
     * @param defaultValue 默认值
     * @return 字段值
     */
    static std::string GetString(const json& params, 
                                 const std::string& fieldName, 
                                 const std::string& defaultValue = "");
    
    /**
     * @brief 获取整数字段（带默认值）
     * @param params 参数对象
     * @param fieldName 字段名
     * @param defaultValue 默认值
     * @return 字段值
     */
    static int64_t GetInteger(const json& params, 
                             const std::string& fieldName, 
                             int64_t defaultValue = 0);
    
    /**
     * @brief 获取布尔字段（带默认值）
     * @param params 参数对象
     * @param fieldName 字段名
     * @param defaultValue 默认值
     * @return 字段值
     */
    static bool GetBoolean(const json& params, 
                          const std::string& fieldName, 
                          bool defaultValue = false);
    
    /**
     * @brief 验证地址字符串格式
     * @param address 地址字符串
     * @return 解析后的地址
     * @throws InvalidAddressException 地址格式错误
     */
    static uint64_t ValidateAddress(const std::string& address);
    
    /**
     * @brief 验证大小参数
     * @param size 大小值
     * @param maxSize 最大允许大小
     * @throws InvalidSizeException 大小无效
     */
    static void ValidateSize(size_t size, size_t maxSize);
};

} // namespace MCP
