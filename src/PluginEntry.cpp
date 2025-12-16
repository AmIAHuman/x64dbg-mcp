#include "core/Logger.h"
#include "core/ConfigManager.h"
#include "core/MethodDispatcher.h"
#include "core/PermissionChecker.h"
#include "core/MCPToolRegistry.h"
#include "core/MCPResourceRegistry.h"
#include "core/MCPPromptRegistry.h"
#include "core/X64DBGBridge.h"
#include "handlers/DebugHandler.h"
#include "handlers/RegisterHandler.h"
#include "handlers/MemoryHandler.h"
#include "handlers/BreakpointHandler.h"
#include "handlers/DisassemblyHandler.h"
#include "handlers/StackHandler.h"
#include "handlers/ThreadHandler.h"
#include "handlers/ModuleHandler.h"
#include "handlers/EventCallbackHandler.h"
#include "handlers/ScriptHandler.h"
#include "handlers/ContextHandler.h"
#include "handlers/DumpHandler.h"
#include "communication/MCPHttpServer.h"
#include "ui/ConfigEditor.h"
#include <windows.h>
#include <filesystem>
#include <fstream>

// 插件版本信息
#define PLUGIN_VERSION "1.0.1"

// 可由 CMake 覆盖：PLUGIN_DISPLAY_NAME, PLUGIN_DIR_NAME
#ifndef PLUGIN_DISPLAY_NAME
#define PLUGIN_DISPLAY_NAME "x64dbg MCP Server"
#endif

#ifndef PLUGIN_DIR_NAME
#define PLUGIN_DIR_NAME "x64dbg-mcp"
#endif

#ifndef PLUGIN_NAME
#define PLUGIN_NAME PLUGIN_DISPLAY_NAME
#endif

// 全局变量
static int g_pluginHandle = 0;
static int g_menuHandle = 0;
static std::unique_ptr<MCP::MCPHttpServer> g_mcpHttpServer;
static HMODULE g_hModule = NULL;  // 插件模块句柄

// 菜单命令ID - 使用唯一的ID值
enum MenuCommands {
    MENU_START_MCP_HTTP = 1,
    MENU_STOP_MCP_HTTP = 2,
    MENU_EDIT_CONFIG = 3,
    MENU_SHOW_CONFIG = 4,
    MENU_ABOUT = 5
};

