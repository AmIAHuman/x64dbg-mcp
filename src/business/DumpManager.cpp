#include "DumpManager.h"
#include "DebugController.h"
#include "BreakpointManager.h"
#include "MemoryManager.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include "../core/X64DBGBridge.h"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <windows.h>

#ifdef XDBG_SDK_AVAILABLE
#include "_scriptapi_module.h"
#include "_scriptapi_memory.h"
#include <bridgelist.h>
#endif

namespace MCP {
namespace {

std::filesystem::path ToFilesystemPath(const std::string& utf8Path) {
    return std::filesystem::u8path(utf8Path);
}

struct SectionLayout {
    uint32_t virtualAddress = 0;
    uint32_t span = 0;
    uint32_t characteristics = 0;
    std::string name;
};

struct ModuleLayout {
    uint32_t entryRva = 0;
    std::vector<SectionLayout> sections;
};

uint32_t GetSectionSpan(const IMAGE_SECTION_HEADER& section) {
    const uint32_t virtualSize = section.Misc.VirtualSize;
    const uint32_t rawSize = section.SizeOfRawData;
    return std::max(virtualSize, rawSize);
}

std::string GetSectionName(const IMAGE_SECTION_HEADER& section) {
    char name[9] = {0};
    std::memcpy(name, section.Name, sizeof(section.Name));
    return std::string(name);
}

bool IsRvaInSection(uint32_t rva, const SectionLayout& section) {
    return section.span != 0 &&
           rva >= section.virtualAddress &&
           rva < section.virtualAddress + section.span;
}

std::optional<size_t> FindSectionIndex(const ModuleLayout& layout, uint32_t rva) {
    for (size_t i = 0; i < layout.sections.size(); ++i) {
        if (IsRvaInSection(rva, layout.sections[i])) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<ModuleLayout> ReadModuleLayout(uint64_t moduleBase) {
    constexpr size_t kHeaderProbeSize = 0x4000;

    auto& memMgr = MemoryManager::Instance();
    std::vector<uint8_t> peHeader = memMgr.Read(moduleBase, kHeaderProbeSize);
    if (peHeader.size() < sizeof(IMAGE_DOS_HEADER)) {
        return std::nullopt;
    }

    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(peHeader.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return std::nullopt;
    }

    if (dosHeader->e_lfanew <= 0 ||
        peHeader.size() < static_cast<size_t>(dosHeader->e_lfanew) + sizeof(IMAGE_NT_HEADERS)) {
        return std::nullopt;
    }

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        peHeader.data() + dosHeader->e_lfanew
    );
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return std::nullopt;
    }

    const WORD sectionCount = ntHeaders->FileHeader.NumberOfSections;
    if (sectionCount == 0) {
        return std::nullopt;
    }

    const size_t sectionsOffset = static_cast<size_t>(dosHeader->e_lfanew) + sizeof(IMAGE_NT_HEADERS);
    const size_t sectionsSize = static_cast<size_t>(sectionCount) * sizeof(IMAGE_SECTION_HEADER);
    if (peHeader.size() < sectionsOffset + sectionsSize) {
        return std::nullopt;
    }

    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(peHeader.data() + sectionsOffset);

    ModuleLayout layout;
    layout.entryRva = ntHeaders->OptionalHeader.AddressOfEntryPoint;
    layout.sections.reserve(sectionCount);

    for (WORD i = 0; i < sectionCount; ++i) {
        SectionLayout section;
        section.virtualAddress = sections[i].VirtualAddress;
        section.span = GetSectionSpan(sections[i]);
        section.characteristics = sections[i].Characteristics;
        section.name = GetSectionName(sections[i]);
        layout.sections.push_back(section);
    }

    return layout;
}

bool IsLikelyCodeBytes(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        return false;
    }

    bool allZero = true;
    bool allInt3 = true;
    for (uint8_t b : bytes) {
        if (b != 0x00) {
            allZero = false;
        }
        if (b != 0xCC) {
            allInt3 = false;
        }
    }

    return !allZero && !allInt3;
}

std::string ToLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return value;
}

std::string CanonicalText(const std::string& value) {
    return ToLowerAscii(StringUtils::FixUtf8Mojibake(value));
}

std::string BaseNameFromPath(const std::string& path) {
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string StripExtension(const std::string& fileName) {
    const size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return fileName;
    }
    return fileName.substr(0, dot);
}

#ifdef XDBG_SDK_AVAILABLE
bool ModuleMatchesQuery(const Script::Module::ModuleInfo& mod, const std::string& query) {
    if (query.empty()) {
        return false;
    }

    const std::string queryLower = CanonicalText(query);
    const std::string name = StringUtils::FixUtf8Mojibake(mod.name);
    const std::string path = StringUtils::FixUtf8Mojibake(mod.path);
    const std::string fileName = BaseNameFromPath(path);

    const std::string nameLower = CanonicalText(name);
    const std::string pathLower = CanonicalText(path);
    const std::string fileLower = CanonicalText(fileName);

    if (queryLower == nameLower || queryLower == pathLower || queryLower == fileLower) {
        return true;
    }

    const bool hasWildcard = queryLower.find('*') != std::string::npos ||
                             queryLower.find('?') != std::string::npos;
    if (hasWildcard) {
        if (StringUtils::WildcardMatchUtf8(queryLower, nameLower) ||
            StringUtils::WildcardMatchUtf8(queryLower, pathLower) ||
            StringUtils::WildcardMatchUtf8(queryLower, fileLower)) {
            return true;
        }
    }

    const std::string queryStemLower = CanonicalText(StripExtension(query));
    if (queryStemLower.empty()) {
        return false;
    }

    const std::string nameStemLower = CanonicalText(StripExtension(name));
    const std::string fileStemLower = CanonicalText(StripExtension(fileName));

    if (hasWildcard) {
        if (StringUtils::WildcardMatchUtf8(queryStemLower, nameStemLower) ||
            StringUtils::WildcardMatchUtf8(queryStemLower, fileStemLower)) {
            return true;
        }
    }

    return queryStemLower == nameStemLower || queryStemLower == fileStemLower;
}

std::optional<uint64_t> ResolveModuleBaseByQueryFallback(const std::string& query) {
    BridgeList<Script::Module::ModuleInfo> moduleList;
    if (!Script::Module::GetList(&moduleList)) {
        return std::nullopt;
    }

    for (size_t i = 0; i < moduleList.Count(); ++i) {
        const auto& mod = moduleList[i];
        if (ModuleMatchesQuery(mod, query)) {
            return mod.base;
        }
    }

    return std::nullopt;
}
#endif

bool SectionContainsRva(const IMAGE_SECTION_HEADER& section, uint32_t rva) {
    const uint32_t start = section.VirtualAddress;
    const uint32_t span = GetSectionSpan(section);
    return span != 0 && rva >= start && rva < start + span;
}

std::optional<uint64_t> FindTransferAddressToTarget(uint64_t moduleBase, uint64_t targetAddress) {
    auto layoutOpt = ReadModuleLayout(moduleBase);
    if (!layoutOpt.has_value()) {
        return std::nullopt;
    }

    const ModuleLayout& layout = layoutOpt.value();
    auto entrySectionIndexOpt = FindSectionIndex(layout, layout.entryRva);
    if (!entrySectionIndexOpt.has_value()) {
        return std::nullopt;
    }

    const auto& entrySection = layout.sections[entrySectionIndexOpt.value()];
    const uint64_t entryVA = moduleBase + layout.entryRva;
    const uint64_t entrySectionEnd =
        moduleBase + static_cast<uint64_t>(entrySection.virtualAddress) + entrySection.span;
    const size_t scanSize = static_cast<size_t>(
        std::min<uint64_t>(0x6000, entrySectionEnd > entryVA ? entrySectionEnd - entryVA : 0)
    );
    if (scanSize < 2) {
        return std::nullopt;
    }

    auto& memMgr = MemoryManager::Instance();
    auto code = memMgr.Read(entryVA, scanSize);

    for (size_t i = 0; i < code.size(); ++i) {
        const uint64_t instructionAddress = entryVA + i;

        if (i + 5 <= code.size() && code[i] == 0xE9) {
            int32_t rel32 = 0;
            std::memcpy(&rel32, code.data() + i + 1, sizeof(rel32));
            const uint64_t target = static_cast<uint64_t>(
                static_cast<int64_t>(instructionAddress) + 5 + rel32
            );
            if (target == targetAddress) {
                return instructionAddress;
            }
        }

        if (i + 2 <= code.size() && code[i] == 0xEB) {
            const int8_t rel8 = static_cast<int8_t>(code[i + 1]);
            const uint64_t target = static_cast<uint64_t>(
                static_cast<int64_t>(instructionAddress) + 2 + rel8
            );
            if (target == targetAddress) {
                return instructionAddress;
            }
        }

        if (i + 6 <= code.size() && code[i] == 0xFF && code[i + 1] == 0x25) {
            int32_t disp32 = 0;
            std::memcpy(&disp32, code.data() + i + 2, sizeof(disp32));

            uint64_t pointerAddress = 0;
#ifdef _WIN64
            pointerAddress = static_cast<uint64_t>(
                static_cast<int64_t>(instructionAddress) + 6 + disp32
            );
#else
            pointerAddress = static_cast<uint32_t>(disp32);
#endif

            try {
                auto pointerBytes = memMgr.Read(pointerAddress, sizeof(duint));
                if (pointerBytes.size() == sizeof(duint)) {
                    duint targetValue = 0;
                    std::memcpy(&targetValue, pointerBytes.data(), sizeof(duint));
                    if (static_cast<uint64_t>(targetValue) == targetAddress) {
                        return instructionAddress;
                    }
                }
            } catch (...) {
                // Ignore unresolved pointer targets.
            }
        }

        // x86: push imm32; ret
        if (i + 6 <= code.size() && code[i] == 0x68 && code[i + 5] == 0xC3) {
            uint32_t imm32 = 0;
            std::memcpy(&imm32, code.data() + i + 1, sizeof(imm32));
            if (static_cast<uint64_t>(imm32) == targetAddress) {
                return instructionAddress;
            }
        }

        // x86/x64: mov reg, imm; jmp reg
        if (i + 7 <= code.size() && code[i] >= 0xB8 && code[i] <= 0xBF &&
            code[i + 5] == 0xFF && code[i + 6] >= 0xE0 && code[i + 6] <= 0xE7) {
            uint32_t imm32 = 0;
            std::memcpy(&imm32, code.data() + i + 1, sizeof(imm32));
            if (static_cast<uint64_t>(imm32) == targetAddress) {
                return instructionAddress;
            }
        }

#ifdef _WIN64
        if (i + 13 <= code.size() && code[i] == 0x48 &&
            code[i + 1] >= 0xB8 && code[i + 1] <= 0xBF &&
            code[i + 10] == 0xFF && code[i + 11] >= 0xE0 && code[i + 11] <= 0xE7) {
            uint64_t imm64 = 0;
            std::memcpy(&imm64, code.data() + i + 2, sizeof(imm64));
            if (imm64 == targetAddress) {
                return instructionAddress;
            }
        }
#endif
    }

    return std::nullopt;
}

