#include "DebugHandler.h"
#include "../business/DebugController.h"
#include "../core/MethodDispatcher.h"
#include "../core/RequestValidator.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"
#include "bridgemain.h"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace MCP {

void DebugHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("debug.get_state", GetState);
    dispatcher.RegisterMethod("debug.run", Run);
    dispatcher.RegisterMethod("debug.pause", Pause);
    dispatcher.RegisterMethod("debug.step_into", StepInto);
    dispatcher.RegisterMethod("debug.step_over", StepOver);
    dispatcher.RegisterMethod("debug.step_out", StepOut);
    dispatcher.RegisterMethod("debug.run_to", RunTo);
    dispatcher.RegisterMethod("debug.restart", Restart);
    dispatcher.RegisterMethod("debug.stop", Stop);
    dispatcher.RegisterMethod("debug.load_binary", LoadBinary);
    dispatcher.RegisterMethod("debug.hide_debugger", HideDebugger);

    Logger::Info("Registered debug.* methods");
}

json DebugHandler::GetState(const json& params) {
    auto& controller = DebugController::Instance();
    
    DebugState state = controller.GetState();
    std::string stateStr = StateToString(state);
    
    json result = {
        {"state", stateStr}
    };
    
    if (controller.IsDebugging()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
        } catch (...) {
            // 如果无法获取 RIP，忽略错误
        }
    }
    
    return result;
}

json DebugHandler::Run(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Run();
    
    json result = {
        {"success", success}
    };
    
    // 等待一小段时间让调试器状态稳定
    Sleep(50);
    
    // 获取当前状态
    DebugState state = controller.GetState();
    result["state"] = StateToString(state);
    
    // 如果暂停了，返回停止原因
    if (controller.IsPaused()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
            result["stop_reason"] = "breakpoint_or_exception";
        } catch (...) {
            // 忽略
        }
    }
    
    return result;
}

json DebugHandler::Pause(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Pause();
    uint64_t rip = 0;
    
    if (success && controller.IsPaused()) {
        try {
            rip = controller.GetInstructionPointer();
        } catch (...) {
            // 忽略
        }
    }
    
    json result = {
        {"success", success}
    };
    
    if (rip != 0) {
        result["rip"] = StringUtils::FormatAddress(rip);
    }
    
    return result;
}

