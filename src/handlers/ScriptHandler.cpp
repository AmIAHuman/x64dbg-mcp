#include "ScriptHandler.h"
#include "../core/Logger.h"
#include <sstream>

#ifdef XDBG_SDK_AVAILABLE
#include "_plugins.h"
#include "bridgemain.h"
#endif

std::string ScriptHandler::lastResult = "";
bool ScriptHandler::lastSuccess = false;

json ScriptHandler::execute(const json& params) {
    try {
        if (!params.contains("command") || !params["command"].is_string()) {
            return {
                {"success", false},
                {"error", "Missing or invalid 'command' parameter"}
            };
        }

        std::string command = params["command"];
        MCP::Logger::Debug("Executing script command: {}", command);

        // Execute the command
        bool success = DbgCmdExec(command.c_str());
        
        lastSuccess = success;
        lastResult = success ? "Command executed successfully" : "Command execution failed";

        json result = {
            {"success", success},
            {"command", command}
        };

        if (!success) {
            result["error"] = "Command execution failed";
        }

        // If command has output, try to capture it
        if (params.contains("capture_output") && params["capture_output"].is_boolean() && params["capture_output"]) {
            // Note: x64dbg doesn't provide direct command output capture
            // This would require additional implementation
            result["output"] = lastResult;
        }

        return result;

    } catch (const std::exception& e) {
        MCP::Logger::Error("Script execution error: {}", e.what());
        return {
            {"success", false},
            {"error", e.what()}
        };
    }
}

json ScriptHandler::executeBatch(const json& params) {
    try {
        if (!params.contains("commands") || !params["commands"].is_array()) {
            return {
                {"success", false},
                {"error", "Missing or invalid 'commands' parameter (must be array)"}
            };
        }

        json::array_t commands = params["commands"];
        json results = json::array();
        bool allSuccess = true;
        int successCount = 0;
        int failCount = 0;

        bool stopOnError = false;
        if (params.contains("stop_on_error") && params["stop_on_error"].is_boolean()) {
            stopOnError = params["stop_on_error"];
        }

        for (const auto& cmd : commands) {
            if (!cmd.is_string()) {
                results.push_back({
                    {"success", false},
                    {"command", ""},
                    {"error", "Invalid command (not a string)"}
                });
                allSuccess = false;
                failCount++;
                if (stopOnError) break;
                continue;
            }

            std::string command = cmd;
            bool success = DbgCmdExec(command.c_str());

            json cmdResult = {
                {"success", success},
                {"command", command}
            };

            if (!success) {
                cmdResult["error"] = "Command execution failed";
                allSuccess = false;
                failCount++;
                if (stopOnError) {
                    results.push_back(cmdResult);
                    break;
                }
            } else {
                successCount++;
            }

            results.push_back(cmdResult);
        }
        
        // 更新 lastResult 和 lastSuccess
        lastSuccess = allSuccess;
        if (allSuccess) {
            lastResult = "All " + std::to_string(successCount) + " commands executed successfully";
        } else {
            lastResult = std::to_string(successCount) + " succeeded, " + std::to_string(failCount) + " failed";
        }

        return {
            {"success", allSuccess},
            {"total", commands.size()},
            {"succeeded", successCount},
            {"failed", failCount},
            {"results", results}
        };

    } catch (const std::exception& e) {
        MCP::Logger::Error("Batch script execution error: {}", e.what());
        return {
            {"success", false},
            {"error", e.what()}
        };
    }
}

json ScriptHandler::getLastResult(const json& params) {
    json result = {
        {"success", lastSuccess},
        {"result", lastResult}
    };
    
    // 当失败时，添加 error 字段提供更多信息
    if (!lastSuccess) {
        result["error"] = lastResult.empty() ? "No script executed yet or last execution failed" : lastResult;
    }
    
    return result;
}
