/**
 * @file MCPResourceRegistry.cpp
 * @brief MCP Resource Registry Implementation
 */

#include "MCPResourceRegistry.h"
#include "Logger.h"
#include "MethodDispatcher.h"
#include "JSONRPCParser.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace MCP {

json MCPResourceDefinition::ToMCPFormat() const {
    return json{
        {"uri", uri},
        {"name", name},
        {"description", description},
        {"mimeType", mimeType}
    };
}

json MCPResourceTemplate::ToMCPFormat() const {
    return json{
        {"uriTemplate", uriTemplate},
        {"name", name},
        {"description", description},
        {"mimeType", mimeType}
    };
}

json MCPResourceContent::ToMCPFormat() const {
    json content;
    content["uri"] = uri;
    content["mimeType"] = mimeType;
    
    if (!text.empty()) {
        content["text"] = text;
    }
    if (!blob.empty()) {
        content["blob"] = blob;
    }
    
    return content;
}

MCPResourceRegistry& MCPResourceRegistry::Instance() {
    static MCPResourceRegistry instance;
    return instance;
}

void MCPResourceRegistry::RegisterResource(const MCPResourceDefinition& resource) {
    m_resources.push_back(resource);
}

void MCPResourceRegistry::RegisterTemplate(const MCPResourceTemplate& template_def) {
    m_templates.push_back(template_def);
}

void MCPResourceRegistry::RegisterDefaultResources() {
    Logger::Info("Registering default MCP resources...");
    
    // 1. Debugger State Resources (Direct Resources)
    RegisterResource({
        "debugger://state/current",
        "debugger-state",
        "Current Debugger State",
        "Current debugging state including running status, process info, and execution context",
        "application/json"
    });
    
    RegisterResource({
        "debugger://registers/all",
        "registers-all",
        "All Registers",
        "Complete snapshot of all CPU registers (general purpose, flags, segments, etc.)",
        "application/json"
    });
    
    RegisterResource({
        "debugger://modules/list",
        "modules-list",
        "Loaded Modules",
        "List of all loaded modules/DLLs with base addresses and sizes",
        "application/json"
    });
    
    RegisterResource({
        "debugger://threads/list",
        "threads-list",
        "Active Threads",
        "List of all threads in the debugged process",
        "application/json"
    });
    
    RegisterResource({
        "debugger://memory/map",
        "memory-map",
        "Memory Map",
        "Complete memory map showing all allocated regions and their protections",
        "application/json"
    });
    
    RegisterResource({
        "debugger://breakpoints/all",
        "breakpoints-all",
        "All Breakpoints",
        "List of all breakpoints (software, hardware, memory) with their conditions",
        "application/json"
    });
    
    RegisterResource({
        "debugger://stack/trace",
        "stack-trace",
        "Stack Trace",
        "Current call stack with return addresses and frame information",
        "application/json"
    });
    
    // 2. Resource Templates (Dynamic Resources with Parameters)
    RegisterTemplate({
        "memory://{address}/{size}",
        "memory-read",
        "Memory Content",
        "Read memory content at specified address and size (hex format)",
        "application/json"
    });
    
    RegisterTemplate({
        "disassembly://{address}/{count}",
        "disassembly-at",
        "Disassembly Listing",
        "Disassemble instructions starting at address for count instructions",
        "text/plain"
    });
    
    RegisterTemplate({
        "module://{module_name}/info",
        "module-info",
        "Module Information",
        "Detailed information about a specific module",
        "application/json"
    });
    
    RegisterTemplate({
        "module://{module_name}/exports",
        "module-exports",
        "Module Exports",
        "Exported functions from a specific module",
        "application/json"
    });
    
    RegisterTemplate({
        "module://{module_name}/imports",
        "module-imports",
        "Module Imports",
        "Imported functions used by a specific module",
        "application/json"
    });
    
    RegisterTemplate({
        "symbol://{name}/resolve",
        "symbol-resolve",
        "Symbol Resolution",
        "Resolve symbol name to address",
        "application/json"
    });
    
    RegisterTemplate({
        "function://{address}/disassembly",
        "function-disassembly",
        "Function Disassembly",
        "Complete disassembly of a function at given address",
        "text/plain"
    });
    
    RegisterTemplate({
        "breakpoint://{address}/info",
        "breakpoint-info",
        "Breakpoint Details",
        "Detailed information about a breakpoint at specific address",
        "application/json"
    });
    
    Logger::Info("Registered {} resources and {} templates", 
                 m_resources.size(), m_templates.size());
}

std::optional<MCPResourceDefinition> MCPResourceRegistry::FindResource(const std::string& uri) const {
    for (const auto& res : m_resources) {
        if (res.uri == uri) {
            return res;
        }
    }
    return std::nullopt;
}

std::optional<MCPResourceTemplate> MCPResourceRegistry::FindTemplate(const std::string& uriOrName) const {
    for (const auto& tmpl : m_templates) {
        if (tmpl.name == uriOrName || tmpl.uriTemplate == uriOrName) {
            return tmpl;
        }
    }
    return std::nullopt;
}

json MCPResourceRegistry::GenerateResourcesListResponse() const {
    json resources_array = json::array();
    
    for (const auto& resource : m_resources) {
        resources_array.push_back(resource.ToMCPFormat());
    }
    
    return json{
        {"resources", resources_array}
    };
}

json MCPResourceRegistry::GenerateTemplatesListResponse() const {
    json templates_array = json::array();
    
    for (const auto& tmpl : m_templates) {
        templates_array.push_back(tmpl.ToMCPFormat());
    }
    
    return json{
        {"resourceTemplates", templates_array}
    };
}

MCPResourceContent MCPResourceRegistry::ReadResource(const std::string& uri) const {
    MCPResourceContent content;
    content.uri = uri;
    content.mimeType = "application/json";
    
    auto& dispatcher = MethodDispatcher::Instance();
    
    try {
        // Parse URI to determine what resource to read
        if (uri == "debugger://state/current") {
            // Call debug.get_state
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "debug.get_state";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://registers/all") {
            // Call register.list
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "register.list";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://modules/list") {
            // Call module.list
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "module.list";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://threads/list") {
            // Call thread.list
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "thread.list";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://memory/map") {
            // Call memory.enumerate
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "memory.enumerate";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://breakpoints/all") {
            // Call breakpoint.list
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "breakpoint.list";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        else if (uri == "debugger://stack/trace") {
            // Call stack.get_trace
            JSONRPCRequest request;
            request.jsonrpc = "2.0";
            request.method = "stack.get_trace";
            request.id = 1;
            request.params = json::object();
            
            JSONRPCResponse response = dispatcher.Dispatch(request);
            content.text = response.result.dump(2);
        }
        // Handle template URIs (with parameters)
        else if (uri.find("memory://") == 0) {
            // Parse: memory://{address}/{size}
            // Example: memory://0x401000/256
            content.mimeType = "application/json";
            content.text = R"({"error": "Template URI - use resources/read with proper parameters"})";
        }
        else if (uri.find("disassembly://") == 0) {
            // Parse: disassembly://{address}/{count}
            content.mimeType = "text/plain";
            content.text = "Template URI - use resources/read with proper parameters";
        }
        else {
            content.text = R"({"error": "Unknown resource URI"})";
        }
        
    } catch (const std::exception& e) {
        content.text = json{{"error", e.what()}}.dump();
    }
    
    return content;
}

} // namespace MCP