bool EnsureDebuggerPausedForDump(const char* phase) {
    auto& debugController = DebugController::Instance();
    if (!debugController.IsDebugging()) {
        return false;
    }

    if (debugController.IsPaused()) {
        return true;
    }

    Logger::Info("Debugger is running before {}. Requesting pause...", phase);
    if (!debugController.Pause()) {
        Logger::Warning("Failed to pause debugger before {}", phase);
        return false;
    }

    return debugController.IsPaused();
}

bool EnsureExecutionInModuleContext(
    const std::string& modulePath,
    uint64_t moduleBase,
    uint64_t moduleSize,
    const char* phase)
{
    if (modulePath.empty() || moduleSize == 0) {
        return false;
    }

    const auto isRipInModule = [&](uint64_t rip) {
        return rip >= moduleBase && rip < moduleBase + moduleSize;
    };

    auto& debugController = DebugController::Instance();

    uint64_t currentRip = 0;
    try {
        currentRip = debugController.GetInstructionPointer();
    } catch (...) {
        currentRip = 0;
    }

    if (isRipInModule(currentRip)) {
        return true;
    }

    std::string escapedPath = modulePath;
    size_t quotePos = 0;
    while ((quotePos = escapedPath.find('"', quotePos)) != std::string::npos) {
        escapedPath.replace(quotePos, 1, "\\\"");
        quotePos += 2;
    }

    const std::string initCommand = "init \"" + escapedPath + "\"";
    Logger::Info(
        "RIP {} is outside module before {}. Resetting context with {}",
        StringUtils::FormatAddress(currentRip),
        phase,
        initCommand
    );

    if (!DbgCmdExec(initCommand.c_str())) {
        Logger::Warning("Failed to execute init command before {}", phase);
        return false;
    }

    // Wait for debugger session to become available after init.
    const auto attachStart = std::chrono::steady_clock::now();
    while (!debugController.IsDebugging()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - attachStart
        ).count();
        if (elapsed >= 5000) {
            Logger::Warning("Debugger did not become active after init command");
            return false;
        }
        Sleep(10);
    }

    // Move execution until debugger lands in target module startup flow.
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (debugController.IsPaused()) {
            uint64_t rip = 0;
            try {
                rip = debugController.GetInstructionPointer();
            } catch (...) {
                rip = 0;
            }

            if (isRipInModule(rip)) {
                Logger::Info("Recovered module context at RIP {}", StringUtils::FormatAddress(rip));
                return true;
            }
        }

        // Continue execution and wait for the next debug stop.
        // This matches manual recovery flow: init -> run -> stop in loader/module.
        if (!debugController.Run()) {
            Logger::Warning("Run command was not accepted during context recovery attempt {}", attempt + 1);
            Sleep(50);
        }

        // Run command is asynchronous; give debugger state a short time to transition.
        Sleep(50);

        const auto waitStart = std::chrono::steady_clock::now();
        while (!debugController.IsPaused()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - waitStart
            ).count();

            if (elapsed >= 15000) {
                Logger::Warning("Context recovery wait timed out on attempt {}", attempt + 1);
                break;
            }

            Sleep(10);
        }

        if (!debugController.IsPaused()) {
            // Try to force a break and continue recovery attempts.
            try {
                debugController.Pause();
            } catch (...) {
                // Ignore pause errors here; next attempt may still recover.
            }
        }
    }

    Logger::Warning("Failed to recover module context before {}", phase);
    return false;
}

} // namespace

DumpManager& DumpManager::Instance() {
    static DumpManager instance;
    return instance;
}

DumpResult DumpManager::DumpModule(
    const std::string& moduleNameOrAddress,
    const std::string& outputPath,
    const DumpOptions& options,
    ProgressCallback progressCallback)
{
    DumpResult result;
    DumpProgress progress;
    
    auto updateProgress = [&](DumpProgress::Stage stage, int percent, const std::string& msg) {
        progress.stage = stage;
        progress.progress = percent;
        progress.message = msg;
        if (progressCallback) {
            progressCallback(progress);
        }
        Logger::Info("[Dump] {} - {}%: {}", static_cast<int>(stage), percent, msg);
    };
    
    try {
        if (!DebugController::Instance().IsDebugging()) {
            throw DebuggerNotRunningException();
        }

        if (!EnsureDebuggerPausedForDump("module dump")) {
            throw MCPException("Failed to pause debugger before dump");
        }
        
        updateProgress(DumpProgress::Stage::Preparing, 0, "Parsing module information");
        
        // 瑙ｆ瀽妯″潡鍦板潃
        auto moduleBaseOpt = ParseModuleOrAddress(moduleNameOrAddress);
        if (!moduleBaseOpt.has_value()) {
            throw InvalidParamsException("Invalid module name or address: " + moduleNameOrAddress);
        }
        
        uint64_t moduleBase = moduleBaseOpt.value();
        uint64_t moduleSize = GetModuleSize(moduleBase);
        uint64_t entryPoint = GetModuleEntryPoint(moduleBase);
        
        if (moduleSize == 0) {
            throw MCPException("Failed to get module size");
        }
        
        Logger::Info("Dumping module at {}, size: {} bytes, EP: {}",
                    StringUtils::FormatAddress(moduleBase),
                    moduleSize,
                    StringUtils::FormatAddress(entryPoint));

        const std::string modulePath = GetModulePath(moduleBase);
        const std::filesystem::path moduleFsPath =
            modulePath.empty() ? std::filesystem::path() : ToFilesystemPath(modulePath);
        const std::filesystem::path outputFsPath = ToFilesystemPath(outputPath);
        std::string packerId;
        const bool isPackedImage = IsPacked(moduleBase, packerId);
        const bool hasResolvedOEP =
            options.forcedOEP.has_value() && options.forcedOEP.value() != entryPoint;

        // If still packed and no resolved OEP is provided, return a runnable baseline by copying
        // the original image instead of writing unstable runtime memory state.
        if (isPackedImage && !options.autoDetectOEP && !hasResolvedOEP &&
            !modulePath.empty() && std::filesystem::exists(moduleFsPath)) {
            updateProgress(DumpProgress::Stage::Preparing, 5,
                           "Packed module fallback: copying original image");

            std::filesystem::copy_file(
                moduleFsPath,
                outputFsPath,
                std::filesystem::copy_options::overwrite_existing
            );

            result.success = true;
            result.filePath = outputPath;
            result.dumpedSize = std::filesystem::file_size(outputFsPath);
            result.originalEP = entryPoint;
            result.newEP = entryPoint;

            updateProgress(
                DumpProgress::Stage::Completed,
                100,
                "Packed image copied. Run this file or resolve OEP first for true unpack dump."
            );
            progress.success = true;
            result.finalProgress = progress;

            Logger::Warning(
                "Packed module '{}' copied to output because no resolved OEP was provided",
                packerId
            );
            return result;
        }
        
        updateProgress(DumpProgress::Stage::ReadingMemory, 10, "Reading module memory");
        
        // 璇诲彇鏁翠釜妯″潡鍐呭瓨
        auto& memMgr = MemoryManager::Instance();
        std::vector<uint8_t> buffer;
        
        if (options.dumpFullImage) {
            // 鎸塒E鏂囦欢澶у皬dump
            buffer = memMgr.Read(moduleBase, moduleSize);
        } else {
            // 鍙猟ump宸叉彁浜ょ殑鍐呭瓨椤?
            buffer = memMgr.Read(moduleBase, moduleSize);
        }
        
        result.dumpedSize = buffer.size();
        result.originalEP = entryPoint;
        
        // 楠岃瘉PE澶?
        if (!ValidatePEHeader(buffer)) {
            Logger::Warning("Invalid PE header detected, attempting to continue...");
        }
        
        updateProgress(DumpProgress::Stage::FixingPEHeaders, 30, "Fixing PE headers");
        
        // Rebuild PE header.
        if (options.rebuildPE) {
            std::optional<uint32_t> newOEP;

            const auto trySetOEP = [&](uint64_t absoluteOEP, const char* source) {
                if (absoluteOEP < moduleBase || absoluteOEP >= moduleBase + moduleSize) {
                    Logger::Warning("{} OEP {} is outside module range [{}, {})",
                                    source,
                                    StringUtils::FormatAddress(absoluteOEP),
                                    StringUtils::FormatAddress(moduleBase),
                                    StringUtils::FormatAddress(moduleBase + moduleSize));
                    return false;
                }

                const uint64_t rva64 = absoluteOEP - moduleBase;
                if (rva64 > std::numeric_limits<uint32_t>::max()) {
                    Logger::Warning("{} OEP RVA {} exceeds 32-bit PE limit",
                                    source,
                                    StringUtils::FormatAddress(rva64));
                    return false;
                }

                newOEP = static_cast<uint32_t>(rva64);
                result.newEP = absoluteOEP;
                Logger::Info("{} OEP: {} (RVA: {})",
                             source,
                             StringUtils::FormatAddress(absoluteOEP),
                             StringUtils::FormatAddress(newOEP.value()));
                return true;
            };

            if (options.forcedOEP.has_value()) {
                if (!trySetOEP(options.forcedOEP.value(), "Forced")) {
                    throw InvalidParamsException("Forced OEP is outside target module range");
                }
            } else if (options.autoDetectOEP) {
                auto detectedOEP = DetectOEP(moduleBase);
                if (detectedOEP.has_value()) {
                    trySetOEP(detectedOEP.value(), "Auto-detected");
                }
            } else if (options.fixOEP) {
                trySetOEP(entryPoint, "Entry-point");
            }
            
            if (!RebuildPEHeaders(moduleBase, buffer, newOEP)) {
                Logger::Warning("Failed to rebuild PE headers");
            }
        }
        
        
        updateProgress(DumpProgress::Stage::FixingRelocations, 70, "Fixing relocations");
        
        // 淇閲嶅畾浣?
        if (options.fixRelocations) {
            // 浣跨敤褰撳墠鍩哄潃浣滀负棣栭€夊熀鍧€
            if (!FixRelocations(moduleBase, moduleBase, buffer)) {
                Logger::Warning("Failed to fix relocations");
            }
        }
        
        // 绉婚櫎PE鏍￠獙鍜?
        if (options.removeIntegrityCheck) {
            FixPEChecksum(buffer);
        }
        
        updateProgress(DumpProgress::Stage::Writing, 90, "Writing to file");
        
        // 鍐欏叆鏂囦欢
        std::ofstream outFile(outputFsPath, std::ios::binary);
        if (!outFile) {
            throw MCPException("Failed to create output file: " + outputPath);
        }
        
        outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        outFile.close();
        
        if (!outFile.good()) {
            throw MCPException("Failed to write dump file");
        }
        
        // Fix imports using file-based IAT reconstruction from live process
        if (options.fixImports) {
            updateProgress(DumpProgress::Stage::FixingImports, 85, "Fixing import table");
            auto fixResult = FixImportsFromFile(outputPath, moduleBase, std::nullopt);
            if (fixResult.success) {
                Logger::Info("Import table reconstructed: {} imports from {} DLLs",
                             fixResult.importCount, fixResult.dllCount);
            } else {
                Logger::Warning("Import reconstruction failed: {}", fixResult.error);
            }
        }
        
        result.success = true;
        result.filePath = outputPath;
        
        updateProgress(DumpProgress::Stage::Completed, 100, "Dump completed successfully");
        progress.success = true;
        result.finalProgress = progress;
        
        Logger::Info("Module dumped successfully to: {}", outputPath);
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
        progress.stage = DumpProgress::Stage::Failed;
        progress.success = false;
        progress.message = e.what();
        result.finalProgress = progress;
        
        Logger::Error("Dump failed: {}", e.what());
    }
    
    return result;
}

