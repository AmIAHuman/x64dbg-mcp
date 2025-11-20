#include "ResponseBuilder.h"
#include "Exceptions.h"

namespace MCP {

JSONRPCResponse ResponseBuilder::CreateSuccessResponse(const RequestId& id, const json& result) {
    JSONRPCResponse response;
    response.jsonrpc = "2.0";
    response.id = id;
    response.result = result;
    return response;
}

JSONRPCResponse ResponseBuilder::CreateErrorResponse(
    const RequestId& id,
    int code,
    const std::string& message,
    const json& data)
{
    JSONRPCResponse response;
    response.jsonrpc = "2.0";
    response.id = id;
    response.error = JSONRPCError(code, message, data);
    return response;
}

JSONRPCResponse ResponseBuilder::CreateErrorResponseFromException(
    const RequestId& id,
    const std::exception& ex)
{
    return CreateErrorResponse(id, -32603, ex.what());
}

JSONRPCResponse ResponseBuilder::CreateErrorResponseFromMCPException(
    const RequestId& id,
    const MCPException& ex)
{
    json data = {
        {"exception_type", "MCPException"}
    };
    return CreateErrorResponse(id, ex.GetCode(), ex.GetMessage(), data);
}

std::string ResponseBuilder::CreateNotification(const std::string& method, const json& params) {
    json notification = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    return notification.dump();
}

std::string ResponseBuilder::Serialize(const JSONRPCResponse& response) {
    return response.ToJson().dump();
}

std::string ResponseBuilder::SerializeBatch(const std::vector<JSONRPCResponse>& responses) {
    json batch = json::array();
    for (const auto& response : responses) {
        batch.push_back(response.ToJson());
    }
    return batch.dump();
}

} // namespace MCP
