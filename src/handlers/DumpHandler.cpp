#include "DumpHandler.h"
#include "../business/DumpManager.h"
#include "../business/ScyllaWrapper.h"
#include "../core/MethodDispatcher.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"
#include <set>

#ifdef XDBG_SDK_AVAILABLE
#include <_scriptapi_module.h>
#endif

namespace MCP {

void DumpHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();

    dispatcher.RegisterMethod("dump.module", DumpModule);
    dispatcher.RegisterMethod("dump.memory_region", DumpMemoryRegion);
    dispatcher.RegisterMethod("dump.get_dumpable_regions", GetDumpableRegions);
    dispatcher.RegisterMethod("dump.fix_imports", FixImports);
}

nlohmann::json DumpHandler::DumpModule(const nlohmann::json& params) {
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Dumping module requires write permission");
    }

    if (!params.contains("module")) {
        throw InvalidParamsException("Missing required parameter: module");
    }

    if (!params.contains("output_path")) {
        throw InvalidParamsException("Missing required parameter: output_path");
    }

    std::string module = params["module"].get<std::string>();
    std::string outputPath = params["output_path"].get<std::string>();

    // Support both flattened tool arguments and nested `options` object.
    DumpOptions options;
    nlohmann::json nestedOptions = nlohmann::json::object();
    if (params.contains("options") && !params["options"].is_null()) {
        if (!params["options"].is_object()) {
            throw InvalidParamsException("Parameter 'options' must be an object");
        }
        nestedOptions = params["options"];
    }

    auto readBoolOption = [&](const char* key, bool defaultValue) {
        if (nestedOptions.contains(key)) {
            return nestedOptions[key].get<bool>();
        }
        if (params.contains(key)) {
            return params[key].get<bool>();
        }
        return defaultValue;
    };

    options.fixImports = readBoolOption("fix_imports", true);
    options.fixRelocations = readBoolOption("fix_relocations", false);
    options.fixOEP = readBoolOption("fix_oep", true);
    options.removeIntegrityCheck = readBoolOption("remove_integrity_check", true);
    options.rebuildPE = readBoolOption("rebuild_pe", true);
    options.autoDetectOEP = readBoolOption("auto_detect_oep", false);
    options.dumpFullImage = readBoolOption("dump_full_image", false);

    const bool nestedHasOEP = nestedOptions.contains("oep");
    const bool topLevelHasOEP = params.contains("oep");
    if (nestedHasOEP || topLevelHasOEP) {
        const nlohmann::json& oepNode = nestedHasOEP ? nestedOptions["oep"] : params["oep"];
        if (!oepNode.is_string()) {
            throw InvalidParamsException("Parameter 'oep' must be a string");
        }

        const uint64_t forcedOEP = StringUtils::ParseAddress(oepNode.get<std::string>());
        options.forcedOEP = forcedOEP;
        options.fixOEP = true;
    }

    Logger::Info("[dump_module] module={}, output={}, fix_imports={}, fix_oep={}, rebuild_pe={}",
                 module, outputPath, options.fixImports, options.fixOEP, options.rebuildPE);
    if (options.forcedOEP.has_value()) {
        Logger::Info("[dump_module] forced OEP=0x{:X}", options.forcedOEP.value());
    }

    auto& manager = DumpManager::Instance();
    auto result = manager.DumpModule(module, outputPath, options, nullptr);

    if (result.success) {
        Logger::Info("[dump_module] OK: file={}, size={}", result.filePath, result.dumpedSize);
    } else {
        Logger::Error("[dump_module] FAILED: {}", result.error);
    }

    return DumpResultToJson(result);
}

nlohmann::json DumpHandler::DumpMemoryRegion(const nlohmann::json& params) {
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Dumping memory requires write permission");
    }

    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }

    if (!params.contains("size")) {
        throw InvalidParamsException("Missing required parameter: size");
    }

    if (!params.contains("output_path")) {
        throw InvalidParamsException("Missing required parameter: output_path");
    }

    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    size_t size = params["size"].get<size_t>();
    std::string outputPath = params["output_path"].get<std::string>();
    bool asRawBinary = params.value("as_raw_binary", false);

    Logger::Info("[dump_memory_region] address=0x{:X}, size={}, output={}, raw={}",
                 address, size, outputPath, asRawBinary);

    auto& manager = DumpManager::Instance();
    auto result = manager.DumpMemoryRegion(address, size, outputPath, asRawBinary);

    return DumpResultToJson(result);
}

