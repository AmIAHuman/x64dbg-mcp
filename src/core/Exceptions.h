#pragma once
#include <exception>
#include <string>

namespace MCP {

/**
 * @brief MCP 异常基类
 */
class MCPException : public std::exception {
public:
    explicit MCPException(const std::string& message, int code = -32603)
        : m_message(message), m_code(code) {}

    const char* what() const noexcept override {
        return m_message.c_str();
    }

    int GetCode() const noexcept {
        return m_code;
    }

    const std::string& GetMessage() const noexcept {
        return m_message;
    }

protected:
    std::string m_message;
    int m_code;
};

/**
 * @brief 调试器未运行异常
 */
class DebuggerNotRunningException : public MCPException {
public:
    explicit DebuggerNotRunningException(const std::string& message = "Debugger is not running")
        : MCPException(message, -32002) {}
};

/**
 * @brief 调试器未暂停异常
 */
class DebuggerNotPausedException : public MCPException {
public:
    explicit DebuggerNotPausedException(const std::string& message = "Debugger is not paused")
        : MCPException(message, -32001) {}
};

/**
 * @brief 权限不足异常
 */
class PermissionDeniedException : public MCPException {
public:
    explicit PermissionDeniedException(const std::string& message = "Permission denied")
        : MCPException(message, -32003) {}
};

/**
 * @brief 资源未找到异常
 */
class ResourceNotFoundException : public MCPException {
public:
    explicit ResourceNotFoundException(const std::string& message = "Resource not found")
        : MCPException(message, -32004) {}
};

/**
 * @brief 操作超时异常
 */
class OperationTimeoutException : public MCPException {
public:
    explicit OperationTimeoutException(const std::string& message = "Operation timeout")
        : MCPException(message, -32005) {}
};

/**
 * @brief 无效地址异常
 */
class InvalidAddressException : public MCPException {
public:
    explicit InvalidAddressException(const std::string& message = "Invalid memory address")
        : MCPException(message, -32010) {}
};

/**
 * @brief 无效大小异常
 */
class InvalidSizeException : public MCPException {
public:
    explicit InvalidSizeException(const std::string& message = "Invalid size")
        : MCPException(message, -32011) {}
};

/**
 * @brief 无效寄存器异常
 */
class InvalidRegisterException : public MCPException {
public:
    explicit InvalidRegisterException(const std::string& message = "Invalid register name")
        : MCPException(message, -32012) {}
};

/**
 * @brief 无效表达式异常
 */
class InvalidExpressionException : public MCPException {
public:
    explicit InvalidExpressionException(const std::string& message = "Invalid expression")
        : MCPException(message, -32013) {}
};

/**
 * @brief JSON-RPC 解析错误
 */
class ParseErrorException : public MCPException {
public:
    explicit ParseErrorException(const std::string& message = "Parse error")
        : MCPException(message, -32700) {}
};

/**
 * @brief JSON-RPC 无效请求
 */
class InvalidRequestException : public MCPException {
public:
    explicit InvalidRequestException(const std::string& message = "Invalid request")
        : MCPException(message, -32600) {}
};

/**
 * @brief JSON-RPC 方法未找到
 */
class MethodNotFoundException : public MCPException {
public:
    explicit MethodNotFoundException(const std::string& message = "Method not found")
        : MCPException(message, -32601) {}
};

/**
 * @brief JSON-RPC 无效参数
 */
class InvalidParamsException : public MCPException {
public:
    explicit InvalidParamsException(const std::string& message = "Invalid params")
        : MCPException(message, -32602) {}
};

} // namespace MCP
