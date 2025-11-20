#pragma once
#include <string>
#include <cstdint>

namespace MCP {

/**
 * @brief 调试器状态
 */
enum class DebugState {
    Stopped,    // 未运行
    Running,    // 正在运行
    Paused      // 已暂停
};

/**
 * @brief 调试控制器
 * 封装 x64dbg 调试控制 API
 */
class DebugController {
public:
    /**
     * @brief 获取单例实例
     */
    static DebugController& Instance();
    
    /**
     * @brief 获取调试器状态
     */
    DebugState GetState() const;
    
    /**
     * @brief 获取当前指令指针
     */
    uint64_t GetInstructionPointer() const;
    
    /**
     * @brief 继续执行
     * @return 是否成功
     */
    bool Run();
    
    /**
     * @brief 暂停执行
     * @return 是否成功
     */
    bool Pause();
    
    /**
     * @brief 单步进入
     * @return 新的指令指针
     */
    uint64_t StepInto();
    
    /**
     * @brief 单步跨越
     * @return 新的指令指针
     */
    uint64_t StepOver();
    
    /**
     * @brief 执行到返回
     * @return 新的指令指针
     */
    uint64_t StepOut();
    
    /**
     * @brief 执行到指定地址
     * @param address 目标地址
     * @return 是否成功
     */
    bool RunToAddress(uint64_t address);
    
    /**
     * @brief 重启调试
     * @return 是否成功
     */
    bool Restart();
    
    /**
     * @brief 停止调试
     * @return 是否成功
     */
    bool Stop();
    
    /**
     * @brief 检查调试器是否正在运行
     */
    bool IsDebugging() const;
    
    /**
     * @brief 检查调试器是否已暂停
     */
    bool IsPaused() const;

private:
    DebugController() = default;
    ~DebugController() = default;
    DebugController(const DebugController&) = delete;
    DebugController& operator=(const DebugController&) = delete;
    
    bool ExecuteCommand(const std::string& command);
    bool WaitForPause(uint32_t timeoutMs = 5000);
};

} // namespace MCP