json DebugHandler::StepInto(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepInto();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::StepOver(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepOver();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::StepOut(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepOut();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::RunTo(const json& params) {
    RequestValidator::RequireString(params, "address");
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = RequestValidator::ValidateAddress(addressStr);
    
    auto& controller = DebugController::Instance();
    bool success = controller.RunToAddress(address);
    
    return {
        {"success", success},
        {"address", StringUtils::FormatAddress(address)}
    };
}

json DebugHandler::Restart(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Restart();
    
    return {
        {"success", success}
    };
}

json DebugHandler::Stop(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Stop();
    
    return {
        {"success", success}
    };
}

json DebugHandler::LoadBinary(const json& params) {
    RequestValidator::RequireString(params, "path");

    std::string path = params["path"].get<std::string>();
    std::string args;
    int timeout = 10000;

    if (params.contains("args") && params["args"].is_string()) {
        args = params["args"].get<std::string>();
    }
    if (params.contains("timeout") && params["timeout"].is_number_integer()) {
        timeout = params["timeout"].get<int>();
        if (timeout <= 0) timeout = 10000;
    }

    // Escape quotes in the path
    std::string escapedPath = path;
    size_t quotePos = 0;
    while ((quotePos = escapedPath.find('"', quotePos)) != std::string::npos) {
        escapedPath.replace(quotePos, 1, "\\\"");
        quotePos += 2;
    }

    // Build the init command
    std::string initCommand;
    if (args.empty()) {
        initCommand = "init \"" + escapedPath + "\"";
    } else {
        std::string escapedArgs = args;
        quotePos = 0;
        while ((quotePos = escapedArgs.find('"', quotePos)) != std::string::npos) {
            escapedArgs.replace(quotePos, 1, "\\\"");
            quotePos += 2;
        }
        initCommand = "init \"" + escapedPath + "\", \"" + escapedArgs + "\"";
    }

    Logger::Info("LoadBinary: executing '{}'", initCommand);

    if (!DbgCmdExec(initCommand.c_str())) {
        return {
            {"success", false},
            {"error", "Failed to execute init command"}
        };
    }

    // Poll until debugger is active or timeout
    auto start = std::chrono::steady_clock::now();
    auto& controller = DebugController::Instance();

    while (!controller.IsDebugging()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        if (elapsed >= timeout) {
            return {
                {"success", false},
                {"error", "Timeout waiting for debugger to attach"}
            };
        }
        Sleep(10);
    }

    // Wait a bit for the debugger state to stabilize
    Sleep(50);

    json result = {
        {"success", true},
        {"path", path},
        {"state", "paused"}
    };

    // Try to get instruction pointer
    if (controller.IsPaused()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
        } catch (...) {
            // Ignore
        }
    }

    return result;
}

json DebugHandler::HideDebugger(const json& params) {
    // Determine profile
    std::string profile = "MCP";
    if (params.contains("profile") && params["profile"].is_string()) {
        profile = params["profile"].get<std::string>();
    }

    // Parse technique categories (default: all enabled)
    bool enablePeb = true, enableNtquery = true, enableTiming = true;
    bool enableHardware = true, enableWindow = true, enableHandle = true;
    bool enableThread = true, enableMisc = true;

    if (params.contains("techniques") && params["techniques"].is_array()) {
        // If techniques array provided, start with all disabled
        enablePeb = enableNtquery = enableTiming = false;
        enableHardware = enableWindow = enableHandle = false;
        enableThread = enableMisc = false;

        for (const auto& t : params["techniques"]) {
            if (!t.is_string()) continue;
            std::string tech = t.get<std::string>();
            if (tech == "peb" || tech == "heap") enablePeb = true;
            else if (tech == "ntquery") enableNtquery = true;
            else if (tech == "timing") enableTiming = true;
            else if (tech == "hardware") enableHardware = true;
            else if (tech == "window") enableWindow = true;
            else if (tech == "handle") enableHandle = true;
            else if (tech == "thread" || tech == "api_hooks") enableThread = true;
            else if (tech == "misc") enableMisc = true;
            else if (tech == "all") {
                enablePeb = enableNtquery = enableTiming = true;
                enableHardware = enableWindow = enableHandle = true;
                enableThread = enableMisc = true;
            }
        }
    }

    auto b = [](bool v) -> std::string { return v ? "1" : "0"; };

    // Build INI content
    std::string ini;
    ini += "[SETTINGS]\n";
    ini += "CurrentProfile=" + profile + "\n";
    ini += "[" + profile + "]\n";
    ini += "DLLNormal=1\n";
    ini += "DLLStealth=0\n";
    ini += "DLLUnload=0\n";

    // PEB
    ini += "PebBeingDebugged=" + b(enablePeb) + "\n";
    ini += "PebNtGlobalFlag=" + b(enablePeb) + "\n";
    ini += "PebHeapFlags=" + b(enablePeb) + "\n";
    ini += "PebStartupInfo=" + b(enablePeb) + "\n";
    ini += "PebOsBuildNumber=" + b(enablePeb) + "\n";

    // NtQuery
    ini += "NtQueryInformationProcessHook=" + b(enableNtquery) + "\n";
    ini += "NtQuerySystemInformationHook=" + b(enableNtquery) + "\n";
    ini += "NtQueryObjectHook=" + b(enableNtquery) + "\n";
    ini += "NtSetInformationProcessHook=" + b(enableNtquery) + "\n";

    // Thread / API hooks
    ini += "NtSetInformationThreadHook=" + b(enableThread) + "\n";
    ini += "NtCreateThreadExHook=" + b(enableThread) + "\n";
    ini += "PreventThreadCreation=0\n";

    // Hardware / debug registers
    ini += "NtGetContextThreadHook=" + b(enableHardware) + "\n";
    ini += "NtSetContextThreadHook=" + b(enableHardware) + "\n";
    ini += "KiUserExceptionDispatcherHook=" + b(enableHardware) + "\n";
    ini += "NtContinueHook=" + b(enableHardware) + "\n";

    // Timing
    ini += "GetTickCountHook=" + b(enableTiming) + "\n";
    ini += "GetTickCount64Hook=" + b(enableTiming) + "\n";
    ini += "GetLocalTimeHook=" + b(enableTiming) + "\n";
    ini += "GetSystemTimeHook=" + b(enableTiming) + "\n";
    ini += "NtQuerySystemTimeHook=" + b(enableTiming) + "\n";
    ini += "NtQueryPerformanceCounterHook=" + b(enableTiming) + "\n";

    // Window detection
    ini += "NtUserFindWindowExHook=" + b(enableWindow) + "\n";
    ini += "NtUserBuildHwndListHook=" + b(enableWindow) + "\n";
    ini += "NtUserQueryWindowHook=" + b(enableWindow) + "\n";
    ini += "NtUserGetForegroundWindowHook=" + b(enableWindow) + "\n";

    // Handle
    ini += "NtCloseHook=" + b(enableHandle) + "\n";

    // Misc
    ini += "OutputDebugStringHook=" + b(enableMisc) + "\n";
    ini += "NtYieldExecutionHook=" + b(enableMisc) + "\n";
    ini += "NtSetDebugFilterStateHook=" + b(enableMisc) + "\n";
    ini += "NtUserBlockInputHook=" + b(enableMisc) + "\n";
    ini += "KillAntiAttach=" + b(enableMisc) + "\n";
    ini += "RemoveDebugPrivileges=" + b(enableMisc) + "\n";

    // Find ScyllaHide INI path (same directory as our plugin)
    char modulePath[MAX_PATH] = {};
    HMODULE hModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&HideDebugger, &hModule);
    GetModuleFileNameA(hModule, modulePath, MAX_PATH);

    std::filesystem::path pluginDir = std::filesystem::path(modulePath).parent_path();
    std::string iniPath = (pluginDir / "scylla_hide.ini").string();

    // Write INI
    std::ofstream file(iniPath);
    if (!file.is_open()) {
        return {
            {"success", false},
            {"error", "Failed to write ScyllaHide INI at: " + iniPath}
        };
    }
    file << ini;
    file.close();

    Logger::Info("Wrote ScyllaHide config to: {}", iniPath);

    // Build applied techniques list
    json applied = json::array();
    if (enablePeb) applied.push_back("peb");
    if (enableNtquery) applied.push_back("ntquery");
    if (enableTiming) applied.push_back("timing");
    if (enableHardware) applied.push_back("hardware");
    if (enableWindow) applied.push_back("window");
    if (enableHandle) applied.push_back("handle");
    if (enableThread) applied.push_back("thread");
    if (enableMisc) applied.push_back("misc");

    return {
        {"success", true},
        {"profile", profile},
        {"ini_path", iniPath},
        {"applied", applied}
    };
}

std::string DebugHandler::StateToString(DebugState state) {
    switch (state) {
        case DebugState::Stopped:
            return "stopped";
        case DebugState::Running:
            return "running";
        case DebugState::Paused:
            return "paused";
        default:
            return "unknown";
    }
}

} // namespace MCP
