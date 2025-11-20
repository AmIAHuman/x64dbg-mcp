#include "MessageTransport.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"

namespace MCP {

std::vector<uint8_t> MessageTransport::Encode(const std::string& message) {
    if (message.size() > MAX_MESSAGE_SIZE) {
        throw InvalidSizeException("Message too large: " + std::to_string(message.size()));
    }
    
    std::vector<uint8_t> encoded;
    encoded.resize(LENGTH_PREFIX_SIZE + message.size());
    
    // 写入长度前缀（大端）
    WriteUInt32BE(encoded.data(), static_cast<uint32_t>(message.size()));
    
    // 写入消息内容
    std::memcpy(encoded.data() + LENGTH_PREFIX_SIZE, message.data(), message.size());
    
    return encoded;
}

bool MessageTransport::Decode(const uint8_t* data, size_t size, 
                             std::string& message, size_t& bytesConsumed)
{
    bytesConsumed = 0;
    
    // 检查是否有完整消息
    size_t messageSize = GetCompleteMessageSize(data, size);
    if (messageSize == 0) {
        return false; // 不完整
    }
    
    // 读取长度
    uint32_t payloadLength = ReadUInt32BE(data);
    
    // 提取消息内容
    message.assign(reinterpret_cast<const char*>(data + LENGTH_PREFIX_SIZE), payloadLength);
    bytesConsumed = messageSize;
    
    return true;
}

size_t MessageTransport::GetCompleteMessageSize(const uint8_t* data, size_t size) {
    // 需要至少 4 字节来读取长度
    if (size < LENGTH_PREFIX_SIZE) {
        return 0;
    }
    
    // 读取消息长度
    uint32_t payloadLength = ReadUInt32BE(data);
    
    // 验证长度
    if (payloadLength > MAX_MESSAGE_SIZE) {
        Logger::Error("Message too large: {}", payloadLength);
        throw InvalidSizeException("Message exceeds maximum size");
    }
    
    // 检查是否有完整的消息
    size_t totalSize = LENGTH_PREFIX_SIZE + payloadLength;
    if (size < totalSize) {
        return 0; // 不完整
    }
    
    return totalSize;
}

uint32_t MessageTransport::ReadUInt32BE(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           (static_cast<uint32_t>(data[3]));
}

void MessageTransport::WriteUInt32BE(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(value & 0xFF);
}

} // namespace MCP
