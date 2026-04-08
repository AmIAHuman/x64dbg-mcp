#include "ScyllaWrapper.h"
#include "../core/Logger.h"
#include <filesystem>

namespace MCP {
namespace {

// UTF-8 ↔ wide string helpers (Scylla uses W functions)
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                   static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                   static_cast<int>(wide.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()),
                        utf8.data(), len, nullptr, nullptr);
    return utf8;
}

// Get the x64dbg root directory by finding our own DLL and going up from plugins/
std::wstring GetX64dbgRootDir() {
    HMODULE hSelf = nullptr;
    // Get handle to the DLL containing this function
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetX64dbgRootDir),
        &hSelf);

    if (!hSelf) return {};

    wchar_t dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(hSelf, dllPath, MAX_PATH) == 0) return {};

    // dllPath = ...\x64\plugins\x64dbg-mcp.dp64
    std::filesystem::path p(dllPath);
    // Go up twice: plugins/ → x64/
    return p.parent_path().parent_path().wstring();
}

} // anonymous namespace


ScyllaWrapper& ScyllaWrapper::Instance() {
    static ScyllaWrapper instance;
    return instance;
}

ScyllaWrapper::ScyllaWrapper() {
    std::wstring rootDir = GetX64dbgRootDir();
    if (rootDir.empty()) {
        m_loadError = "Failed to determine x64dbg root directory";
        Logger::Error("[scylla] {}", m_loadError);
        return;
    }

    // Try Scylla.dll first (current x64dbg), then ScyllaDLLx64.dll (older/standalone)
    std::filesystem::path scyllaPath = std::filesystem::path(rootDir) / L"Scylla.dll";
    Logger::Info("[scylla] Loading from: {}", scyllaPath.string());
    m_hScylla = LoadLibraryW(scyllaPath.c_str());

    if (!m_hScylla) {
        scyllaPath = std::filesystem::path(rootDir) / L"ScyllaDLLx64.dll";
        Logger::Info("[scylla] Trying fallback: {}", scyllaPath.string());
        m_hScylla = LoadLibraryW(scyllaPath.c_str());
    }

    if (!m_hScylla) {
        DWORD err = GetLastError();
        m_loadError = "Scylla DLL not found in " + WideToUtf8(rootDir)
                    + " (tried Scylla.dll, ScyllaDLLx64.dll; GetLastError="
                    + std::to_string(err) + ")";
        Logger::Error("[scylla] {}", m_loadError);
        return;
    }

    // Resolve exports
    m_iatSearch = reinterpret_cast<FnIatSearch>(
        GetProcAddress(m_hScylla, "ScyllaIatSearch"));
    m_iatFixAutoW = reinterpret_cast<FnIatFixAutoW>(
        GetProcAddress(m_hScylla, "ScyllaIatFixAutoW"));
    m_rebuildFileW = reinterpret_cast<FnRebuildFileW>(
        GetProcAddress(m_hScylla, "ScyllaRebuildFileW"));

    std::string missing;
    if (!m_iatSearch)    missing += "ScyllaIatSearch ";
    if (!m_iatFixAutoW)  missing += "ScyllaIatFixAutoW ";
    if (!m_rebuildFileW) missing += "ScyllaRebuildFileW ";

    if (!missing.empty()) {
        m_loadError = "Failed to resolve Scylla exports: " + missing;
        Logger::Error("[scylla] {}", m_loadError);
        FreeLibrary(m_hScylla);
        m_hScylla = nullptr;
        return;
    }

    // Log version if available
    auto fnVersionA = reinterpret_cast<FnVersionA>(
        GetProcAddress(m_hScylla, "ScyllaVersionInformationA"));
    if (fnVersionA) {
        const char* ver = fnVersionA();
        Logger::Info("[scylla] Loaded — version: {}", ver ? ver : "(null)");
    } else {
        Logger::Info("[scylla] Loaded — version export not found");
    }

    m_available = true;
}

ScyllaWrapper::~ScyllaWrapper() {
    if (m_hScylla) {
        FreeLibrary(m_hScylla);
        m_hScylla = nullptr;
    }
}