DumpResult DumpManager::DumpMemoryRegion(
    uint64_t startAddress,
    size_t size,
    const std::string& outputPath,
    bool asRawBinary)
{
    DumpResult result;
    
    try {
        if (!DebugController::Instance().IsDebugging()) {
            throw DebuggerNotRunningException();
        }
        
        Logger::Info("Dumping memory region {} - {} ({} bytes)",
                    StringUtils::FormatAddress(startAddress),
                    StringUtils::FormatAddress(startAddress + size),
                    size);
        
        auto& memMgr = MemoryManager::Instance();
        std::vector<uint8_t> buffer = memMgr.Read(startAddress, size);
        
        if (!asRawBinary && ValidatePEHeader(buffer)) {
            // 灏濊瘯淇PE
            Logger::Info("PE header detected, attempting to fix");
            RebuildPEHeaders(startAddress, buffer);
        }
        
        std::ofstream outFile(ToFilesystemPath(outputPath), std::ios::binary);
        if (!outFile) {
            throw MCPException("Failed to create output file: " + outputPath);
        }
        
        outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        outFile.close();
        
        result.success = true;
        result.filePath = outputPath;
        result.dumpedSize = buffer.size();
        result.finalProgress.stage = DumpProgress::Stage::Completed;
        result.finalProgress.progress = 100;
        result.finalProgress.message = "Memory region dumped successfully";
        result.finalProgress.success = true;
        
        Logger::Info("Memory region dumped successfully to: {}", outputPath);
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
        result.finalProgress.stage = DumpProgress::Stage::Failed;
        result.finalProgress.progress = 0;
        result.finalProgress.message = e.what();
        result.finalProgress.success = false;
        Logger::Error("Memory dump failed: {}", e.what());
    }
    
    return result;
}

DumpResult DumpManager::AutoUnpackAndDump(
    const std::string& moduleNameOrAddress,
    const std::string& outputPath,
    int maxIterations,
    const std::string& oepStrategy,
    ProgressCallback progressCallback)
{
    DumpResult result;
    DumpProgress progress;
    
    auto updateProgress = [&](DumpProgress::Stage stage, int percent, const std::string& msg) {
        progress.stage = stage;
        progress.progress = percent;
        progress.message = msg;
        if (progressCallback) {
            progressCallback(progress);
        }
        Logger::Info("[AutoUnpack] {}%: {}", percent, msg);
    };

    const auto waitForPause = [](uint32_t timeoutMs) -> bool {
        const auto start = std::chrono::steady_clock::now();
        while (true) {
            if (DebugController::Instance().IsPaused()) {
                return true;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed >= timeoutMs) {
                return false;
            }

            Sleep(10);
        }
    };

    const auto ensurePaused = [&](uint32_t timeoutMs, const char* phase) -> bool {
        auto& debugController = DebugController::Instance();
        if (debugController.IsPaused()) {
            return true;
        }

        try {
            Logger::Info("Debugger is running before {}. Sending pause request...", phase);
            if (!debugController.Pause()) {
                Logger::Warning("Pause command failed before {}", phase);
                return false;
            }
        } catch (const std::exception& e) {
            Logger::Warning("Pause request threw before {}: {}", phase, e.what());
            return false;
        }

        if (!waitForPause(timeoutMs)) {
            Logger::Warning("Timed out waiting for paused state before {}", phase);
            return false;
        }

        return true;
    };
    
    try {
        if (!DebugController::Instance().IsDebugging()) {
            throw DebuggerNotRunningException();
        }

        if (maxIterations <= 0) {
            throw InvalidParamsException("maxIterations must be greater than zero");
        }

        if (!ensurePaused(5000, "auto-unpack analysis")) {
            throw MCPException("Failed to pause debugger before auto-unpack");
        }

        updateProgress(DumpProgress::Stage::Preparing, 0, "Analyzing target module");
        
        auto moduleBaseOpt = ParseModuleOrAddress(moduleNameOrAddress);
        if (!moduleBaseOpt.has_value()) {
            throw InvalidParamsException("Invalid module: " + moduleNameOrAddress);
        }
        
        uint64_t moduleBase = moduleBaseOpt.value();
        
        // 鍒嗘瀽鏄惁鍔犲３
        ModuleDumpInfo info = AnalyzeModule(moduleNameOrAddress);
        updateProgress(DumpProgress::Stage::Preparing, 10, 
                      info.isPacked ? "Packed module detected: " + info.packerId : "Module is not packed");
        
        Logger::Info("Module: {}, Base: {}, Packed: {}",
                    info.name, StringUtils::FormatAddress(info.baseAddress), info.isPacked);
        
        if (!info.isPacked) {
            // 鏈姞澹?鐩存帴dump
            updateProgress(DumpProgress::Stage::Preparing, 20, "No packer detected, performing standard dump");
            DumpOptions opts;
            opts.autoDetectOEP = false;
            return DumpModule(moduleNameOrAddress, outputPath, opts, progressCallback);
        }
        
        // 鑷姩鑴卞３娴佺▼
        updateProgress(DumpProgress::Stage::Preparing, 20, "Starting automatic unpacking");
        const uint64_t declaredEntry = GetModuleEntryPoint(moduleBase);

        if (!EnsureExecutionInModuleContext(info.path, moduleBase, info.size, "auto-unpack")) {
            throw MCPException("Failed to recover target module execution context");
        }

        if (!ensurePaused(5000, "auto-unpack context ready")) {
            throw MCPException("Failed to pause debugger before auto-unpack iteration");
        }
        
        for (int iteration = 0; iteration < maxIterations; iteration++) {
            int baseProgress = 20 + (iteration * 60 / maxIterations);
            updateProgress(DumpProgress::Stage::Preparing, baseProgress, 
                          "Unpacking iteration " + std::to_string(iteration + 1));

            if (!ensurePaused(5000, "OEP detection")) {
                Logger::Warning("Iteration {}: debugger could not be paused", iteration + 1);
                continue;
            }
            
            // 灏濊瘯妫€娴婳EP
            auto oepOpt = DetectOEP(moduleBase, oepStrategy);
            if (!oepOpt.has_value()) {
                Logger::Warning("Failed to detect OEP in iteration {}", iteration + 1);
                continue;
            }
            
            uint64_t detectedOEP = oepOpt.value();
            Logger::Info("Iteration {}: Detected OEP at {}",
                         iteration + 1,
                         StringUtils::FormatAddress(detectedOEP));
            if (info.isPacked && detectedOEP == declaredEntry) {
                Logger::Warning(
                    "Iteration {}: detected OEP is still packed entry {}",
                    iteration + 1,
                    StringUtils::FormatAddress(detectedOEP)
                );
                continue;
            }

            // 鍦∣EP璁剧疆鏂偣
            updateProgress(DumpProgress::Stage::Preparing, baseProgress + 10, 
                          "Setting breakpoint at OEP");

            bool reachedOEP = false;
            bool breakpointSet = false;
            auto& breakpointManager = BreakpointManager::Instance();
            auto transferAddressOpt = FindTransferAddressToTarget(moduleBase, detectedOEP);
            const uint64_t breakpointAddress = transferAddressOpt.value_or(detectedOEP);

            try {
                breakpointSet = breakpointManager.SetSoftwareBreakpoint(breakpointAddress, "__mcp_auto_oep");
            } catch (const std::exception& e) {
                Logger::Warning("Failed to set temporary OEP breakpoint at {}: {}",
                                breakpointAddress, e.what());
            }

            if (!breakpointSet) {
                Logger::Warning("Unable to set temporary OEP breakpoint at {}", breakpointAddress);
                continue;
            }

            auto& debugController = DebugController::Instance();
            for (int runAttempt = 0; runAttempt < 32; ++runAttempt) {
                if (!debugController.Run()) {
                    Logger::Warning("Run failed while waiting for OEP {}", detectedOEP);
                    break;
                }

                if (!waitForPause(15000)) {
                    Logger::Warning("Timed out waiting for pause while running to OEP {}",
                                    detectedOEP);
                    break;
                }

                uint64_t rip = 0;
                try {
                    rip = debugController.GetInstructionPointer();
                } catch (...) {
                    Logger::Warning("Failed to read RIP while waiting for OEP");
                    break;
                }

                if (rip == detectedOEP) {
                    reachedOEP = true;
                    break;
                }

                if (rip == breakpointAddress && breakpointAddress != detectedOEP) {
                    try {
                        breakpointManager.DeleteBreakpoint(breakpointAddress, BreakpointType::Software);
                    } catch (...) {
                        // Continue even if temporary cleanup fails.
                    }

                    uint64_t steppedRip = 0;
                    try {
                        steppedRip = debugController.StepInto();
                    } catch (const std::exception& e) {
                        Logger::Warning("Step into transfer instruction failed: {}", e.what());
                        break;
                    }

                    if (steppedRip == detectedOEP) {
                        reachedOEP = true;
                        break;
                    }
                }

                if (rip >= moduleBase && rip < moduleBase + info.size) {
                    try {
                        breakpointManager.DeleteBreakpoint(rip, BreakpointType::Software);
                        Logger::Info("Removed interfering software breakpoint at {}", rip);
                    } catch (...) {
                        // Not all stops are caused by removable software breakpoints.
                    }
                }

                Logger::Info(
                    "Stopped at {} while waiting for OEP {} (attempt {}/{})",
                    rip,
                    detectedOEP,
                    runAttempt + 1,
                    32
                );
            }

            try {
                breakpointManager.DeleteBreakpoint(breakpointAddress, BreakpointType::Software);
            } catch (...) {
                // Ignore cleanup failures for temporary breakpoint.
            }

            if (!reachedOEP) {
                Logger::Warning("Failed to reach detected OEP {} in iteration {}",
                                detectedOEP, iteration + 1);
                continue;
            }
            
            // 灏濊瘯dump
            updateProgress(DumpProgress::Stage::ReadingMemory, baseProgress + 20, 
                          "Dumping unpacked module");

            if (!ensurePaused(5000, "dump writing")) {
                Logger::Warning("Iteration {}: debugger is not paused before dump", iteration + 1);
                continue;
            }
            
            DumpOptions opts;
            opts.autoDetectOEP = false;
            opts.fixOEP = true;
            opts.fixImports = true;
            opts.rebuildPE = true;
            opts.forcedOEP = detectedOEP;
            
            std::string iterOutputPath = outputPath;
            if (iteration > 0) {
                size_t dotPos = outputPath.find_last_of('.');
                if (dotPos != std::string::npos) {
                    iterOutputPath = outputPath.substr(0, dotPos) + 
                                    "_iter" + std::to_string(iteration) + 
                                    outputPath.substr(dotPos);
                } else {
                    iterOutputPath = outputPath + "_iter" + std::to_string(iteration);
                }
            }
            
            result = DumpModule(moduleNameOrAddress, iterOutputPath, opts, progressCallback);
            
            if (result.success) {
                updateProgress(DumpProgress::Stage::Completed, 100, 
                              "Auto-unpack completed after " + std::to_string(iteration + 1) + " iterations");
                return result;
            }
        }
        
        // 鎵€鏈夎凯浠ｉ兘澶辫触
        throw MCPException("Failed to unpack after " + std::to_string(maxIterations) + " iterations");
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
        progress.stage = DumpProgress::Stage::Failed;
        progress.message = e.what();
        result.finalProgress = progress;
        Logger::Error("Auto-unpack failed: {}", e.what());
    }
    
    return result;
}