namespace MCP {

/**
 * @brief x64dbg 回调: 初始化调试
 */
static void CB_InitDebug(CBTYPE cbType, void* callbackInfo) {
    Logger::Info("Debug session started");
}

/**
 * @brief x64dbg 回调: 停止调试
 */
static void CB_StopDebug(CBTYPE cbType, void* callbackInfo) {
    Logger::Info("Debug session stopped");
}

/**
 * @brief x64dbg 回调: 断点命中
 */
static void CB_Breakpoint(CBTYPE cbType, void* callbackInfo) {
    // TODO: 需要根据实际SDK结构实现
    MCP::Logger::Info("Breakpoint hit");
}

/**
 * @brief x64dbg 回调: 异常
 */
static void CB_Exception(CBTYPE cbType, void* callbackInfo) {
    // TODO: 需要根据实际SDK结构实现
    MCP::Logger::Info("Exception occurred");
}

/**
 * @brief x64dbg 回调: 模块加载
 */
static void CB_LoadDll(CBTYPE cbType, void* callbackInfo) {
    // TODO: 需要根据实际SDK结构实现
    MCP::Logger::Info("DLL loaded");
}

/**
 * @brief x64dbg 回调: 模块卸载
 */
static void CB_UnloadDll(CBTYPE cbType, void* callbackInfo) {
    // TODO: 需要根据实际SDK结构实现
    MCP::Logger::Info("DLL unloaded");
}

/**
 * @brief x64dbg 回调: 进程创建
 */
static void CB_CreateProcess(CBTYPE cbType, void* callbackInfo) {
    EventCallbackHandler::OnCreateProcess();
}

/**
 * @brief x64dbg 回调: 进程退出
 */
static void CB_ExitProcess(CBTYPE cbType, void* callbackInfo) {
    EventCallbackHandler::OnExitProcess();
}

/**
 * @brief x64dbg 回调: 菜单项点击
 */
static void CB_MenuEntry(CBTYPE cbType, void* callbackInfo) {
    auto* info = static_cast<PLUG_CB_MENUENTRY*>(callbackInfo);
    
    try {
        if (info->hEntry == MENU_EDIT_CONFIG) {
            // 编辑配置
            _plugin_logputs("[MCP] Opening config editor...");
            
            auto& config = ConfigManager::Instance();
            
            // 构建配置文件路径
            std::string configPath = config.GetConfigPath();
            if (configPath.empty()) {
                // 如果配置未加载,使用默认路径
                configPath = std::string(PLUGIN_DIR_NAME) + "\\config.json";
                _plugin_logprintf("[MCP] Using default config path: %s\n", configPath.c_str());
            }
            
            // 获取x64dbg主窗口句柄
            HWND hwndDlg = GuiGetWindowHandle();
            
            if (MCP::ConfigEditor::Show(g_hModule, hwndDlg, configPath)) {
                _plugin_logputs("[MCP] Configuration updated");
                // 重新加载配置
                config.Load(configPath);
                
                // 如果服务器正在运行,提示需要重启
                if (g_mcpHttpServer && g_mcpHttpServer->IsRunning()) {
                    MessageBoxA(hwndDlg, 
                        "Configuration saved. Please restart MCP HTTP Server for changes to take effect.", 
                        "Config Updated", 
                        MB_OK | MB_ICONINFORMATION);
                }
            } else {
                _plugin_logputs("[MCP] Config editor cancelled or failed");
            }
        }
        else if (info->hEntry == MENU_SHOW_CONFIG) {
            // 显示配置
            auto& config = ConfigManager::Instance();
            
            _plugin_logputs("[MCP] Current Configuration:");
            _plugin_logprintf("  Address: %s\n", config.Get<std::string>("server.address", "127.0.0.1").c_str());
            _plugin_logprintf("  Port: %d\n", config.Get<int>("server.port", 3000));
        }
        else if (info->hEntry == MENU_START_MCP_HTTP) {
            // 启动 MCP HTTP 服务器
            if (!g_mcpHttpServer) {
                g_mcpHttpServer = std::make_unique<MCP::MCPHttpServer>();
            }
            
            if (g_mcpHttpServer->IsRunning()) {
                _plugin_logputs("[MCP] HTTP Server is already running");
                return;
            }
            
            if (g_mcpHttpServer->Start("127.0.0.1", 3000)) {
                _plugin_logputs("[MCP] HTTP Server started on http://127.0.0.1:3000");
                _plugin_logputs("[MCP] Configure VSCode/Claude to connect via SSE");
            } else {
                _plugin_logputs("[MCP] Failed to start HTTP Server");
            }
        }
        else if (info->hEntry == MENU_STOP_MCP_HTTP) {
            // 停止 MCP HTTP 服务器
            if (!g_mcpHttpServer || !g_mcpHttpServer->IsRunning()) {
                _plugin_logputs("[MCP] HTTP Server is not running");
                return;
            }
            
            g_mcpHttpServer->Stop();
            _plugin_logputs("[MCP] HTTP Server stopped");
        }
        else if (info->hEntry == MENU_ABOUT) {
            // 关于
            char aboutMsg[256];
            sprintf_s(aboutMsg, "[MCP] %s Plugin v1.0.1", PLUGIN_DISPLAY_NAME);
            _plugin_logputs(aboutMsg);
            _plugin_logputs("[MCP] Provides JSON-RPC debugging interface");
            _plugin_logputs("[MCP] https://github.com/SetsunaYukiOvO/x64dbg-mcp");
        }
    } catch (const std::exception& e) {
        _plugin_logprintf("[MCP] Menu callback error: %s\n", e.what());
    }
}

} // namespace MCP

/**
 * @brief 注册所有 JSON-RPC 方法
 */
