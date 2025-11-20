#include "RequestValidator.h"
#include "Exceptions.h"
#include "../utils/StringUtils.h"

namespace MCP {

void RequestValidator::RequireField(const json& params, const std::string& fieldName) {
    if (!params.contains(fieldName)) {
        throw InvalidParamsException("Missing required field: " + fieldName);
    }
}

void RequestValidator::RequireString(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_string()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be a string");
    }
}

void RequestValidator::RequireNumber(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_number()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be a number");
    }
}

void RequestValidator::RequireInteger(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_number_integer()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be an integer");
    }
}

void RequestValidator::RequireBoolean(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_boolean()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be a boolean");
    }
}

void RequestValidator::RequireObject(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_object()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be an object");
    }
}

void RequestValidator::RequireArray(const json& params, const std::string& fieldName) {
    RequireField(params, fieldName);
    if (!params[fieldName].is_array()) {
        throw InvalidParamsException("Field '" + fieldName + "' must be an array");
    }
}

std::string RequestValidator::GetString(const json& params, 
                                       const std::string& fieldName, 
                                       const std::string& defaultValue)
{
    if (!params.contains(fieldName)) {
        return defaultValue;
    }
    
    if (!params[fieldName].is_string()) {
        return defaultValue;
    }
    
    return params[fieldName].get<std::string>();
}

int64_t RequestValidator::GetInteger(const json& params, 
                                    const std::string& fieldName, 
                                    int64_t defaultValue)
{
    if (!params.contains(fieldName)) {
        return defaultValue;
    }
    
    if (!params[fieldName].is_number_integer()) {
        return defaultValue;
    }
    
    return params[fieldName].get<int64_t>();
}

bool RequestValidator::GetBoolean(const json& params, 
                                 const std::string& fieldName, 
                                 bool defaultValue)
{
    if (!params.contains(fieldName)) {
        return defaultValue;
    }
    
    if (!params[fieldName].is_boolean()) {
        return defaultValue;
    }
    
    return params[fieldName].get<bool>();
}

uint64_t RequestValidator::ValidateAddress(const std::string& address) {
    try {
        return StringUtils::ParseAddress(address);
    } catch (const std::exception& e) {
        throw InvalidAddressException("Invalid address format: " + address);
    }
}

void RequestValidator::ValidateSize(size_t size, size_t maxSize) {
    if (size == 0) {
        throw InvalidSizeException("Size cannot be zero");
    }
    
    if (size > maxSize) {
        throw InvalidSizeException("Size exceeds maximum allowed: " + std::to_string(maxSize));
    }
}

} // namespace MCP