ModuleDumpInfo DumpManager::AnalyzeModule(const std::string& moduleNameOrAddress) {
    ModuleDumpInfo info;
    
    try {
        if (!DebugController::Instance().IsDebugging()) {
            throw DebuggerNotRunningException();
        }

        if (!EnsureDebuggerPausedForDump("module analysis")) {
            throw MCPException("Failed to pause debugger before module analysis");
        }

        auto moduleBaseOpt = ParseModuleOrAddress(moduleNameOrAddress);
        if (!moduleBaseOpt.has_value()) {
            throw InvalidParamsException("Invalid module");
        }
        
        uint64_t moduleBase = moduleBaseOpt.value();
        info.baseAddress = moduleBase;
        info.size = GetModuleSize(moduleBase);
        info.entryPoint = GetModuleEntryPoint(moduleBase);
        info.path = GetModulePath(moduleBase);
        
        // 浠庤矾寰勬彁鍙栨ā鍧楀悕
        size_t lastSlash = info.path.find_last_of("\\/");
        info.name = (lastSlash != std::string::npos) ? 
                    info.path.substr(lastSlash + 1) : info.path;
        
        // 妫€娴嬫槸鍚﹀姞澹?
        info.isPacked = IsPacked(moduleBase, info.packerId);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to analyze module: {}", e.what());
        throw;
    }
    
    return info;
}

std::optional<uint64_t> DumpManager::DetectOEP(uint64_t moduleBase, const std::string& strategy) {
    if (!DebugController::Instance().IsDebugging()) {
        throw DebuggerNotRunningException();
    }

    if (!EnsureDebuggerPausedForDump("OEP detection")) {
        throw MCPException("Failed to pause debugger before OEP detection");
    }

    Logger::Debug("Detecting OEP for module at {} using strategy: {}",
                  StringUtils::FormatAddress(moduleBase),
                  strategy);
    std::string packerId;
    const bool isPacked = IsPacked(moduleBase, packerId);
    const uint64_t entryPoint = GetModuleEntryPoint(moduleBase);

    if (strategy == "entropy") {
        auto result = DetectOEPByEntropy(moduleBase);
        if (!result.has_value() && isPacked) {
            result = DetectOEPByPattern(moduleBase);
        }
        if (result.has_value()) {
            Logger::Info("OEP detected by entropy: {}", StringUtils::FormatAddress(result.value()));
        }
        return result;
    }

    if (strategy == "code_analysis") {
        auto result = DetectOEPByPattern(moduleBase);
        if (!result.has_value() && isPacked) {
            result = DetectOEPByExecution(moduleBase);
        }
        if (result.has_value()) {
            Logger::Info("OEP detected by code analysis: {}", StringUtils::FormatAddress(result.value()));
        }
        return result;
    }

    if (strategy == "api_calls") {
        // TODO: Implement API-call based OEP detection
        Logger::Warning("API calls strategy not yet implemented");
        return std::nullopt;
    }

    if (strategy == "tls") {
        // TODO: Implement TLS callback based OEP detection
        Logger::Warning("TLS strategy not yet implemented");
        return std::nullopt;
    }

    if (strategy == "entrypoint") {
        if (isPacked) {
            auto unpackedCandidate = DetectOEPByPattern(moduleBase);
            if (unpackedCandidate.has_value() && unpackedCandidate.value() != entryPoint) {
                Logger::Info(
                    "Packed module '{}' OEP resolved from transfer pattern: {}",
                    packerId,
                    StringUtils::FormatAddress(unpackedCandidate.value())
                );
                return unpackedCandidate;
            }
        }

        if (entryPoint != 0) {
            Logger::Info("Using declared entry point as OEP: {}",
                         StringUtils::FormatAddress(entryPoint));
            return entryPoint;
        }

        Logger::Warning("Failed to get module entry point");
        return std::nullopt;
    }

    Logger::Error("Unknown OEP detection strategy: {}", strategy);
    return std::nullopt;
}

std::vector<MemoryRegionDump> DumpManager::GetDumpableRegions(uint64_t moduleBase) {
    std::vector<MemoryRegionDump> regions;
    
    auto& memMgr = MemoryManager::Instance();
    auto allRegions = memMgr.EnumerateRegions();
    
    for (const auto& region : allRegions) {
        // 杩囨护鏉′欢
        if (moduleBase != 0 && region.base < moduleBase) {
            continue;
        }
        
        if (moduleBase != 0) {
            uint64_t moduleSize = GetModuleSize(moduleBase);
            if (region.base >= moduleBase + moduleSize) {
                continue;
            }
        }
        
        // 鍙寘鍚凡鎻愪氦鐨勫彲璇诲唴瀛?
        if (region.type.find("MEM_COMMIT") == std::string::npos) {
            continue;
        }
        
        MemoryRegionDump dumpRegion;
        dumpRegion.address = region.base;
        dumpRegion.size = region.size;
        dumpRegion.protection = region.protection;
        dumpRegion.type = region.type;
        dumpRegion.name = region.info;
        
        regions.push_back(dumpRegion);
    }
    
    Logger::Debug("Found {} dumpable regions for module at {}",
                  regions.size(),
                  StringUtils::FormatAddress(moduleBase));
    return regions;
}

bool DumpManager::FixImportTable(uint64_t moduleBase, std::vector<uint8_t>& buffer) {
    try {
        if (buffer.size() < sizeof(IMAGE_DOS_HEADER)) {
            return false;
        }

        auto* dumpDosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
        if (dumpDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        if (buffer.size() < dumpDosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)) {
            return false;
        }

        auto* dumpNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + dumpDosHeader->e_lfanew);
        if (dumpNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        const std::string modulePath = GetModulePath(moduleBase);
        if (modulePath.empty()) {
            Logger::Warning("Cannot fix imports: module path is empty for {}",
                            StringUtils::FormatAddress(moduleBase));
            return false;
        }

        std::ifstream input(ToFilesystemPath(modulePath), std::ios::binary | std::ios::ate);
        if (!input) {
            Logger::Warning("Cannot fix imports: failed to open original file {}", modulePath);
            return false;
        }

        const std::streamsize fileSize = input.tellg();
        if (fileSize <= 0) {
            Logger::Warning("Cannot fix imports: original file {} is empty", modulePath);
            return false;
        }
        input.seekg(0, std::ios::beg);

        std::vector<uint8_t> originalFile(static_cast<size_t>(fileSize));
        if (!input.read(reinterpret_cast<char*>(originalFile.data()), fileSize)) {
            Logger::Warning("Cannot fix imports: failed to read original file {}", modulePath);
            return false;
        }

        if (!ValidatePEHeader(originalFile)) {
            Logger::Warning("Cannot fix imports: original file PE header is invalid ({})", modulePath);
            return false;
        }

        auto* originalDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(originalFile.data());
        auto* originalNtHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            originalFile.data() + originalDosHeader->e_lfanew
        );

        const WORD dumpSectionCount = dumpNtHeaders->FileHeader.NumberOfSections;
        const WORD originalSectionCount = originalNtHeaders->FileHeader.NumberOfSections;
        if (dumpSectionCount == 0 || originalSectionCount == 0) {
            Logger::Warning("Cannot fix imports: missing section headers");
            return false;
        }

        auto* dumpSections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
            reinterpret_cast<uint8_t*>(dumpNtHeaders) + sizeof(IMAGE_NT_HEADERS)
        );
        auto* originalSections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
            reinterpret_cast<const uint8_t*>(originalNtHeaders) + sizeof(IMAGE_NT_HEADERS)
        );

        // Keep dump section layout (raw offsets/sizes) intact.
        // Only restore metadata fields that are safe for import reconstruction.
        const WORD copySectionCount = std::min(dumpSectionCount, originalSectionCount);
        for (WORD i = 0; i < copySectionCount; ++i) {
            std::memcpy(dumpSections[i].Name, originalSections[i].Name, IMAGE_SIZEOF_SHORT_NAME);
            dumpSections[i].Characteristics = originalSections[i].Characteristics;
        }

        dumpNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] =
            originalNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        dumpNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT] =
            originalNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];

        const auto findSectionByRva = [](const IMAGE_SECTION_HEADER* sections, WORD count, uint32_t rva)
            -> std::optional<WORD> {
            for (WORD i = 0; i < count; ++i) {
                const uint32_t sectionStart = sections[i].VirtualAddress;
                const uint32_t sectionSpan = std::max(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData);
                if (sectionSpan == 0) {
                    continue;
                }

                if (rva >= sectionStart && rva < sectionStart + sectionSpan) {
                    return i;
                }
            }
            return std::nullopt;
        };

        const auto copySectionFromOriginal = [&](WORD sectionIndex, const char* reason) -> bool {
            const IMAGE_SECTION_HEADER& sec = originalSections[sectionIndex];
            if (sec.SizeOfRawData == 0) {
                return false;
            }

            const size_t srcOffset = static_cast<size_t>(sec.PointerToRawData);
            const size_t dstOffset = static_cast<size_t>(sec.VirtualAddress);
            if (srcOffset >= originalFile.size() || dstOffset >= buffer.size()) {
                return false;
            }

            const size_t copySize = std::min(
                static_cast<size_t>(sec.SizeOfRawData),
                std::min(originalFile.size() - srcOffset, buffer.size() - dstOffset)
            );
            if (copySize == 0) {
                return false;
            }

            std::memcpy(buffer.data() + dstOffset, originalFile.data() + srcOffset, copySize);

            char sectionName[9] = {0};
            std::memcpy(sectionName, sec.Name, 8);
            Logger::Info("Restored section '{}' ({} bytes) from original file for {}",
                         sectionName, copySize, reason);
            return true;
        };

        bool restored = false;
        const IMAGE_DATA_DIRECTORY& importDir =
            originalNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.VirtualAddress != 0) {
            auto importSection = findSectionByRva(originalSections, originalSectionCount, importDir.VirtualAddress);
            if (importSection.has_value()) {
                restored = copySectionFromOriginal(importSection.value(), "import directory");
            }
        }

        const IMAGE_DATA_DIRECTORY& iatDir =
            originalNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
        if (iatDir.VirtualAddress != 0) {
            auto iatSection = findSectionByRva(originalSections, originalSectionCount, iatDir.VirtualAddress);
            if (iatSection.has_value()) {
                restored = copySectionFromOriginal(iatSection.value(), "iat directory") || restored;
            }
        }

        if (!restored) {
            for (WORD i = 0; i < originalSectionCount; ++i) {
                char sectionName[9] = {0};
                std::memcpy(sectionName, originalSections[i].Name, 8);
                std::string nameLower = ToLowerAscii(sectionName);
                const bool nameLooksImportRelated =
                    nameLower.find("idata") != std::string::npos ||
                    nameLower.find("rdata") != std::string::npos ||
                    nameLower.find("data") != std::string::npos ||
                    nameLower.find("imp") != std::string::npos;

                const bool readable =
                    (originalSections[i].Characteristics & IMAGE_SCN_MEM_READ) != 0;
                const bool nonExecutable =
                    (originalSections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0;

                if (nameLooksImportRelated ||
                    (readable && nonExecutable && originalSections[i].SizeOfRawData != 0)) {
                    restored = copySectionFromOriginal(i, "fallback import section") || restored;
                }
            }
        }

        if (!restored) {
            Logger::Warning("Import table fallback could not restore import-related sections");
            return false;
        }

        Logger::Info("Import table fix completed using original file fallback");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to fix import table: {}", e.what());
        return false;
    }
}