static void RegisterAllMethods() {
    // 注册 MCP 工具定义
    MCP::MCPToolRegistry::Instance().RegisterDefaultTools();
    
    // 注册 MCP 资源和提示词
    MCP::MCPResourceRegistry::Instance().RegisterDefaultResources();
    MCP::MCPPromptRegistry::Instance().RegisterDefaultPrompts();
    
    // 注册 JSON-RPC 方法处理器
    MCP::DebugHandler::RegisterMethods();
    MCP::RegisterHandler::RegisterMethods();
    MCP::MemoryHandler::RegisterMethods();
    MCP::BreakpointHandler::RegisterMethods();
    MCP::DisassemblyHandler::RegisterMethods();
    MCP::SymbolHandler::RegisterMethods();
    MCP::StackHandler::RegisterMethods();
    MCP::ThreadHandler::RegisterMethods();
    MCP::ModuleHandler::RegisterMethods();
    MCP::DumpHandler::RegisterMethods();
    
    // Register script execution methods
    auto& dispatcher = MCP::MethodDispatcher::Instance();
    dispatcher.RegisterMethod("script.execute", [](const json& params) -> json {
        return ScriptHandler::execute(params);
    });
    dispatcher.RegisterMethod("script.execute_batch", [](const json& params) -> json {
        return ScriptHandler::executeBatch(params);
    });
    dispatcher.RegisterMethod("script.get_last_result", [](const json& params) -> json {
        return ScriptHandler::getLastResult(params);
    });
    
    // Register context snapshot methods
    dispatcher.RegisterMethod("context.get_snapshot", [](const json& params) -> json {
        return ContextHandler::getSnapshot(params);
    });
    dispatcher.RegisterMethod("context.get_basic", [](const json& params) -> json {
        return ContextHandler::getBasicContext(params);
    });
    dispatcher.RegisterMethod("context.compare_snapshots", [](const json& params) -> json {
        return ContextHandler::compareSnapshots(params);
    });
    
    MCP::Logger::Info("All JSON-RPC methods registered");
}

/**
 * @brief 注册所有回调
 */
static void RegisterCallbacks() {
    _plugin_registercallback(g_pluginHandle, CB_INITDEBUG, MCP::CB_InitDebug);
    _plugin_registercallback(g_pluginHandle, CB_STOPDEBUG, MCP::CB_StopDebug);
    _plugin_registercallback(g_pluginHandle, CB_BREAKPOINT, MCP::CB_Breakpoint);
    _plugin_registercallback(g_pluginHandle, CB_EXCEPTION, MCP::CB_Exception);
    _plugin_registercallback(g_pluginHandle, CB_CREATEPROCESS, MCP::CB_CreateProcess);
    _plugin_registercallback(g_pluginHandle, CB_EXITPROCESS, MCP::CB_ExitProcess);
    _plugin_registercallback(g_pluginHandle, CB_LOADDLL, MCP::CB_LoadDll);
    _plugin_registercallback(g_pluginHandle, CB_UNLOADDLL, MCP::CB_UnloadDll);
    _plugin_registercallback(g_pluginHandle, CB_MENUENTRY, MCP::CB_MenuEntry);  // 注册菜单回调
    
    MCP::Logger::Info("All callbacks registered");
}

// ==================== x64dbg 插件导出函数 ====================

/**
 * @brief 插件初始化
 * @return true表示初始化成功，false表示失败
 */
extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* initStruct) {
    // ✅ 必须填写插件信息，否则x64dbg无法识别插件
    initStruct->sdkVersion = PLUG_SDKVERSION;
    initStruct->pluginVersion = 1;  // 插件版本号
    strcpy_s(initStruct->pluginName, PLUGIN_NAME);  // 插件名称
    
    g_pluginHandle = initStruct->pluginHandle;
    
    try {
        // 获取当前模块路径，用于日志文件
        char logPath[MAX_PATH] = {0};
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&pluginit, &hModule)) {
            GetModuleFileNameA(hModule, logPath, MAX_PATH);
            // 替换文件名为日志文件名
            char* lastSlash = strrchr(logPath, '\\');
            if (lastSlash) {
                std::string logName = std::string(PLUGIN_DIR_NAME) + ".log";
                strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash - logPath + 1), logName.c_str());
            }
        }
        
        // 如果获取路径失败，使用默认路径
        if (logPath[0] == 0) {
            std::string logName = std::string(PLUGIN_DIR_NAME) + ".log";
            strcpy_s(logPath, logName.c_str());
        }
        
        // 初始化日志系统
        MCP::Logger::Initialize(logPath, MCP::LogLevel::Info, true);
        char initMsg[256];
        sprintf_s(initMsg, "%s Plugin v1.0.1 initializing...", PLUGIN_DISPLAY_NAME);
        MCP::Logger::Info(initMsg);
        _plugin_logprintf("[MCP] Log file: %s\n", logPath);
        
        _plugin_logprintf("[MCP] %s v%s\n", PLUGIN_DISPLAY_NAME, PLUGIN_VERSION);
        _plugin_logputs("[MCP] https://github.com/SetsunaYukiOvO/x64dbg-mcp");
        
        // 加载配置（如果不存在则创建默认配置）
        try {
            auto& config = MCP::ConfigManager::Instance();
            std::string configPath = std::string(PLUGIN_DIR_NAME) + "\\config.json";
            
            // 检查配置文件是否存在
            if (!std::filesystem::exists(configPath)) {
                MCP::Logger::Warning("Config file not found, creating default config.json");
                _plugin_logputs("[MCP] Creating default config.json...");
                
                // 创建目录
                std::filesystem::create_directories(PLUGIN_DIR_NAME);
                
                // 创建默认配置
                std::ofstream configFile(configPath);
                if (configFile.is_open()) {
                    configFile << R"({
  "version": "1.0.1",
  "server": {
    "address": "127.0.0.1",
    "port": 3000
  },
  "permissions": {
    "allow_memory_write": true,
    "allow_register_write": true,
    "allow_script_execution": true,
    "allow_breakpoint_modification": true,
    "allowed_methods": [
      "debug.*",
      "register.*",
      "memory.*",
      "breakpoint.*",
      "disasm.*",
      "disassembly.*",
      "module.*",
      "symbol.*",
      "thread.*",
      "stack.*",
      "comment.*",
      "script.*",
      "context.*",
      "dump.*"
    ]
  },
    "logging": {
        "enabled": true,
        "level": "info",
        "file": "plugin.log",
        "max_file_size_mb": 10,
        "console_output": true
    },
  "timeout": {
    "request_timeout_ms": 30000,
    "step_timeout_ms": 10000,
    "memory_read_timeout_ms": 5000
  },
  "features": {
    "enable_notifications": true,
    "enable_heartbeat": true,
    "heartbeat_interval_seconds": 30,
    "enable_batch_requests": true
  }
})";
                    configFile.close();
                    MCP::Logger::Info("Default config.json created successfully");
                    _plugin_logputs("[MCP] Default config.json created");
                } else {
                    MCP::Logger::Error("Failed to create config.json");
                    _plugin_logputs("[MCP] Failed to create config.json");
                }
            }
            
            config.Load(configPath);
            
            // 设置日志级别
            std::string logLevel = config.Get<std::string>("logging.level", "info");
            // 转换字符串到枚举
            MCP::LogLevel level = MCP::LogLevel::Info;
            if (logLevel == "trace") level = MCP::LogLevel::Trace;
            else if (logLevel == "debug") level = MCP::LogLevel::Debug;
            else if (logLevel == "warning") level = MCP::LogLevel::Warning;
            else if (logLevel == "error") level = MCP::LogLevel::Error;
            else if (logLevel == "critical") level = MCP::LogLevel::Critical;
            MCP::Logger::SetLevel(level);
        } catch (const std::exception& e) {
            MCP::Logger::Warning("Failed to load config: {}, using defaults", e.what());
        }
        
        // 初始化权限检查器
        try {
            MCP::PermissionChecker::Instance().Initialize();
            MCP::Logger::Info("PermissionChecker initialized");
        } catch (const std::exception& e) {
            MCP::Logger::Error("Failed to initialize PermissionChecker: {}", e.what());
            _plugin_logprintf("[MCP] Failed to initialize PermissionChecker: %s\n", e.what());
        }
        
        // 注册所有 JSON-RPC 方法
        try {
            RegisterAllMethods();
        } catch (const std::exception& e) {
            MCP::Logger::Error("Failed to register methods: {}", e.what());
            _plugin_logprintf("[MCP] Failed to register methods: %s\n", e.what());
        }
        
        // 注册 x64dbg 回调
        try {
            RegisterCallbacks();
        } catch (const std::exception& e) {
            MCP::Logger::Error("Failed to register callbacks: {}", e.what());
            _plugin_logprintf("[MCP] Failed to register callbacks: %s\n", e.what());
        }
        
        // 初始化事件处理器
        try {
            MCP::EventCallbackHandler::Instance().Initialize();
        } catch (const std::exception& e) {
            MCP::Logger::Error("Failed to initialize event handler: {}", e.what());
            _plugin_logprintf("[MCP] Failed to initialize event handler: %s\n", e.what());
        }
        
        MCP::Logger::Info("Plugin initialized successfully");
        _plugin_logputs("[MCP] Plugin initialized successfully");
        return true;  // ✅ 返回true表示初始化成功
    } catch (const std::exception& e) {
        // 捕获所有未处理的异常，防止崩溃
        _plugin_logprintf("[MCP] FATAL ERROR during initialization: %s\n", e.what());
        return false;  // ✅ 返回false表示初始化失败
    } catch (...) {
        _plugin_logputs("[MCP] FATAL ERROR: Unknown exception during initialization");
        return false;  // ✅ 返回false表示初始化失败
    }
}

