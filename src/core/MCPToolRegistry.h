#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MCP {

/**
 * @brief MCP 工具参数定义
 */
struct MCPToolParameter {
    std::string name;
    std::string type;  // "string", "number", "integer", "boolean", "object", "array"
    std::string description;
    bool required;
    json defaultValue;
    json enumValues;  // 可选的枚举值列表
    json arrayItemsSchema;  // array 类型的元素 schema
    
    json ToSchema() const;
};

/**
 * @brief MCP 工具定义
 */
struct MCPToolDefinition {
    std::string name;
    std::string description;
    std::string jsonrpcMethod;  // 对应的 JSON-RPC 方法名
    std::vector<MCPToolParameter> parameters;
    
    json ToMCPFormat() const;
    
    /**
     * @brief 验证参数是否符合 schema
     * @param args 待验证的参数
     * @return 错误信息,如果为空表示验证通过
     */
    std::string ValidateArguments(const json& args) const;
    
    /**
     * @brief 将 MCP arguments 转换为 JSON-RPC params
     */
    json TransformToJSONRPC(const json& mcpArgs) const;
};

/**
 * @brief MCP 工具注册表
 */
class MCPToolRegistry {
public:
    static MCPToolRegistry& Instance();
    
    /**
     * @brief 注册一个工具
     */
    void RegisterTool(const MCPToolDefinition& tool);
    
    /**
     * @brief 获取所有工具定义
     */
    std::vector<MCPToolDefinition> GetAllTools() const;
    
    /**
     * @brief 根据名称查找工具
     */
    std::optional<MCPToolDefinition> FindTool(const std::string& name) const;
    
    /**
     * @brief 生成 tools/list 响应
     */
    json GenerateToolsListResponse() const;
    
    /**
     * @brief 注册所有默认工具
     */
    void RegisterDefaultTools();
    
private:
    MCPToolRegistry() = default;
    ~MCPToolRegistry() = default;
    MCPToolRegistry(const MCPToolRegistry&) = delete;
    MCPToolRegistry& operator=(const MCPToolRegistry&) = delete;
    
    std::map<std::string, MCPToolDefinition> m_tools;
};

} // namespace MCP