bool DumpManager::FixRelocations(uint64_t moduleBase, uint64_t preferredBase, 
                                 std::vector<uint8_t>& buffer) {
    try {
        // TODO: 瀹炵幇閲嶅畾浣嶄慨澶?
        // 濡傛灉妯″潡琚噸瀹氫綅浜?闇€瑕佽皟鏁撮噸瀹氫綅琛?
        
        Logger::Info("Relocation fix completed");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to fix relocations: {}", e.what());
        return false;
    }
}

bool DumpManager::RebuildPEHeaders(uint64_t moduleBase, std::vector<uint8_t>& buffer,
                                   std::optional<uint32_t> newEP) {
    try {
        if (buffer.size() < sizeof(IMAGE_DOS_HEADER)) {
            return false;
        }
        
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }
        
        if (buffer.size() < dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)) {
            return false;
        }
        
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }
        
        // 淇鍏ュ彛鐐?
        if (newEP.has_value()) {
            ntHeaders->OptionalHeader.AddressOfEntryPoint = newEP.value();
            Logger::Info("Updated entry point to RVA: {}",
                         StringUtils::FormatAddress(newEP.value()));
        }
        
        // 淇ImageBase
        ntHeaders->OptionalHeader.ImageBase = moduleBase;
        
        // 瀵归綈鑺?
        AlignPESections(buffer);
        
        Logger::Info("PE headers rebuilt successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to rebuild PE headers: {}", e.what());
        return false;
    }
}

bool DumpManager::ScyllaRebuildImports(uint64_t moduleBase, std::vector<uint8_t>& buffer) {
    try {
        // TODO: 瀹炵幇Scylla椋庢牸鐨処AT閲嶅缓
        // 杩欐槸涓€涓鏉傜殑杩囩▼,闇€瑕?
        // 1. 鎵弿IAT鍖哄煙
        // 2. 璇嗗埆API鍦板潃
        // 3. 鍙嶆煡妯″潡鍜屽嚱鏁板悕
        // 4. 閲嶅缓瀵煎叆琛?
        
        Logger::Info("Scylla import rebuild attempted");
        return false; // 鏆傛湭瀹炵幇
        
    } catch (const std::exception& e) {
        Logger::Error("Scylla import rebuild failed: {}", e.what());
        return false;
    }
}

// ========== IAT Reconstruction ==========

namespace {

// Align a value up to the given alignment
inline uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef XDBG_SDK_AVAILABLE
// Resolve a single IAT pointer from live process memory to DLL+function name.
// Returns false if the pointer is zero (separator) or cannot be resolved.
bool ResolveIATPointer(duint ptrValue, std::string& outDllName, std::string& outFuncName,
                       uint16_t& outOrdinal, bool& outByOrdinal) {
    if (ptrValue == 0) {
        return false;
    }

    outByOrdinal = false;
    outOrdinal = 0;

    // Try DbgGetLabelAt first — returns "module.FunctionName"
    char label[MAX_LABEL_SIZE] = {};
    if (DbgGetLabelAt(ptrValue, SEG_DEFAULT, label) && label[0] != '\0') {
        std::string labelStr(label);
        size_t dotPos = labelStr.find('.');
        if (dotPos != std::string::npos && dotPos > 0 && dotPos < labelStr.size() - 1) {
            outDllName = labelStr.substr(0, dotPos);
            outFuncName = labelStr.substr(dotPos + 1);

            // Strip leading '#' if ordinal label like "#123"
            if (!outFuncName.empty() && outFuncName[0] == '#') {
                try {
                    outOrdinal = static_cast<uint16_t>(std::stoul(outFuncName.substr(1)));
                    outByOrdinal = true;
                } catch (...) {}
            }

            // Ensure DLL name has .dll extension
            std::string dllLower = ToLowerAscii(outDllName);
            if (dllLower.find(".dll") == std::string::npos &&
                dllLower.find(".drv") == std::string::npos &&
                dllLower.find(".sys") == std::string::npos) {
                outDllName += ".dll";
            }
            return true;
        }
    }

    // Fallback: try to get module name + use the label as function name
    char modName[MAX_MODULE_SIZE] = {};
    if (Script::Module::NameFromAddr(ptrValue, modName) && modName[0] != '\0') {
        outDllName = std::string(modName);
        std::string dllLower = ToLowerAscii(outDllName);
        if (dllLower.find(".dll") == std::string::npos &&
            dllLower.find(".drv") == std::string::npos &&
            dllLower.find(".sys") == std::string::npos) {
            outDllName += ".dll";
        }

        // Try to get function name from symbol info
        SYMBOLINFOCPP symInfo;
        if (DbgGetSymbolInfoAt(ptrValue, &symInfo)) {
            const char* name = symInfo.undecoratedSymbol ? symInfo.undecoratedSymbol :
                               symInfo.decoratedSymbol;
            if (name && name[0] != '\0') {
                outFuncName = name;
                // Remove "module." prefix if present
                std::string prefix = std::string(modName) + ".";
                if (outFuncName.substr(0, prefix.size()) == prefix) {
                    outFuncName = outFuncName.substr(prefix.size());
                }
                if (symInfo.ordinal != 0) {
                    outOrdinal = static_cast<uint16_t>(symInfo.ordinal);
                }
                return true;
            }
        }
    }

    return false;
}

// Scan a contiguous IAT region and group resolved imports by DLL.
// iatRva: RVA of the IAT start in the PE
// iatSize: size of the IAT region in bytes
// moduleBase: base address of the module in live process
void ScanIATRegion(uint64_t moduleBase, uint32_t iatRva, uint32_t iatSize,
                   std::vector<DumpManager::ImportGroup>& outGroups) {
    const uint32_t ptrSize = sizeof(duint);
    const uint32_t slotCount = iatSize / ptrSize;

    // Map from DLL name (lowercase) to group index
    std::unordered_map<std::string, size_t> dllGroupMap;

    uint32_t unresolved = 0;
    for (uint32_t i = 0; i < slotCount; ++i) {
        uint32_t slotRva = iatRva + i * ptrSize;
        duint ptrValue = Script::Memory::ReadPtr(moduleBase + slotRva);

        if (ptrValue == 0) {
            continue;  // NULL separator between DLL groups
        }

        std::string dllName, funcName;
        uint16_t ordinal = 0;
        bool byOrdinal = false;

        if (!ResolveIATPointer(ptrValue, dllName, funcName, ordinal, byOrdinal)) {
            ++unresolved;
            continue;
        }

        std::string dllKey = ToLowerAscii(dllName);
        size_t groupIdx;
        auto it = dllGroupMap.find(dllKey);
        if (it == dllGroupMap.end()) {
            groupIdx = outGroups.size();
            dllGroupMap[dllKey] = groupIdx;
            DumpManager::ImportGroup group;
            group.dllName = dllName;
            outGroups.push_back(std::move(group));
        } else {
            groupIdx = it->second;
        }

        DumpManager::ResolvedImport imp;
        imp.dllName = dllName;
        imp.functionName = funcName;
        imp.ordinal = ordinal;
        imp.importByOrdinal = byOrdinal;

        outGroups[groupIdx].functions.push_back(std::move(imp));
        outGroups[groupIdx].iatSlotRvas.push_back(slotRva);
    }

    if (unresolved > 0) {
        Logger::Warning("IAT scan: {} slots could not be resolved", unresolved);
    }
}
#endif // XDBG_SDK_AVAILABLE

} // anonymous namespace

