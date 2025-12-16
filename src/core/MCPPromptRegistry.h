/**
 * @file MCPPromptRegistry.h
 * @brief MCP Prompt Registry - Pre-built instruction templates
 * 
 * Prompts are user-controlled templates that guide the model to work with
 * specific tools and resources according to MCP specification.
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace MCP {

using json = nlohmann::json;

/**
 * @brief Prompt argument definition
 */
struct MCPPromptArgument {
    std::string name;          // Argument name
    std::string description;   // What this argument is for
    bool required;             // Is this argument required
    
    json ToSchema() const;
};

/**
 * @brief MCP Prompt Definition
 * 
 * Prompts provide structured templates for common debugging workflows
 */
struct MCPPromptDefinition {
    std::string name;                              // Unique prompt name
    std::string title;                             // Human-readable title
    std::string description;                       // What this prompt does
    std::vector<MCPPromptArgument> arguments;      // Expected arguments
    
    json ToMCPFormat() const;
    
    /**
     * @brief Generate the actual prompt text with filled arguments
     */
    std::string GeneratePrompt(const json& args) const;
};

/**
 * @brief Generated prompt with messages
 */
struct MCPPromptMessage {
    std::string role;      // "user" or "assistant"
    std::string content;   // Message content
    
    json ToMCPFormat() const;
};

struct MCPPromptResult {
    std::string description;
    std::vector<MCPPromptMessage> messages;
    
    json ToMCPFormat() const;
};

/**
 * @brief MCP Prompt Registry
 * 
 * Manages prompt templates for common x64dbg debugging workflows
 */
class MCPPromptRegistry {
public:
    static MCPPromptRegistry& Instance();
    
    /**
     * @brief Register a prompt
     */
    void RegisterPrompt(const MCPPromptDefinition& prompt);
    
    /**
     * @brief Register default x64dbg prompts
     */
    void RegisterDefaultPrompts();
    
    /**
     * @brief Find a prompt by name
     */
    std::optional<MCPPromptDefinition> FindPrompt(const std::string& name) const;
    
    /**
     * @brief Generate prompts/list response
     */
    json GeneratePromptsListResponse() const;
    
    /**
     * @brief Get a prompt with filled arguments
     * @param name Prompt name
     * @param args Arguments to fill
     * @return Prompt result with messages
     */
    MCPPromptResult GetPrompt(const std::string& name, const json& args) const;

private:
    MCPPromptRegistry() = default;
    
    std::vector<MCPPromptDefinition> m_prompts;
};

} // namespace MCP
