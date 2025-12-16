#pragma once

/**
 * @file X64DBGBridge.h
 * @brief xdbg SDK 适配层（支持 x64 / x86）
 *
 * 这个文件统一管理所有 xdbg SDK 的引用,
 * 提供一个中心化的位置来处理 SDK 的包含和类型定义。
 */

// 检查 SDK 是否可用
#ifdef XDBG_SDK_AVAILABLE
    // 使用实际的 x64dbg/x32dbg SDK（通过 CMake 定义）
    #include "_plugins.h"
    #include "_dbgfunctions.h"
    #include "bridgemain.h"
    #include "_scriptapi_debug.h"
    #include "_scriptapi_memory.h"
    #include "_scriptapi_module.h"
    #include "_scriptapi_function.h"

    // SDK 已定义所有需要的类型和函数

#else
    // 临时定义 - 用于编译时没有 SDK 的情况
    #warning "xdbg SDK not available, using temporary definitions"

    #include <cstdint>
    #include <windows.h>

    // 基本类型定义：根据目标架构选择宽度
#ifdef XDBG_ARCH_X64
    using duint = uint64_t;
#else
    using duint = uint32_t;
#endif
    
    // 插件结构
    #define PLUG_EXPORT extern "C" __declspec(dllexport)
    #define PLUG_SDKVERSION 1
    
    struct PLUG_INITSTRUCT {
        int pluginHandle;
        int sdkVersion;
        int pluginVersion;
        const char* pluginName;
    };
    
    struct PLUG_SETUPSTRUCT {
        void* hwndDlg;
        int hMenu;
        int hMenuDisasm;
        int hMenuDump;
        int hMenuStack;
    };
    
    // 回调类型
    enum CBTYPE {
        CB_INITDEBUG,
        CB_STOPDEBUG,
        CB_BREAKPOINT,
        CB_EXCEPTION,
        CB_MENUENTRY,
        CB_CREATEPROCESS,
        CB_EXITPROCESS,
        CB_CREATETHREAD,
        CB_EXITTHREAD,
        CB_SYSTEMBREAKPOINT,
        CB_LOADDLL,
        CB_UNLOADDLL
    };
    
    struct CBPLUGIN {
        const char* pluginName;
        CBTYPE cbType;
        void* callbackInfo;
    };
    
    struct PLUG_CB_BREAKPOINT {
        uint64_t breakpointAddr;
    };
    
    struct PLUG_CB_EXCEPTION {
        uint32_t exceptionCode;
        uint64_t exceptionAddr;
    };
    
    struct PLUG_CB_LOADDLL {
        const char* modname;
        uint64_t modbase;
        uint64_t modsize;
    };
    
    struct PLUG_CB_UNLOADDLL {
        const char* modname;
    };
    
    // 调试函数 (临时实现)
    inline bool DbgIsDebugging() { return false; }
    inline bool DbgIsPaused() { return false; }
    inline bool DbgCmdExec(const char* cmd) { return false; }
    inline bool DbgValFromString(const char* name, uint64_t* value) { return false; }
    inline bool DbgSetRegister(const char* name, uint64_t value) { return false; }
    inline bool DbgGetRegDump(void* regdump) { return false; }
    
    // 内存函数
    inline bool DbgMemRead(uint64_t addr, void* data, size_t size) { return false; }
    inline bool DbgMemWrite(uint64_t addr, const void* data, size_t size) { return false; }
    inline bool DbgMemIsValidReadPtr(uint64_t addr) { return false; }
    inline size_t DbgMemGetPageSize(uint64_t addr) { return 0; }
    inline bool DbgMemFindBaseAddr(uint64_t addr, uint64_t* base) { return false; }
    
    // 断点函数
    inline bool DbgSetBreakpoint(uint64_t addr) { return false; }
    inline bool DbgDeleteBreakpoint(uint64_t addr) { return false; }
    inline bool DbgEnableBreakpoint(uint64_t addr) { return false; }
    inline bool DbgDisableBreakpoint(uint64_t addr) { return false; }
    inline bool DbgSetHardwareBreakpoint(uint64_t addr, int type) { return false; }
    inline bool DbgDeleteHardwareBreakpoint(uint64_t addr) { return false; }
    inline bool DbgSetMemoryBreakpoint(uint64_t addr, size_t size) { return false; }
    inline bool DbgDeleteMemoryBreakpoint(uint64_t addr) { return false; }
    inline int DbgGetBpxTypeAt(uint64_t addr) { return 0; }
    
    // 断点类型标志
    #define bp_none       0
    #define bp_normal     1
    #define bp_hardware   2
    #define bp_memory     4
    
    // 硬件断点条件
    #define hw_access     0
    #define hw_write      1
    #define hw_execute    2
    
    // 反汇编函数
    struct BASIC_INSTRUCTION_INFO {
        uint64_t addr;
        int size;
        char instruction[512];
        unsigned char dump[16];
        bool isbranch;
        bool iscall;
        bool isret;
        uint64_t branch;
        bool branch_true;
        bool branch_false;
    };
    
    inline bool DbgDisasmFastAt(uint64_t addr, BASIC_INSTRUCTION_INFO* info) { return false; }
    inline size_t DbgGetInstructionLength(uint64_t addr) { return 0; }
    
    // 符号函数
    inline bool DbgGetSymbolAt(uint64_t addr, char* name) { return false; }
    inline bool DbgGetAddrFromName(const char* name, uint64_t* addr) { return false; }
    inline const char* DbgGetModuleAt(uint64_t addr) { return nullptr; }
    inline uint64_t DbgGetFunctionStart(uint64_t addr) { return 0; }
    inline uint64_t DbgGetFunctionEnd(uint64_t addr) { return 0; }
    inline bool DbgSetLabelAt(uint64_t addr, const char* name) { return false; }
    inline bool DbgGetLabelAt(uint64_t addr, char* name) { return false; }
    inline bool DbgSetCommentAt(uint64_t addr, const char* comment) { return false; }
    inline bool DbgGetCommentAt(uint64_t addr, char* comment) { return false; }
    
    // 插件函数
    inline bool _plugin_registercallback(int handle, CBTYPE type, void* callback) { return true; }
    inline int _plugin_menuadd(int hMenu, const char* text) { return 0; }
    inline void _plugin_logprintf(const char* format, ...) { printf("%s\n", format); }
    inline void _plugin_logputs(const char* text) { puts(text); }
    
    // 寄存器结构
    struct REGDUMP {
#ifdef XDBG_ARCH_X64
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi, rbp, rsp;
        uint64_t r8, r9, r10, r11;
        uint64_t r12, r13, r14, r15;
        uint64_t rip;
#else
        uint32_t eax, ebx, ecx, edx;
        uint32_t esi, edi, ebp, esp;
        uint32_t eip;
#endif
        uint64_t eflags;
        unsigned short cs, ds, es, fs, gs, ss;
    };
    
    // 内存页结构
    struct MEMPAGE {
        uint64_t mbi_base;
        uint64_t mbi_size;
        uint32_t mbi_protect;
        uint32_t mbi_type;
    };
    
    // 符号信息
    struct SYMBOLINFO {
        char name[1024];
        char decoratedName[1024];
        char mod[256];
        uint64_t addr;
        unsigned int size;
        int type;
    };
    
    // 模块信息
    struct MODULEINFO {
        char name[256];
        char path[MAX_PATH];
        uint64_t base;
        unsigned int size;
        uint64_t entry;
    };
    
    // 断点信息
    struct BRIDGEBP {
        uint64_t addr;
        bool enabled;
        bool singleshoot;
        bool active;
        char name[256];
        char mod[256];
        unsigned short slot;
        unsigned int typeEx;
        unsigned int hitCount;
    };
    
#endif // X64DBG_SDK_AVAILABLE

namespace MCP {

/**
 * @brief xdbg SDK 辅助类
 * 提供便捷的 SDK 功能访问
 */
class XDBGHelper {
public:
    /**
     * @brief 检查 SDK 是否可用
     */
    static bool IsSDKAvailable() {
        #ifdef XDBG_SDK_AVAILABLE
            return true;
        #else
            return false;
        #endif
    }
    
    /**
     * @brief 获取 SDK 版本信息
     */
    static const char* GetSDKVersion() {
        #ifdef XDBG_SDK_AVAILABLE
            #ifdef XDBG_ARCH_X64
                return "x64dbg SDK (Real)";
            #else
                return "x32dbg SDK (Real)";
            #endif
        #else
            return "Mock SDK (临时)";
        #endif
    }
};

} // namespace MCP