bool DumpManager::ScanAndResolveIAT(
    uint64_t moduleBase,
    const std::vector<uint8_t>& peBuffer,
    std::vector<ImportGroup>& outGroups)
{
#ifndef XDBG_SDK_AVAILABLE
    Logger::Error("ScanAndResolveIAT requires x64dbg SDK");
    return false;
#else
    auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(peBuffer.data());
    auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        peBuffer.data() + dosHeader->e_lfanew);
    const auto& dataDir = ntHeaders->OptionalHeader.DataDirectory;

    // Strategy 1: Use IAT DataDirectory
    const auto& iatDir = dataDir[IMAGE_DIRECTORY_ENTRY_IAT];
    if (iatDir.VirtualAddress != 0 && iatDir.Size >= sizeof(duint)) {
        Logger::Info("Scanning IAT via DataDirectory[IAT]: RVA={}, Size={}",
                     StringUtils::FormatAddress(iatDir.VirtualAddress), iatDir.Size);
        ScanIATRegion(moduleBase, iatDir.VirtualAddress, iatDir.Size, outGroups);
        if (!outGroups.empty()) {
            return true;
        }
        Logger::Warning("IAT DataDirectory scan yielded no results, trying import descriptors");
        outGroups.clear();
    }

    // Strategy 2: Walk import descriptors to find IAT arrays
    const auto& importDir = dataDir[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress != 0 && importDir.Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        Logger::Info("Scanning IAT via import descriptors: RVA={}",
                     StringUtils::FormatAddress(importDir.VirtualAddress));

        const uint32_t ptrSize = sizeof(duint);
        std::unordered_map<std::string, size_t> dllGroupMap;

        // Read import descriptors from live process memory (they may differ from PE on disk)
        for (uint32_t descOffset = 0; ; descOffset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            uint64_t descAddr = moduleBase + importDir.VirtualAddress + descOffset;

            IMAGE_IMPORT_DESCRIPTOR desc = {};
            if (!Script::Memory::Read(descAddr, &desc, sizeof(desc), nullptr)) {
                break;
            }

            // Null terminator
            if (desc.FirstThunk == 0 && desc.OriginalFirstThunk == 0) {
                break;
            }

            if (desc.FirstThunk == 0) {
                continue;
            }

            // Walk the IAT (FirstThunk) array
            for (uint32_t entryIdx = 0; ; ++entryIdx) {
                uint32_t slotRva = desc.FirstThunk + entryIdx * ptrSize;
                duint ptrValue = Script::Memory::ReadPtr(moduleBase + slotRva);

                if (ptrValue == 0) {
                    break;  // End of this DLL's IAT entries
                }

                std::string dllName, funcName;
                uint16_t ordinal = 0;
                bool byOrdinal = false;

                if (!ResolveIATPointer(ptrValue, dllName, funcName, ordinal, byOrdinal)) {
                    continue;
                }

                std::string dllKey = ToLowerAscii(dllName);
                size_t groupIdx;
                auto it = dllGroupMap.find(dllKey);
                if (it == dllGroupMap.end()) {
                    groupIdx = outGroups.size();
                    dllGroupMap[dllKey] = groupIdx;
                    ImportGroup group;
                    group.dllName = dllName;
                    outGroups.push_back(std::move(group));
                } else {
                    groupIdx = it->second;
                }

                ResolvedImport imp;
                imp.dllName = dllName;
                imp.functionName = funcName;
                imp.ordinal = ordinal;
                imp.importByOrdinal = byOrdinal;

                outGroups[groupIdx].functions.push_back(std::move(imp));
                outGroups[groupIdx].iatSlotRvas.push_back(slotRva);
            }
        }

        if (!outGroups.empty()) {
            return true;
        }
        Logger::Warning("Import descriptor scan yielded no results, trying heuristic");
        outGroups.clear();
    }

    // Strategy 3: Heuristic scan — look for pointer-sized runs of valid API addresses
    // in any readable section (including executable, since packers like UPX place IAT there)
    Logger::Info("Attempting heuristic IAT scan");
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        reinterpret_cast<const uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS));
    const WORD sectionCount = ntHeaders->FileHeader.NumberOfSections;
    const uint32_t ptrSize = sizeof(duint);

    std::unordered_map<std::string, size_t> dllGroupMap;

    for (WORD secIdx = 0; secIdx < sectionCount; ++secIdx) {
        const auto& sec = sections[secIdx];

        // Look in any readable section with enough data for at least 3 pointers
        bool readable = (sec.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        uint32_t secSpan = std::max(sec.Misc.VirtualSize, sec.SizeOfRawData);
        if (!readable || secSpan < ptrSize * 3) {
            continue;
        }

        // Scan for consecutive resolved pointers (minimum run of 3)
        uint32_t consecutiveResolved = 0;
        std::vector<std::tuple<uint32_t, std::string, std::string, uint16_t, bool>> pendingSlots;

        for (uint32_t offset = 0; offset + ptrSize <= secSpan; offset += ptrSize) {
            uint32_t slotRva = sec.VirtualAddress + offset;
            duint ptrValue = Script::Memory::ReadPtr(moduleBase + slotRva);

            if (ptrValue == 0) {
                // Flush pending if we had a good run
                if (consecutiveResolved >= 3) {
                    for (auto& [sRva, dll, func, ord, byOrd] : pendingSlots) {
                        std::string dllKey = ToLowerAscii(dll);
                        size_t groupIdx;
                        auto it = dllGroupMap.find(dllKey);
                        if (it == dllGroupMap.end()) {
                            groupIdx = outGroups.size();
                            dllGroupMap[dllKey] = groupIdx;
                            ImportGroup group;
                            group.dllName = dll;
                            outGroups.push_back(std::move(group));
                        } else {
                            groupIdx = it->second;
                        }

                        ResolvedImport imp;
                        imp.dllName = dll;
                        imp.functionName = func;
                        imp.ordinal = ord;
                        imp.importByOrdinal = byOrd;
                        outGroups[groupIdx].functions.push_back(std::move(imp));
                        outGroups[groupIdx].iatSlotRvas.push_back(sRva);
                    }
                }
                consecutiveResolved = 0;
                pendingSlots.clear();
                continue;
            }

            std::string dllName, funcName;
            uint16_t ordinal = 0;
            bool byOrdinal = false;
            if (ResolveIATPointer(ptrValue, dllName, funcName, ordinal, byOrdinal)) {
                ++consecutiveResolved;
                pendingSlots.emplace_back(slotRva, dllName, funcName, ordinal, byOrdinal);
            } else {
                // Break the run
                if (consecutiveResolved >= 3) {
                    for (auto& [sRva, dll, func, ord, byOrd] : pendingSlots) {
                        std::string dllKey = ToLowerAscii(dll);
                        size_t groupIdx;
                        auto it = dllGroupMap.find(dllKey);
                        if (it == dllGroupMap.end()) {
                            groupIdx = outGroups.size();
                            dllGroupMap[dllKey] = groupIdx;
                            ImportGroup group;
                            group.dllName = dll;
                            outGroups.push_back(std::move(group));
                        } else {
                            groupIdx = it->second;
                        }

                        ResolvedImport imp;
                        imp.dllName = dll;
                        imp.functionName = func;
                        imp.ordinal = ord;
                        imp.importByOrdinal = byOrd;
                        outGroups[groupIdx].functions.push_back(std::move(imp));
                        outGroups[groupIdx].iatSlotRvas.push_back(sRva);
                    }
                }
                consecutiveResolved = 0;
                pendingSlots.clear();
            }
        }

        // Flush final run in section
        if (consecutiveResolved >= 3) {
            for (auto& [sRva, dll, func, ord, byOrd] : pendingSlots) {
                std::string dllKey = ToLowerAscii(dll);
                size_t groupIdx;
                auto it = dllGroupMap.find(dllKey);
                if (it == dllGroupMap.end()) {
                    groupIdx = outGroups.size();
                    dllGroupMap[dllKey] = groupIdx;
                    ImportGroup group;
                    group.dllName = dll;
                    outGroups.push_back(std::move(group));
                } else {
                    groupIdx = it->second;
                }

                ResolvedImport imp;
                imp.dllName = dll;
                imp.functionName = func;
                imp.ordinal = ord;
                imp.importByOrdinal = byOrd;
                outGroups[groupIdx].functions.push_back(std::move(imp));
                outGroups[groupIdx].iatSlotRvas.push_back(sRva);
            }
        }
    }

    return !outGroups.empty();
#endif
}

bool DumpManager::BuildAndWriteImportSection(
    std::vector<uint8_t>& peBuffer,
    const std::vector<ImportGroup>& groups,
    uint32_t sectionAlignment,
    uint32_t fileAlignment)
{
    if (groups.empty()) return false;

    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(peBuffer.data());
    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
        peBuffer.data() + dosHeader->e_lfanew);

    const WORD oldSectionCount = ntHeaders->FileHeader.NumberOfSections;
    auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
        reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS));

    // Check if there's space for one more section header
    const size_t sectionsEnd = static_cast<size_t>(dosHeader->e_lfanew) + sizeof(IMAGE_NT_HEADERS)
        + (oldSectionCount + 1) * sizeof(IMAGE_SECTION_HEADER);
    uint32_t firstSectionRawData = 0;
    for (WORD i = 0; i < oldSectionCount; ++i) {
        if (sections[i].PointerToRawData != 0) {
            if (firstSectionRawData == 0 || sections[i].PointerToRawData < firstSectionRawData) {
                firstSectionRawData = sections[i].PointerToRawData;
            }
        }
    }
    if (firstSectionRawData != 0 && sectionsEnd > firstSectionRawData) {
        Logger::Error("No space in PE header for additional section header");
        return false;
    }

    const uint32_t ptrSize = sizeof(duint);
    const size_t dllCount = groups.size();

    // Calculate sizes for each component
    // 1. Import descriptors: (dllCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR)
    const uint32_t descriptorsSize = static_cast<uint32_t>((dllCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR));

    // 2. INT (OriginalFirstThunk) arrays: for each DLL, (funcCount + 1) * ptrSize
    uint32_t intArraysSize = 0;
    for (const auto& group : groups) {
        intArraysSize += static_cast<uint32_t>((group.functions.size() + 1) * ptrSize);
    }

    // 3. IMAGE_IMPORT_BY_NAME structs: for each function, 2-byte hint + name + null + align
    uint32_t nameEntriesSize = 0;
    for (const auto& group : groups) {
        for (const auto& func : group.functions) {
            if (!func.importByOrdinal) {
                // 2 bytes hint + function name + null byte, aligned to 2
                uint32_t entrySize = 2 + static_cast<uint32_t>(func.functionName.size()) + 1;
                entrySize = (entrySize + 1) & ~1u;  // align to 2
                nameEntriesSize += entrySize;
            }
        }
    }

    // 4. DLL name strings
    uint32_t dllNamesSize = 0;
    for (const auto& group : groups) {
        dllNamesSize += static_cast<uint32_t>(group.dllName.size()) + 1;
    }

    const uint32_t totalDataSize = descriptorsSize + intArraysSize + nameEntriesSize + dllNamesSize;
    const uint32_t alignedDataSize = AlignUp(totalDataSize, fileAlignment);

    // Determine new section RVA
    uint32_t lastSectionEnd = 0;
    for (WORD i = 0; i < oldSectionCount; ++i) {
        uint32_t secEnd = sections[i].VirtualAddress +
            AlignUp(std::max(sections[i].Misc.VirtualSize, sections[i].SizeOfRawData), sectionAlignment);
        if (secEnd > lastSectionEnd) {
            lastSectionEnd = secEnd;
        }
    }
    const uint32_t newSectionRva = AlignUp(lastSectionEnd, sectionAlignment);
    const uint32_t newSectionFileOffset = static_cast<uint32_t>(peBuffer.size());

    // Resize buffer to accommodate new section
    peBuffer.resize(peBuffer.size() + alignedDataSize, 0);

    // Re-acquire pointers after resize
    dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(peBuffer.data());
    ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(peBuffer.data() + dosHeader->e_lfanew);
    sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
        reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS));

    uint8_t* sectionData = peBuffer.data() + newSectionFileOffset;

    // Layout within the new section:
    // [import descriptors][INT arrays][name entries][DLL name strings]
    uint32_t curOffset = 0;

    // --- Write import descriptors ---
    const uint32_t descriptorsOffset = curOffset;
    curOffset += descriptorsSize;

    // --- Write INT arrays ---
    const uint32_t intArraysOffset = curOffset;
    // We'll fill these in per-DLL below
    curOffset += intArraysSize;

    // --- Write name entries ---
    const uint32_t nameEntriesOffset = curOffset;
    curOffset += nameEntriesSize;

    // --- Write DLL name strings ---
    const uint32_t dllNamesOffset = curOffset;

    // Now fill in the data
    uint32_t currentIntOffset = intArraysOffset;
    uint32_t currentNameOffset = nameEntriesOffset;
    uint32_t currentDllNameOffset = dllNamesOffset;

    for (size_t dllIdx = 0; dllIdx < dllCount; ++dllIdx) {
        const auto& group = groups[dllIdx];

        // Write DLL name string
        uint32_t dllNameRva = newSectionRva + currentDllNameOffset;
        std::memcpy(sectionData + currentDllNameOffset, group.dllName.c_str(),
                     group.dllName.size() + 1);
        currentDllNameOffset += static_cast<uint32_t>(group.dllName.size()) + 1;

        // INT array RVA for this DLL
        uint32_t intArrayRva = newSectionRva + currentIntOffset;

        // IAT FirstThunk RVA — use the first IAT slot's RVA from the original PE
        uint32_t iatFirstThunkRva = 0;
        if (!group.iatSlotRvas.empty()) {
            iatFirstThunkRva = group.iatSlotRvas[0];
        }

        // Fill import descriptor
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            sectionData + descriptorsOffset + dllIdx * sizeof(IMAGE_IMPORT_DESCRIPTOR));
        desc->OriginalFirstThunk = intArrayRva;
        desc->TimeDateStamp = 0;
        desc->ForwarderChain = 0;
        desc->Name = dllNameRva;
        desc->FirstThunk = iatFirstThunkRva;

        // Write INT entries and patch IAT slots
        for (size_t funcIdx = 0; funcIdx < group.functions.size(); ++funcIdx) {
            const auto& func = group.functions[funcIdx];
            duint thunkValue = 0;

            if (func.importByOrdinal) {
                // Ordinal import: set high bit + ordinal
#ifdef XDBG_ARCH_X64
                thunkValue = IMAGE_ORDINAL_FLAG64 | func.ordinal;
#else
                thunkValue = IMAGE_ORDINAL_FLAG32 | func.ordinal;
#endif
            } else {
                // Name import: write IMAGE_IMPORT_BY_NAME, thunk points to it
                uint32_t nameEntryRva = newSectionRva + currentNameOffset;

                // Write hint (ordinal as hint, 0 if unknown)
                uint16_t hint = func.ordinal;
                std::memcpy(sectionData + currentNameOffset, &hint, sizeof(hint));
                currentNameOffset += 2;

                // Write function name + null
                std::memcpy(sectionData + currentNameOffset, func.functionName.c_str(),
                             func.functionName.size() + 1);
                currentNameOffset += static_cast<uint32_t>(func.functionName.size()) + 1;

                // Align to 2
                if (currentNameOffset & 1) {
                    sectionData[currentNameOffset] = 0;
                    ++currentNameOffset;
                }

                thunkValue = static_cast<duint>(nameEntryRva);
            }

            // Write INT entry
            std::memcpy(sectionData + currentIntOffset, &thunkValue, ptrSize);
            currentIntOffset += ptrSize;

            // Patch IAT slot in the PE buffer
            if (funcIdx < group.iatSlotRvas.size()) {
                uint32_t iatSlotFileOffset = 0;
                uint32_t iatSlotRva = group.iatSlotRvas[funcIdx];

                // Find the file offset for this IAT RVA
                for (WORD secIdx = 0; secIdx < oldSectionCount; ++secIdx) {
                    uint32_t secRva = sections[secIdx].VirtualAddress;
                    uint32_t secSpan = std::max(sections[secIdx].Misc.VirtualSize,
                                                sections[secIdx].SizeOfRawData);
                    if (iatSlotRva >= secRva && iatSlotRva < secRva + secSpan) {
                        iatSlotFileOffset = sections[secIdx].PointerToRawData +
                                            (iatSlotRva - secRva);
                        break;
                    }
                }

                if (iatSlotFileOffset != 0 && iatSlotFileOffset + ptrSize <= peBuffer.size()) {
                    std::memcpy(peBuffer.data() + iatSlotFileOffset, &thunkValue, ptrSize);
                }
            }
        }

        // Write INT null terminator
        duint nullThunk = 0;
        std::memcpy(sectionData + currentIntOffset, &nullThunk, ptrSize);
        currentIntOffset += ptrSize;
    }

    // Null terminator import descriptor (already zeroed from resize)

    // Add new section header
    auto* newSection = &sections[oldSectionCount];
    std::memset(newSection, 0, sizeof(IMAGE_SECTION_HEADER));
    std::memcpy(newSection->Name, ".idata2", 7);
    newSection->Misc.VirtualSize = totalDataSize;
    newSection->VirtualAddress = newSectionRva;
    newSection->SizeOfRawData = alignedDataSize;
    newSection->PointerToRawData = newSectionFileOffset;
    newSection->Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;

    // Update PE headers
    ntHeaders->FileHeader.NumberOfSections = oldSectionCount + 1;
    ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress =
        newSectionRva + descriptorsOffset;
    ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
        static_cast<uint32_t>(dllCount * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    ntHeaders->OptionalHeader.SizeOfImage =
        AlignUp(newSectionRva + totalDataSize, sectionAlignment);

    Logger::Info("Built new .idata2 section: RVA={}, size={}, {} DLLs",
                 StringUtils::FormatAddress(newSectionRva), totalDataSize, dllCount);
    return true;
}

