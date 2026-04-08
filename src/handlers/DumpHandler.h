#pragma once
#include <nlohmann/json.hpp>

namespace MCP {

/**
 * @brief Dump operations JSON-RPC handler
 *
 * Tools:
 * - dump.module: Dump module to file with optional Scylla IAT reconstruction
 * - dump.memory_region: Dump memory region to file
 * - dump.get_dumpable_regions: List dumpable memory regions
 * - dump.fix_imports: Standalone Scylla IAT reconstruction on dumped PE
 */
class DumpHandler {
public:
    static void RegisterMethods();

private:
    static nlohmann::json DumpModule(const nlohmann::json& params);
    static nlohmann::json DumpMemoryRegion(const nlohmann::json& params);
    static nlohmann::json GetDumpableRegions(const nlohmann::json& params);
    static nlohmann::json FixImports(const nlohmann::json& params);

    // Helpers
    static nlohmann::json DumpResultToJson(const struct DumpResult& result);
    static nlohmann::json MemoryRegionDumpToJson(const struct MemoryRegionDump& region);
};

} // namespace MCP
