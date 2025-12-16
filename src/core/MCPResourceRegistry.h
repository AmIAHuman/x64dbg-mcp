/**
 * @file MCPResourceRegistry.h
 * @brief MCP Resource Registry - Passive data sources for context
 * 
 * Resources are application-controlled, read-only data sources that provide
 * context information to AI models according to MCP specification.
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace MCP {

using json = nlohmann::json;

/**
 * @brief MCP Resource Definition
 * 
 * Resources expose structured data with unique URIs and MIME types
 */
struct MCPResourceDefinition {
    std::string uri;           // Unique resource identifier (e.g., "debugger://state/current")
    std::string name;          // Internal name
    std::string title;         // Human-readable title
    std::string description;   // What this resource provides
    std::string mimeType;      // Content MIME type (e.g., "application/json")
    
    json ToMCPFormat() const;
};

/**
 * @brief MCP Resource Template Definition
 * 
 * Dynamic resources with URI parameters for flexible queries
 */
struct MCPResourceTemplate {
    std::string uriTemplate;   // Template with {parameters} (e.g., "memory://{address}/{size}")
    std::string name;          // Internal name
    std::string title;         // Human-readable title
    std::string description;   // What this template provides
    std::string mimeType;      // Content MIME type
    
    json ToMCPFormat() const;
};

/**
 * @brief Resource content with metadata
 */
struct MCPResourceContent {
    std::string uri;           // Resource URI
    std::string mimeType;      // Content type
    std::string text;          // Text content (for text/* and application/json)
    std::string blob;          // Binary content (base64 encoded)
    
    json ToMCPFormat() const;
};

/**
 * @brief MCP Resource Registry
 * 
 * Manages resource definitions and provides read-only access to debugging context
 */
class MCPResourceRegistry {
public:
    static MCPResourceRegistry& Instance();
    
    /**
     * @brief Register a direct resource
     */
    void RegisterResource(const MCPResourceDefinition& resource);
    
    /**
     * @brief Register a resource template
     */
    void RegisterTemplate(const MCPResourceTemplate& template_def);
    
    /**
     * @brief Register default x64dbg resources
     */
    void RegisterDefaultResources();
    
    /**
     * @brief Find a resource by URI
     */
    std::optional<MCPResourceDefinition> FindResource(const std::string& uri) const;
    
    /**
     * @brief Find a resource template by name or URI pattern
     */
    std::optional<MCPResourceTemplate> FindTemplate(const std::string& uriOrName) const;
    
    /**
     * @brief Generate resources/list response
     */
    json GenerateResourcesListResponse() const;
    
    /**
     * @brief Generate resources/templates/list response
     */
    json GenerateTemplatesListResponse() const;
    
    /**
     * @brief Read a resource by URI
     * @param uri Resource URI
     * @return Resource content or error
     */
    MCPResourceContent ReadResource(const std::string& uri) const;

private:
    MCPResourceRegistry() = default;
    
    std::vector<MCPResourceDefinition> m_resources;
    std::vector<MCPResourceTemplate> m_templates;
};

} // namespace MCP