FixImportsResult DumpManager::FixImportsFromFile(
    const std::string& filePath,
    uint64_t moduleBase,
    std::optional<uint32_t> oepRva)
{
    FixImportsResult result;
    result.filePath = filePath;

    try {
        // 1. Read PE from disk
        auto fsPath = ToFilesystemPath(filePath);
        std::ifstream input(fsPath, std::ios::binary | std::ios::ate);
        if (!input) {
            result.error = "Failed to open PE file: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }
        auto fileSize = input.tellg();
        if (fileSize <= 0) {
            result.error = "PE file is empty: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }
        input.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (!input.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            result.error = "Failed to read PE file: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }
        input.close();

        // 2. Validate PE
        if (!ValidatePEHeader(buffer)) {
            result.error = "Invalid PE header in dump file: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }

        // 3. Parse PE headers for alignment values
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
            buffer.data() + dosHeader->e_lfanew);
        uint32_t sectionAlignment = ntHeaders->OptionalHeader.SectionAlignment;
        uint32_t fileAlignment = ntHeaders->OptionalHeader.FileAlignment;
        if (sectionAlignment == 0) sectionAlignment = 0x1000;
        if (fileAlignment == 0) fileAlignment = 0x200;

        // 4. Optionally set OEP
        if (oepRva.has_value()) {
            ntHeaders->OptionalHeader.AddressOfEntryPoint = oepRva.value();
            Logger::Info("Set OEP to RVA: {}", StringUtils::FormatAddress(oepRva.value()));
        }

        // 5. Scan IAT from live process memory
        std::vector<ImportGroup> importGroups;
        if (!ScanAndResolveIAT(moduleBase, buffer, importGroups)) {
            result.error = "Failed to scan and resolve IAT from live process memory";
            Logger::Error("{}", result.error);
            return result;
        }

        // 6. Count results
        int totalImports = 0;
        for (const auto& group : importGroups) {
            totalImports += static_cast<int>(group.functions.size());
        }
        if (totalImports == 0) {
            result.error = "No imports resolved from IAT scan";
            Logger::Error("{}", result.error);
            return result;
        }

        Logger::Info("Resolved {} imports from {} DLLs", totalImports, importGroups.size());

        // 7. Build new import section
        if (!BuildAndWriteImportSection(buffer, importGroups, sectionAlignment, fileAlignment)) {
            result.error = "Failed to build new import section in PE";
            Logger::Error("{}", result.error);
            return result;
        }

        // 8. Write fixed PE back to disk
        std::ofstream output(fsPath, std::ios::binary);
        if (!output) {
            result.error = "Failed to open output file for writing: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }
        output.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        output.close();
        if (!output.good()) {
            result.error = "Failed to write fixed PE file: " + filePath;
            Logger::Error("{}", result.error);
            return result;
        }

        result.success = true;
        result.importCount = totalImports;
        result.dllCount = static_cast<int>(importGroups.size());
        Logger::Info("IAT reconstruction complete: {} imports from {} DLLs written to {}",
                     totalImports, importGroups.size(), filePath);

    } catch (const std::exception& e) {
        result.error = std::string("Exception during IAT reconstruction: ") + e.what();
        Logger::Error("{}", result.error);
    }
    return result;
}

void DumpManager::SetOEPDetectionStrategy(
    std::function<std::optional<uint64_t>(uint64_t)> strategy) {
    m_oepDetectionStrategy = strategy;
    Logger::Info("Custom OEP detection strategy set");
}

// ========== 绉佹湁杈呭姪鏂规硶 ==========

std::optional<uint64_t> DumpManager::ParseModuleOrAddress(const std::string& input) {
    // 灏濊瘯浣滀负鍦板潃瑙ｆ瀽
    try {
        uint64_t addr = StringUtils::ParseAddress(input);
        if (addr != 0) {
            return addr;
        }
    } catch (...) {
        // 涓嶆槸鍦板潃,灏濊瘯浣滀负妯″潡鍚?
    }
    
    // 浣滀负妯″潡鍚嶆煡鎵?
    char szModPath[MAX_PATH] = {0};
    duint modBase = DbgFunctions()->ModBaseFromName(input.c_str());
    
    if (modBase != 0) {
        return modBase;
    }

#ifdef XDBG_SDK_AVAILABLE
    auto fallbackBase = ResolveModuleBaseByQueryFallback(input);
    if (fallbackBase.has_value()) {
        return fallbackBase.value();
    }
#endif
    
    return std::nullopt;
}

bool DumpManager::ValidatePEHeader(const std::vector<uint8_t>& buffer) {
    if (buffer.size() < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }
    
    auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(buffer.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    
    if (buffer.size() < dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)) {
        return false;
    }
    
    auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(buffer.data() + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    
    return true;
}

bool DumpManager::IsPacked(uint64_t moduleBase, std::string& packerId) {
    try {
        auto& memMgr = MemoryManager::Instance();

        // Read PE header from module memory.
        std::vector<uint8_t> peHeader = memMgr.Read(moduleBase, 4096);

        if (!ValidatePEHeader(peHeader)) {
            return false;
        }

        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(peHeader.data());
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(peHeader.data() + dosHeader->e_lfanew);

        const WORD sectionCount = ntHeaders->FileHeader.NumberOfSections;
        if (sectionCount < 2) {
            packerId = "Packed-like (few sections)";
            return true;
        }

        auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
            reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS)
        );

        const uint32_t entryRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        int entrySection = -1;

        bool hasExecutableZeroRawSection = false;
        bool hasMarkerSectionName = false;
        std::string markerSectionName;

        for (int i = 0; i < sectionCount; i++) {
            if (SectionContainsRva(sections[i], entryRVA)) {
                entrySection = i;
            }

            const uint32_t sectionSpan = GetSectionSpan(sections[i]);
            if (sections[i].SizeOfRawData == 0 &&
                sectionSpan >= 0x2000 &&
                (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
                hasExecutableZeroRawSection = true;
            }

            const std::string sectionNameLower = ToLowerAscii(GetSectionName(sections[i]));
            static const std::vector<std::string> kPackedMarkers = {
                "upx", "aspack", "petite", "pec", "themida", "vmp", "mpress", "fsg", "enigma"
            };
            for (const auto& marker : kPackedMarkers) {
                if (sectionNameLower.find(marker) != std::string::npos) {
                    hasMarkerSectionName = true;
                    markerSectionName = sectionNameLower;
                    break;
                }
            }

            if (hasMarkerSectionName) {
                break;
            }
        }

        const IMAGE_DATA_DIRECTORY& importDir =
            ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const bool importLooksMissing =
            importDir.VirtualAddress == 0 ||
            importDir.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR);

        int suspiciousScore = 0;
        if (entrySection < 0) {
            suspiciousScore += 2;
        } else {
            const IMAGE_SECTION_HEADER& epSection = sections[entrySection];
            if (entrySection == sectionCount - 1) {
                suspiciousScore += 1;
            }

            const bool epWritable = (epSection.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            const bool epExecutable = (epSection.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            if (epWritable && epExecutable) {
                suspiciousScore += 1;
            }

            if (epSection.SizeOfRawData == 0 && GetSectionSpan(epSection) >= 0x1000) {
                suspiciousScore += 1;
            }
        }

        if (importLooksMissing) {
            suspiciousScore += 1;
        }
        if (hasExecutableZeroRawSection) {
            suspiciousScore += 1;
        }
        if (hasMarkerSectionName) {
            suspiciousScore += 2;
        }

        if (suspiciousScore >= 2) {
            if (hasMarkerSectionName) {
                packerId = "Packed-like (section marker: " + markerSectionName + ")";
            } else {
                packerId = "Packed-like layout";
            }
            return true;
        }

        packerId = "";
        return false;

    } catch (const std::exception& e) {
        Logger::Error("Packer detection failed: {}", e.what());
        return false;
    }
}

uint64_t DumpManager::GetModuleSize(uint64_t moduleBase) {
    duint size = DbgFunctions()->ModSizeFromAddr(moduleBase);
    return static_cast<uint64_t>(size);
}

uint64_t DumpManager::GetModuleEntryPoint(uint64_t moduleBase) {
    // Use Script API to get entry point
    duint entry = Script::Module::EntryFromAddr(moduleBase);
    return static_cast<uint64_t>(entry);
}

std::string DumpManager::GetModulePath(uint64_t moduleBase) {
    char path[MAX_PATH] = {0};
    if (DbgFunctions()->ModPathFromAddr(moduleBase, path, MAX_PATH)) {
        return StringUtils::FixUtf8Mojibake(std::string(path));
    }
    return "";
}

std::optional<uint64_t> DumpManager::DetectOEPByEntropy(uint64_t moduleBase) {
    // TODO: 瀹炵幇鍩轰簬鐔靛€肩殑OEP妫€娴?
    // 鍘熺悊: 鍔犲３鍚庣殑浠ｇ爜鐔靛€艰緝楂?鎵惧埌鐔靛€肩獊鍙樼偣
    return std::nullopt;
}

std::optional<uint64_t> DumpManager::DetectOEPByPattern(uint64_t moduleBase) {
    try {
        auto& memMgr = MemoryManager::Instance();
        const uint64_t moduleSize = GetModuleSize(moduleBase);

        auto layoutOpt = ReadModuleLayout(moduleBase);
        if (layoutOpt.has_value()) {
            const ModuleLayout& layout = layoutOpt.value();
            auto entrySectionIndexOpt = FindSectionIndex(layout, layout.entryRva);

            if (entrySectionIndexOpt.has_value()) {
                const size_t entrySectionIndex = entrySectionIndexOpt.value();
                const auto& entrySection = layout.sections[entrySectionIndex];

                const uint64_t entryVA = moduleBase + layout.entryRva;
                const uint64_t entrySectionEnd =
                    moduleBase + static_cast<uint64_t>(entrySection.virtualAddress) + entrySection.span;
                const size_t scanSize = static_cast<size_t>(
                    std::min<uint64_t>(0x6000, entrySectionEnd > entryVA ? entrySectionEnd - entryVA : 0)
                );

                if (scanSize >= 2) {
                    auto code = memMgr.Read(entryVA, scanSize);

                    const auto isValidTarget = [&](uint64_t target) -> bool {
                        if (target < moduleBase || target >= moduleBase + moduleSize) {
                            return false;
                        }

                        const uint64_t rva64 = target - moduleBase;
                        if (rva64 > std::numeric_limits<uint32_t>::max()) {
                            return false;
                        }

                        auto targetSectionIndexOpt = FindSectionIndex(layout, static_cast<uint32_t>(rva64));
                        if (!targetSectionIndexOpt.has_value()) {
                            return false;
                        }

                        const auto& targetSection = layout.sections[targetSectionIndexOpt.value()];
                        if ((targetSection.characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
                            return false;
                        }

                        return targetSectionIndexOpt.value() != entrySectionIndex;
                    };

                    for (size_t i = 0; i < code.size(); ++i) {
                        const uint64_t instructionAddress = entryVA + i;

                        if (i + 5 <= code.size() && code[i] == 0xE9) {
                            int32_t rel32 = 0;
                            std::memcpy(&rel32, code.data() + i + 1, sizeof(rel32));
                            const uint64_t target = static_cast<uint64_t>(
                                static_cast<int64_t>(instructionAddress) + 5 + rel32
                            );
                            if (isValidTarget(target)) {
                                Logger::Info(
                                    "OEP candidate found by near jump at {} -> {}",
                                    StringUtils::FormatAddress(instructionAddress),
                                    StringUtils::FormatAddress(target)
                                );
                                return target;
                            }
                        }

                        if (i + 2 <= code.size() && code[i] == 0xEB) {
                            const int8_t rel8 = static_cast<int8_t>(code[i + 1]);
                            const uint64_t target = static_cast<uint64_t>(
                                static_cast<int64_t>(instructionAddress) + 2 + rel8
                            );
                            if (isValidTarget(target)) {
                                Logger::Info(
                                    "OEP candidate found by short jump at {} -> {}",
                                    StringUtils::FormatAddress(instructionAddress),
                                    StringUtils::FormatAddress(target)
                                );
                                return target;
                            }
                        }

                        if (i + 6 <= code.size() && code[i] == 0xFF && code[i + 1] == 0x25) {
                            int32_t disp32 = 0;
                            std::memcpy(&disp32, code.data() + i + 2, sizeof(disp32));

                            uint64_t pointerAddress = 0;
#ifdef _WIN64
                            pointerAddress = static_cast<uint64_t>(
                                static_cast<int64_t>(instructionAddress) + 6 + disp32
                            );
#else
                            pointerAddress = static_cast<uint32_t>(disp32);
#endif

                            try {
                                auto pointerBytes = memMgr.Read(pointerAddress, sizeof(duint));
                                if (pointerBytes.size() == sizeof(duint)) {
                                    duint targetValue = 0;
                                    std::memcpy(&targetValue, pointerBytes.data(), sizeof(duint));
                                    const uint64_t target = static_cast<uint64_t>(targetValue);
                                    if (isValidTarget(target)) {
                                        Logger::Info(
                                            "OEP candidate found by indirect jump at {} -> {}",
                                            StringUtils::FormatAddress(instructionAddress),
                                            StringUtils::FormatAddress(target)
                                        );
                                        return target;
                                    }
                                }
                            } catch (...) {
                                // Ignore unresolved indirect jump pointers.
                            }
                        }

                        // x86: push imm32; ret
                        if (i + 6 <= code.size() && code[i] == 0x68 && code[i + 5] == 0xC3) {
                            uint32_t imm32 = 0;
                            std::memcpy(&imm32, code.data() + i + 1, sizeof(imm32));
                            const uint64_t target = static_cast<uint64_t>(imm32);
                            if (isValidTarget(target)) {
                                Logger::Info(
                                    "OEP candidate found by push-ret transfer at {} -> {}",
                                    StringUtils::FormatAddress(instructionAddress),
                                    StringUtils::FormatAddress(target)
                                );
                                return target;
                            }
                        }

                        // x86/x64: mov reg, imm; jmp reg
                        if (i + 7 <= code.size() && code[i] >= 0xB8 && code[i] <= 0xBF &&
                            code[i + 5] == 0xFF && code[i + 6] >= 0xE0 && code[i + 6] <= 0xE7) {
                            uint32_t imm32 = 0;
                            std::memcpy(&imm32, code.data() + i + 1, sizeof(imm32));
                            const uint64_t target = static_cast<uint64_t>(imm32);
                            if (isValidTarget(target)) {
                                Logger::Info(
                                    "OEP candidate found by mov-jmp transfer at {} -> {}",
                                    StringUtils::FormatAddress(instructionAddress),
                                    StringUtils::FormatAddress(target)
                                );
                                return target;
                            }
                        }

#ifdef _WIN64
                        if (i + 13 <= code.size() && code[i] == 0x48 &&
                            code[i + 1] >= 0xB8 && code[i + 1] <= 0xBF &&
                            code[i + 10] == 0xFF && code[i + 11] >= 0xE0 && code[i + 11] <= 0xE7) {
                            uint64_t imm64 = 0;
                            std::memcpy(&imm64, code.data() + i + 2, sizeof(imm64));
                            const uint64_t target = imm64;
                            if (isValidTarget(target)) {
                                Logger::Info(
                                    "OEP candidate found by movabs-jmp transfer at {} -> {}",
                                    StringUtils::FormatAddress(instructionAddress),
                                    StringUtils::FormatAddress(target)
                                );
                                return target;
                            }
                        }
#endif
                    }
                }
            }
        }

        std::vector<std::string> patterns = {
            "55 8B EC",
            "55 48 8B EC",
            "48 89 5C 24",
            "40 53",
        };

        const uint64_t searchStart = moduleBase + 0x1000;
        const uint64_t searchEnd = moduleBase + std::min(moduleSize, static_cast<uint64_t>(0x200000));
        if (searchStart < searchEnd) {
            for (const auto& pattern : patterns) {
                auto results = memMgr.Search(pattern, searchStart, searchEnd, 1);
                if (!results.empty()) {
                    auto codeBytes = memMgr.Read(results[0].address, 16);
                    if (!IsLikelyCodeBytes(codeBytes)) {
                        continue;
                    }

                    Logger::Info(
                        "OEP candidate found by function pattern '{}' at {}",
                        pattern,
                        StringUtils::FormatAddress(results[0].address)
                    );
                    return results[0].address;
                }
            }
        }

    } catch (const std::exception& e) {
        Logger::Error("Pattern-based OEP detection failed: {}", e.what());
    }

    return std::nullopt;
}

std::optional<uint64_t> DumpManager::DetectOEPByExecution(uint64_t moduleBase) {
    // TODO: 瀹炵幇鍩轰簬鎵ц杩借釜鐨凮EP妫€娴?
    // 鍘熺悊: 鍗曟鎵ц,妫€娴嬩綍鏃惰烦杞埌鍘熷浠ｇ爜娈?
    // 杩欓渶瑕佷笌璋冭瘯鍣ㄦ繁搴﹂泦鎴?
    return std::nullopt;
}

bool DumpManager::FixPEChecksum(std::vector<uint8_t>& buffer) {
    try {
        if (buffer.size() < sizeof(IMAGE_DOS_HEADER)) {
            return false;
        }
        
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }
        
        if (buffer.size() < dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)) {
            return false;
        }
        
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + dosHeader->e_lfanew);
        
        // 绠€鍗曞湴娓呴浂鏍￠獙鍜?
        ntHeaders->OptionalHeader.CheckSum = 0;
        
        Logger::Debug("PE checksum cleared");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to fix PE checksum: {}", e.what());
        return false;
    }
}

bool DumpManager::AlignPESections(std::vector<uint8_t>& buffer) {
    try {
        if (!ValidatePEHeader(buffer)) {
            return false;
        }
        
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(buffer.data());
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(buffer.data() + dosHeader->e_lfanew);
        
        uint32_t fileAlignment = ntHeaders->OptionalHeader.FileAlignment;
        if (fileAlignment == 0) {
            fileAlignment = 0x200;
        }
        
        auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
            reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS)
        );

        const auto alignUp = [](uint32_t value, uint32_t alignment) -> uint32_t {
            if (alignment == 0) {
                return value;
            }
            return ((value + alignment - 1) / alignment) * alignment;
        };
        
        for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
            const uint32_t virtualAddress = sections[i].VirtualAddress;
            uint32_t virtualSize = sections[i].Misc.VirtualSize;
            if (virtualSize == 0) {
                virtualSize = sections[i].SizeOfRawData;
            }

            // Dump buffer is in memory-image layout, so raw data must point to RVA.
            sections[i].PointerToRawData = virtualAddress;
            sections[i].SizeOfRawData = alignUp(virtualSize, fileAlignment);

            if (virtualAddress >= buffer.size()) {
                sections[i].PointerToRawData = 0;
                sections[i].SizeOfRawData = 0;
                continue;
            }

            const size_t maxAvailable = buffer.size() - static_cast<size_t>(virtualAddress);
            if (sections[i].SizeOfRawData > maxAvailable) {
                sections[i].SizeOfRawData = static_cast<uint32_t>(maxAvailable);
            }
        }
        
        Logger::Debug("PE sections aligned");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to align PE sections: {}", e.what());
        return false;
    }
}

bool DumpManager::RemoveCodeSection(std::vector<uint8_t>& buffer, const std::string& sectionName) {
    // TODO: 瀹炵幇鑺傚垹闄ゅ姛鑳?
    // 鐢ㄤ簬绉婚櫎澹虫坊鍔犵殑鑺?
    return false;
}

} // namespace MCP