ScyllaResult ScyllaWrapper::IatSearch(DWORD pid, DWORD_PTR searchStart,
                                       bool advancedSearch) {
    ScyllaResult result;

    if (!m_available) {
        result.errorMessage = "Scylla not available: " + m_loadError;
        Logger::Error("[scylla] IatSearch called but not loaded: {}", m_loadError);
        return result;
    }

    Logger::Info("[scylla] IatSearch: pid={}, searchStart=0x{:X}, advanced={}",
                 pid, static_cast<uint64_t>(searchStart), advancedSearch);

    result.errorCode = m_iatSearch(
        pid, &result.iatStart, &result.iatSize,
        searchStart, advancedSearch ? TRUE : FALSE);

    if (result.errorCode == 0) {
        result.success = true;
        Logger::Info("[scylla] IatSearch OK: iatStart=0x{:X}, iatSize={} bytes ({} slots)",
                     static_cast<uint64_t>(result.iatStart),
                     result.iatSize,
                     result.iatSize / sizeof(DWORD_PTR));
    } else {
        result.errorMessage = ErrorToString(result.errorCode);
        Logger::Error("[scylla] IatSearch FAILED: error={} ({})",
                      result.errorCode, result.errorMessage);
    }

    return result;
}

ScyllaResult ScyllaWrapper::IatFixAuto(DWORD_PTR iatAddr, DWORD iatSize,
                                        DWORD pid,
                                        const std::wstring& dumpFile,
                                        const std::wstring& fixedFile) {
    ScyllaResult result;

    if (!m_available) {
        result.errorMessage = "Scylla not available: " + m_loadError;
        Logger::Error("[scylla] IatFixAuto called but not loaded: {}", m_loadError);
        return result;
    }

    std::string dumpUtf8 = WideToUtf8(dumpFile);
    std::string fixedUtf8 = WideToUtf8(fixedFile);

    Logger::Info("[scylla] IatFixAuto: iatAddr=0x{:X}, iatSize={}, pid={}, "
                 "dump=\"{}\", fixed=\"{}\"",
                 static_cast<uint64_t>(iatAddr), iatSize, pid,
                 dumpUtf8, fixedUtf8);

    result.errorCode = m_iatFixAutoW(
        iatAddr, iatSize, pid,
        dumpFile.c_str(), fixedFile.c_str());

    if (result.errorCode == 0) {
        result.success = true;
        Logger::Info("[scylla] IatFixAuto OK: fixed PE written to \"{}\"", fixedUtf8);
    } else {
        result.errorMessage = ErrorToString(result.errorCode);
        Logger::Error("[scylla] IatFixAuto FAILED: error={} ({})",
                      result.errorCode, result.errorMessage);
    }

    return result;
}

bool ScyllaWrapper::RebuildFile(const std::wstring& file,
                                 bool removeDosStub,
                                 bool updateChecksum,
                                 bool createBackup) {
    if (!m_available) {
        Logger::Error("[scylla] RebuildFile called but not loaded: {}", m_loadError);
        return false;
    }

    std::string fileUtf8 = WideToUtf8(file);
    Logger::Info("[scylla] RebuildFile: file=\"{}\", removeDosStub={}, "
                 "updateChecksum={}, createBackup={}",
                 fileUtf8, removeDosStub, updateChecksum, createBackup);

    BOOL ok = m_rebuildFileW(
        file.c_str(),
        removeDosStub ? TRUE : FALSE,
        updateChecksum ? TRUE : FALSE,
        createBackup ? TRUE : FALSE);

    if (ok) {
        Logger::Info("[scylla] RebuildFile OK: \"{}\"", fileUtf8);
    } else {
        Logger::Error("[scylla] RebuildFile FAILED: \"{}\"", fileUtf8);
    }

    return ok != FALSE;
}

std::string ScyllaWrapper::ErrorToString(int code) {
    switch (code) {
        case  0: return "success";
        case -1: return "failed to open process";
        case -2: return "failed to write/fix IAT";
        case -4: return "IAT not found";
        case -5: return "process ID not found";
        default: return "unknown error (code " + std::to_string(code) + ")";
    }
}

} // namespace MCP
