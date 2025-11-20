#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace MCP {

/**
 * @brief 消息传输协议（Length-Prefix）
 */
class MessageTransport {
public:
    /**
     * @brief 编码消息（添加长度前缀）
     * @param message JSON 消息字符串
     * @return 编码后的字节数组
     */
    static std::vector<uint8_t> Encode(const std::string& message);
    
    /**
     * @brief 解码消息（移除长度前缀）
     * @param data 原始字节数据
     * @param size 数据大小
     * @param message 输出：解码后的消息
     * @param bytesConsumed 输出：消耗的字节数
     * @return 是否成功解码完整消息
     */
    static bool Decode(const uint8_t* data, size_t size, 
                      std::string& message, size_t& bytesConsumed);
    
    /**
     * @brief 检查缓冲区是否包含完整消息
     * @param data 缓冲区数据
     * @param size 缓冲区大小
     * @return 完整消息的大小（包含长度前缀），0 表示不完整
     */
    static size_t GetCompleteMessageSize(const uint8_t* data, size_t size);
    
private:
    static constexpr size_t LENGTH_PREFIX_SIZE = 4; // 4 字节大端长度
    static constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024; // 16 MB
    
    static uint32_t ReadUInt32BE(const uint8_t* data);
    static void WriteUInt32BE(uint8_t* data, uint32_t value);
};

} // namespace MCP