nlohmann::json DumpHandler::GetDumpableRegions(const nlohmann::json& params) {
    uint64_t moduleBase = 0;

    if (params.contains("module_base")) {
        std::string baseStr = params["module_base"].get<std::string>();
        moduleBase = StringUtils::ParseAddress(baseStr);
    }

    auto& manager = DumpManager::Instance();
    auto regions = manager.GetDumpableRegions(moduleBase);

    nlohmann::json regionArray = nlohmann::json::array();
    for (const auto& region : regions) {
        regionArray.push_back(MemoryRegionDumpToJson(region));
    }

    nlohmann::json result;
    result["regions"] = regionArray;
    result["count"] = regions.size();

    return result;
}

nlohmann::json DumpHandler::FixImports(const nlohmann::json& params) {
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Fixing imports requires write permission");
    }

    if (!params.contains("file_path")) {
        throw InvalidParamsException("Missing required parameter: file_path");
    }
    if (!params.contains("module_base")) {
        throw InvalidParamsException("Missing required parameter: module_base");
    }

    std::string filePath = params["file_path"].get<std::string>();
    std::string baseStr = params["module_base"].get<std::string>();
    uint64_t moduleBase = StringUtils::ParseAddress(baseStr);

    std::optional<uint32_t> oepRva;
    if (params.contains("oep") && !params["oep"].is_null()) {
        uint64_t oep = StringUtils::ParseAddress(params["oep"].get<std::string>());
        oepRva = static_cast<uint32_t>(oep);
    }

    Logger::Info("[fix_imports] file={}, module_base=0x{:X}, oep={}",
                 filePath, moduleBase,
                 oepRva.has_value() ? StringUtils::FormatAddress(oepRva.value()) : "none");

    auto& manager = DumpManager::Instance();
    auto fixResult = manager.FixImportsFromFile(filePath, moduleBase, oepRva);

    nlohmann::json result;
    result["success"] = fixResult.success;
    result["file_path"] = fixResult.filePath;

    if (fixResult.success) {
        result["import_count"] = fixResult.importCount;
        result["dll_count"] = fixResult.dllCount;
        Logger::Info("[fix_imports] OK: file={}, imports={}, dlls={}",
                     fixResult.filePath, fixResult.importCount, fixResult.dllCount);
    } else {
        result["error"] = fixResult.error;
        Logger::Error("[fix_imports] FAILED: {}", fixResult.error);
    }

    return result;
}

// ========== Helpers ==========

nlohmann::json DumpHandler::DumpResultToJson(const DumpResult& result) {
    nlohmann::json json;

    json["success"] = result.success;

    if (result.success) {
        json["file_path"] = result.filePath;
        json["dumped_size"] = result.dumpedSize;

        if (result.originalEP != 0) {
            json["original_ep"] = StringUtils::FormatAddress(result.originalEP);
        }

        if (result.newEP != 0) {
            json["new_ep"] = StringUtils::FormatAddress(result.newEP);
        }

        json["stage"] = static_cast<int>(result.finalProgress.stage);
        json["progress"] = result.finalProgress.progress;
        json["message"] = result.finalProgress.message;
    } else {
        json["error"] = result.error;
        json["stage"] = static_cast<int>(result.finalProgress.stage);
        json["message"] = result.finalProgress.message;
    }

    return json;
}

nlohmann::json DumpHandler::MemoryRegionDumpToJson(const MemoryRegionDump& region) {
    nlohmann::json json;

    json["address"] = StringUtils::FormatAddress(region.address);
    json["size"] = region.size;
    json["protection"] = region.protection;
    json["type"] = region.type;
    json["name"] = region.name;

    return json;
}

} // namespace MCP
