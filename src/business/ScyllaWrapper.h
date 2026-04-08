#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace MCP {

struct ScyllaResult {
    bool success = false;
    int errorCode = 0;
    DWORD_PTR iatStart = 0;
    DWORD iatSize = 0;
    std::string errorMessage;
};

/**
 * @brief Singleton wrapper around Scylla.dll for IAT reconstruction.
 *
 * Loads Scylla.dll (or ScyllaDLLx64.dll) from the x64dbg root directory
 * at plugin initialization. Provides IatSearch and IatFixAuto for import
 * table reconstruction, and RebuildFile for PE header fixing.
 *
 * Scylla auto-initializes via DllMain — no manual init needed.
 */
class ScyllaWrapper {
public:
    static ScyllaWrapper& Instance();

    /** True if Scylla DLL loaded and all exports resolved. */
    bool IsAvailable() const { return m_available; }

    /** Human-readable load error if IsAvailable() is false. */
    const std::string& LoadError() const { return m_loadError; }

    /**
     * @brief Search for IAT in target process memory.
     * @param pid Target process ID
     * @param searchStart Address to start searching (typically module base)
     * @param advancedSearch true = thorough disassembly scan, false = quick scan
     * @return ScyllaResult with iatStart/iatSize on success
     */
    ScyllaResult IatSearch(DWORD pid, DWORD_PTR searchStart,
                           bool advancedSearch = true);

    /**
     * @brief Fix imports in a dumped PE file using live process data.
     * @param iatAddr IAT start address from IatSearch
     * @param iatSize IAT size from IatSearch
     * @param pid Target process ID
     * @param dumpFile Path to dumped PE (input)
     * @param fixedFile Path for fixed PE (output, can be same as dumpFile)
     * @return ScyllaResult with success/error
     */
    ScyllaResult IatFixAuto(DWORD_PTR iatAddr, DWORD iatSize, DWORD pid,
                            const std::wstring& dumpFile,
                            const std::wstring& fixedFile);

    /**
     * @brief Rebuild PE file headers.
     * @param file Path to PE file
     * @param removeDosStub Remove DOS stub
     * @param updateChecksum Update PE header checksum
     * @param createBackup Create backup before modifying
     * @return true on success
     */
    bool RebuildFile(const std::wstring& file, bool removeDosStub = false,
                     bool updateChecksum = true, bool createBackup = false);

private:
    ScyllaWrapper();
    ~ScyllaWrapper();
    ScyllaWrapper(const ScyllaWrapper&) = delete;
    ScyllaWrapper& operator=(const ScyllaWrapper&) = delete;

    bool m_available = false;
    HMODULE m_hScylla = nullptr;
    std::string m_loadError;

    // Scylla DLL function pointer types
    using FnIatSearch = int(WINAPI*)(DWORD, DWORD_PTR*, DWORD*, DWORD_PTR, BOOL);
    using FnIatFixAutoW = int(WINAPI*)(DWORD_PTR, DWORD, DWORD, const WCHAR*, const WCHAR*);
    using FnRebuildFileW = BOOL(WINAPI*)(const WCHAR*, BOOL, BOOL, BOOL);
    using FnVersionA = const char*(WINAPI*)();

    FnIatSearch m_iatSearch = nullptr;
    FnIatFixAutoW m_iatFixAutoW = nullptr;
    FnRebuildFileW m_rebuildFileW = nullptr;

    static std::string ErrorToString(int code);
};

} // namespace MCP
