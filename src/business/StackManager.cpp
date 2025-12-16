#include "StackManager.h"
#include "../core/Exceptions.h"
#include "../core/Logger.h"
#include "RegisterManager.h"
#include "SymbolResolver.h"
#include "_scriptapi_stack.h"
#include "bridgemain.h"
#include "_scriptapi_debug.h"
#include "_scriptapi_memory.h"

namespace MCP {

StackManager& StackManager::Instance() {
    static StackManager instance;
    return instance;
}

std::vector<StackFrame> StackManager::GetStackTrace(size_t maxDepth) {
    std::vector<StackFrame> frames;
    
    // 检查调试器状态
    if (!DbgIsDebugging()) {
        throw DebuggerNotRunningException("Debugger is not debugging");
    }
    
    if (DbgIsRunning()) {
        throw DebuggerNotPausedException("Debugger must be paused to get stack trace");
    }
    
    try {
        // 使用 x64dbg Script API 获取调用栈
        Script::Debug::Wait();  // 确保已暂停
        
        // 获取当前线程的调用栈
        // x64dbg 内部会自动分析调用栈
        DBGCALLSTACK callstack;
        memset(&callstack, 0, sizeof(callstack));
        
        // 通过 DbgFunctions 获取调用栈
        if (!DbgFunctions()->GetCallStack) {
            LOG_WARNING("GetCallStack function not available, using manual stack walk");
            // 如果 API 不可用，使用手动方式
            return GetStackTraceManual(maxDepth);
        }
        
        // 尝试使用内置函数获取调用栈
        // 注意：这个 API 可能在某些版本不可用，需要降级处理
        
        // 手动栈回溯作为主要实现
        return GetStackTraceManual(maxDepth);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get stack trace: {}", e.what());
        throw;
    }
}

std::vector<StackFrame> StackManager::GetStackTraceManual(size_t maxDepth) {
    std::vector<StackFrame> frames;
    
    auto& regMgr = RegisterManager::Instance();
    auto& symResolver = SymbolResolver::Instance();
    
    // 根据架构获取正确的寄存器
#ifdef XDBG_ARCH_X64
    uint64_t rsp = regMgr.GetRegister("rsp");
    uint64_t rbp = regMgr.GetRegister("rbp");
    uint64_t rip = regMgr.GetRegister("rip");
    const size_t ptrSize = 8;
#else
    uint64_t rsp = regMgr.GetRegister("esp");
    uint64_t rbp = regMgr.GetRegister("ebp");
    uint64_t rip = regMgr.GetRegister("eip");
    const size_t ptrSize = 4;
#endif
    
    LOG_DEBUG("Stack trace starting from IP={:016X}, SP={:016X}, BP={:016X}", rip, rsp, rbp);
    
    // 添加当前帧
    StackFrame currentFrame;
    currentFrame.address = rip;
    currentFrame.from = 0;
    currentFrame.to = rip;
    currentFrame.rsp = rsp;
    currentFrame.rbp = rbp;
    currentFrame.comment = ResolveSymbol(rip);
    currentFrame.isUser = true;  // 简化判断
    currentFrame.party = 0;
    frames.push_back(currentFrame);
    
    // 栈回溯循环
    uint64_t currentRBP = rbp;
    uint64_t currentRSP = rsp;
    size_t depth = 1;
    
    while (depth < (maxDepth > 0 ? maxDepth : 100)) {  // 限制最大深度
        // 检查 RBP 是否有效
        if (currentRBP == 0 || !IsAddressOnStack(currentRBP)) {
            break;
        }
        
        // 读取返回地址（RBP 通常指向保存的上一层 RBP，返回地址在 RBP+8）
        uint64_t returnAddress = 0;
        uint64_t savedRBP = 0;
        
        // 读取保存的 RBP
        if (!Script::Memory::Read(currentRBP, &savedRBP, sizeof(savedRBP), nullptr)) {
            LOG_DEBUG("Failed to read saved RBP at {:016X}", currentRBP);
            break;
        }
        
        // 读取返回地址（在调用约定中，返回地址在 BP + 指针大小）
        if (!Script::Memory::Read(currentRBP + ptrSize, &returnAddress, sizeof(returnAddress), nullptr)) {
            LOG_DEBUG("Failed to read return address at {:016X}", currentRBP + ptrSize);
            break;
        }
        
        // 验证返回地址
        if (returnAddress == 0 || returnAddress < 0x10000) {
            LOG_DEBUG("Invalid return address: {:016X}", returnAddress);
            break;
        }
        
        // 检查返回地址是否可执行
        if (!Script::Memory::IsValidPtr(returnAddress)) {
            LOG_DEBUG("Return address {:016X} is not valid", returnAddress);
            break;
        }
        
        // 创建栈帧
        StackFrame frame;
        frame.address = returnAddress;
        frame.from = currentRBP + ptrSize;  // 返回地址所在位置
        frame.to = returnAddress;
        frame.rsp = currentRBP + (ptrSize * 2);  // 估算调用前的 SP (BP + 2*指针大小)
        frame.rbp = savedRBP;
        frame.comment = ResolveSymbol(returnAddress);
        frame.isUser = true;
        frame.party = 0;
        
        frames.push_back(frame);
        
        // 移动到上一层
        currentRBP = savedRBP;
        depth++;
        
        // 防止无限循环（检测循环引用）
        if (savedRBP <= currentRSP || savedRBP == rbp) {
            break;
        }
    }
    
    LOG_DEBUG("Stack trace complete: {} frames", frames.size());
    return frames;
}

std::vector<uint8_t> StackManager::ReadStackFrame(uint64_t frameAddress, size_t size) {
    if (!DbgIsDebugging()) {
        throw DebuggerNotRunningException("Debugger is not debugging");
    }
    
    if (!IsAddressOnStack(frameAddress)) {
        LOG_WARNING("Address {:016X} may not be on stack", frameAddress);
    }
    
    std::vector<uint8_t> data(size);
    
    duint bytesRead = 0;
    if (!Script::Memory::Read(frameAddress, data.data(), size, &bytesRead)) {
        throw MCPException("Failed to read stack frame");
    }
    
    if (bytesRead < size) {
        data.resize(static_cast<size_t>(bytesRead));
        LOG_WARNING("Only read {} of {} bytes from stack frame", bytesRead, size);
    }
    
    return data;
}

uint64_t StackManager::GetStackPointer() {
    if (!DbgIsDebugging()) {
        throw DebuggerNotRunningException("Debugger is not debugging");
    }
    
#ifdef XDBG_ARCH_X64
    return RegisterManager::Instance().GetRegister("rsp");
#else
    return RegisterManager::Instance().GetRegister("esp");
#endif
}

uint64_t StackManager::GetBasePointer() {
    if (!DbgIsDebugging()) {
        throw DebuggerNotRunningException("Debugger is not debugging");
    }
    
#ifdef XDBG_ARCH_X64
    return RegisterManager::Instance().GetRegister("rbp");
#else
    return RegisterManager::Instance().GetRegister("ebp");
#endif
}

bool StackManager::IsAddressOnStack(uint64_t address) {
    if (!DbgIsDebugging()) {
        return false;
    }
    
    try {
        uint64_t rsp = GetStackPointer();
        
        // 获取线程环境块信息来确定栈边界
        // 简化实现：假设栈大小为 1MB（典型值）
        uint64_t stackSize = 1024 * 1024;
        uint64_t stackBase = rsp + stackSize;  // 栈向下增长
        
        // 检查地址是否在合理的栈范围内
        return (address >= rsp - 0x10000) && (address <= stackBase);
        
    } catch (...) {
        return false;
    }
}

std::string StackManager::ResolveSymbol(uint64_t address) {
    try {
        auto symbol = SymbolResolver::Instance().GetSymbolFromAddress(address);
        if (symbol.has_value()) {
            return symbol.value();
        }
        // 如果解析失败，返回地址字符串
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016llX", address);
        return std::string(buf);
    } catch (...) {
        // 如果解析失败，返回地址字符串
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016llX", address);
        return std::string(buf);
    }
}

} // namespace MCP