/**
 * @brief 插件停止
 */
extern "C" __declspec(dllexport) void plugstop() {
    MCP::Logger::Info("Plugin stopping...");
    
    // 停止 MCP HTTP 服务器
    try {
        if (g_mcpHttpServer && g_mcpHttpServer->IsRunning()) {
            g_mcpHttpServer->Stop();
        }
        g_mcpHttpServer.reset();
    } catch (...) {}
    
    // 清理事件处理器
    MCP::EventCallbackHandler::Instance().Cleanup();
    
    MCP::Logger::Info("Plugin stopped");
    MCP::Logger::Shutdown();
}

/**
 * @brief 插件设置（创建菜单）
 * @return true表示设置成功，false表示失败
 */
extern "C" __declspec(dllexport) bool plugsetup(PLUG_SETUPSTRUCT* setupStruct) {
    _plugin_logputs("[MCP] Setting up plugin menu...");
    
    // 添加插件菜单
    g_menuHandle = _plugin_menuadd(setupStruct->hMenu, "&MCP Server");
    
    if (g_menuHandle) {
        // ✅ 使用 _plugin_menuaddentry 添加可点击的菜单项
        _plugin_menuaddentry(g_menuHandle, MENU_START_MCP_HTTP, "Start &MCP HTTP Server");
        _plugin_menuaddentry(g_menuHandle, MENU_STOP_MCP_HTTP, "Stop M&CP HTTP Server");
        _plugin_menuaddseparator(g_menuHandle);  // 分隔符
        _plugin_menuaddentry(g_menuHandle, MENU_EDIT_CONFIG, "&Edit Config");
        _plugin_menuaddentry(g_menuHandle, MENU_SHOW_CONFIG, "Show &Config");
        _plugin_menuaddseparator(g_menuHandle);  // 分隔符
        _plugin_menuaddentry(g_menuHandle, MENU_ABOUT, "&About");
        
        _plugin_logputs("[MCP] Plugin menu created successfully");
        _plugin_logprintf("[MCP] Menu handle: %d\n", g_menuHandle);
        _plugin_logprintf("[MCP] Menu entries added\n");
    } else {
        _plugin_logputs("[MCP] ERROR: Failed to create plugin menu!");
        return false;  // ✅ 菜单创建失败
    }
    
    // 使用x64dbg的日志系统，因为自定义Logger可能还未初始化
    try {
        MCP::Logger::Debug("Plugin menu created");
    } catch (...) {
        // 如果Logger未初始化，忽略错误
    }
    
    return true;  // ✅ 返回true表示设置成功
}

/**
 * @brief DLL 主函数
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;  // 保存模块句柄
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
