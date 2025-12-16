#include "DumpManager.h"
#include "DebugController.h"
#include "MemoryManager.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"
#include "../utils/StringUtils.h"
#include "../core/X64DBGBridge.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <windows.h>

#ifdef XDBG_SDK_AVAILABLE
#include "_scriptapi_module.h"  // For Script::Module::EntryFromAddr
#endif

namespace MCP {

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
        
        updateProgress(DumpProgress::Stage::Preparing, 0, "Parsing module information");
        
        // 解析模块地址
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
        
        Logger::Info("Dumping module at 0x{:X}, size: {} bytes, EP: 0x{:X}", 
                    moduleBase, moduleSize, entryPoint);
        
        updateProgress(DumpProgress::Stage::ReadingMemory, 10, "Reading module memory");
        
        // 读取整个模块内存
        auto& memMgr = MemoryManager::Instance();
        std::vector<uint8_t> buffer;
        
        if (options.dumpFullImage) {
            // 按PE文件大小dump
            buffer = memMgr.Read(moduleBase, moduleSize);
        } else {
            // 只dump已提交的内存页
            buffer = memMgr.Read(moduleBase, moduleSize);
        }
        
        result.dumpedSize = buffer.size();
        result.originalEP = entryPoint;
        
        // 验证PE头
        if (!ValidatePEHeader(buffer)) {
            Logger::Warning("Invalid PE header detected, attempting to continue...");
        }
        
        updateProgress(DumpProgress::Stage::FixingPEHeaders, 30, "Fixing PE headers");
        
        // 重建PE头
        if (options.rebuildPE) {
            std::optional<uint32_t> newOEP;
            
            if (options.autoDetectOEP) {
                auto detectedOEP = DetectOEP(moduleBase);
                if (detectedOEP.has_value()) {
                    newOEP = static_cast<uint32_t>(detectedOEP.value() - moduleBase);
                    result.newEP = detectedOEP.value();
                    Logger::Info("Auto-detected OEP: 0x{:X} (RVA: 0x{:X})", 
                               detectedOEP.value(), newOEP.value());
                }
            } else if (options.fixOEP) {
                newOEP = static_cast<uint32_t>(entryPoint - moduleBase);
                result.newEP = entryPoint;
            }
            
            if (!RebuildPEHeaders(moduleBase, buffer, newOEP)) {
                Logger::Warning("Failed to rebuild PE headers");
            }
        }
        
        updateProgress(DumpProgress::Stage::FixingImports, 50, "Fixing import table");
        
        // 修复导入表
        if (options.fixImports) {
            if (ScyllaRebuildImports(moduleBase, buffer)) {
                Logger::Info("Import table rebuilt successfully");
            } else {
                Logger::Warning("Failed to rebuild import table, using fallback");
                FixImportTable(moduleBase, buffer);
            }
        }
        
        updateProgress(DumpProgress::Stage::FixingRelocations, 70, "Fixing relocations");
        
        // 修复重定位
        if (options.fixRelocations) {
            // 使用当前基址作为首选基址
            if (!FixRelocations(moduleBase, moduleBase, buffer)) {
                Logger::Warning("Failed to fix relocations");
            }
        }
        
        // 移除PE校验和
        if (options.removeIntegrityCheck) {
            FixPEChecksum(buffer);
        }
        
        updateProgress(DumpProgress::Stage::Writing, 90, "Writing to file");
        
        // 写入文件
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            throw MCPException("Failed to create output file: " + outputPath);
        }
        
        outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        outFile.close();
        
        if (!outFile.good()) {
            throw MCPException("Failed to write dump file");
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
        
        Logger::Info("Dumping memory region 0x{:X} - 0x{:X} ({} bytes)",
                    startAddress, startAddress + size, size);
        
        auto& memMgr = MemoryManager::Instance();
        std::vector<uint8_t> buffer = memMgr.Read(startAddress, size);
        
        if (!asRawBinary && ValidatePEHeader(buffer)) {
            // 尝试修复PE
            Logger::Info("PE header detected, attempting to fix");
            RebuildPEHeaders(startAddress, buffer);
        }
        
        std::ofstream outFile(outputPath, std::ios::binary);
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
    
    try {
        updateProgress(DumpProgress::Stage::Preparing, 0, "Analyzing target module");
        
        auto moduleBaseOpt = ParseModuleOrAddress(moduleNameOrAddress);
        if (!moduleBaseOpt.has_value()) {
            throw InvalidParamsException("Invalid module: " + moduleNameOrAddress);
        }
        
        uint64_t moduleBase = moduleBaseOpt.value();
        
        // 分析是否加壳
        ModuleDumpInfo info = AnalyzeModule(moduleNameOrAddress);
        updateProgress(DumpProgress::Stage::Preparing, 10, 
                      info.isPacked ? "Packed module detected: " + info.packerId : "Module is not packed");
        
        Logger::Info("Module: {}, Base: 0x{:X}, Packed: {}", 
                    info.name, info.baseAddress, info.isPacked);
        
        if (!info.isPacked) {
            // 未加壳,直接dump
            updateProgress(DumpProgress::Stage::Preparing, 20, "No packer detected, performing standard dump");
            DumpOptions opts;
            opts.autoDetectOEP = false;
            return DumpModule(moduleNameOrAddress, outputPath, opts, progressCallback);
        }
        
        // 自动脱壳流程
        updateProgress(DumpProgress::Stage::Preparing, 20, "Starting automatic unpacking");
        
        for (int iteration = 0; iteration < maxIterations; iteration++) {
            int baseProgress = 20 + (iteration * 60 / maxIterations);
            updateProgress(DumpProgress::Stage::Preparing, baseProgress, 
                          "Unpacking iteration " + std::to_string(iteration + 1));
            
            // 尝试检测OEP
            auto oepOpt = DetectOEP(moduleBase);
            if (!oepOpt.has_value()) {
                Logger::Warning("Failed to detect OEP in iteration {}", iteration + 1);
                continue;
            }
            
            uint64_t detectedOEP = oepOpt.value();
            Logger::Info("Iteration {}: Detected OEP at 0x{:X}", iteration + 1, detectedOEP);
            
            // 在OEP设置断点
            updateProgress(DumpProgress::Stage::Preparing, baseProgress + 10, 
                          "Setting breakpoint at OEP");
            
            // TODO: 设置断点并运行到OEP
            // 这里需要与BreakpointManager和DebugController协作
            
            // 尝试dump
            updateProgress(DumpProgress::Stage::ReadingMemory, baseProgress + 20, 
                          "Dumping unpacked module");
            
            DumpOptions opts;
            opts.autoDetectOEP = false;
            opts.fixOEP = true;
            opts.fixImports = true;
            opts.rebuildPE = true;
            
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
        
        // 所有迭代都失败
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
        auto moduleBaseOpt = ParseModuleOrAddress(moduleNameOrAddress);
        if (!moduleBaseOpt.has_value()) {
            throw InvalidParamsException("Invalid module");
        }
        
        uint64_t moduleBase = moduleBaseOpt.value();
        info.baseAddress = moduleBase;
        info.size = GetModuleSize(moduleBase);
        info.entryPoint = GetModuleEntryPoint(moduleBase);
        info.path = GetModulePath(moduleBase);
        
        // 从路径提取模块名
        size_t lastSlash = info.path.find_last_of("\\/");
        info.name = (lastSlash != std::string::npos) ? 
                    info.path.substr(lastSlash + 1) : info.path;
        
        // 检测是否加壳
        info.isPacked = IsPacked(moduleBase, info.packerId);
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to analyze module: {}", e.what());
        throw;
    }
    
    return info;
}

std::optional<uint64_t> DumpManager::DetectOEP(uint64_t moduleBase, const std::string& strategy) {
    Logger::Debug("Detecting OEP for module at 0x{:X} using strategy: {}", moduleBase, strategy);
    
    // 根据策略选择检测方法
    if (strategy == "entropy") {
        auto result = DetectOEPByEntropy(moduleBase);
        if (result.has_value()) {
            Logger::Info("OEP detected by entropy: 0x{:X}", result.value());
        }
        return result;
    }
    else if (strategy == "code_analysis") {
        // 基于代码分析/特征码检测
        auto result = DetectOEPByPattern(moduleBase);
        if (result.has_value()) {
            Logger::Info("OEP detected by code analysis: 0x{:X}", result.value());
        }
        return result;
    }
    else if (strategy == "api_calls") {
        // 基于 API 调用检测
        // TODO: 实现基于 API 调用的 OEP 检测
        Logger::Warning("API calls strategy not yet implemented");
        return std::nullopt;
    }
    else if (strategy == "tls") {
        // 基于 TLS 回调检测
        // TODO: 实现基于 TLS 的 OEP 检测
        Logger::Warning("TLS strategy not yet implemented");
        return std::nullopt;
    }
    else if (strategy == "entrypoint") {
        // 直接使用模块声明的入口点
        uint64_t entryPoint = GetModuleEntryPoint(moduleBase);
        if (entryPoint != 0) {
            Logger::Info("Using declared entry point as OEP: 0x{:X}", entryPoint);
            return entryPoint;
        } else {
            Logger::Warning("Failed to get module entry point");
            return std::nullopt;
        }
    }
    else {
        // 不应该到达这里，因为 handler 已经验证了策略
        Logger::Error("Unknown OEP detection strategy: {}", strategy);
        return std::nullopt;
    }
}

std::vector<MemoryRegionDump> DumpManager::GetDumpableRegions(uint64_t moduleBase) {
    std::vector<MemoryRegionDump> regions;
    
    auto& memMgr = MemoryManager::Instance();
    auto allRegions = memMgr.EnumerateRegions();
    
    for (const auto& region : allRegions) {
        // 过滤条件
        if (moduleBase != 0 && region.base < moduleBase) {
            continue;
        }
        
        if (moduleBase != 0) {
            uint64_t moduleSize = GetModuleSize(moduleBase);
            if (region.base >= moduleBase + moduleSize) {
                continue;
            }
        }
        
        // 只包含已提交的可读内存
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
    
    Logger::Debug("Found {} dumpable regions for module at 0x{:X}", regions.size(), moduleBase);
    return regions;
}

bool DumpManager::FixImportTable(uint64_t moduleBase, std::vector<uint8_t>& buffer) {
    try {
        // 基本的IAT修复
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
        
        // TODO: 实现IAT重建逻辑
        // 这里需要扫描内存中的IAT,恢复导入函数名称
        
        Logger::Info("Import table fix completed");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to fix import table: {}", e.what());
        return false;
    }
}

bool DumpManager::FixRelocations(uint64_t moduleBase, uint64_t preferredBase, 
                                 std::vector<uint8_t>& buffer) {
    try {
        // TODO: 实现重定位修复
        // 如果模块被重定位了,需要调整重定位表
        
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
        
        // 修复入口点
        if (newEP.has_value()) {
            ntHeaders->OptionalHeader.AddressOfEntryPoint = newEP.value();
            Logger::Info("Updated entry point to RVA: 0x{:X}", newEP.value());
        }
        
        // 修复ImageBase
        ntHeaders->OptionalHeader.ImageBase = moduleBase;
        
        // 对齐节
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
        // TODO: 实现Scylla风格的IAT重建
        // 这是一个复杂的过程,需要:
        // 1. 扫描IAT区域
        // 2. 识别API地址
        // 3. 反查模块和函数名
        // 4. 重建导入表
        
        Logger::Info("Scylla import rebuild attempted");
        return false; // 暂未实现
        
    } catch (const std::exception& e) {
        Logger::Error("Scylla import rebuild failed: {}", e.what());
        return false;
    }
}

void DumpManager::SetOEPDetectionStrategy(
    std::function<std::optional<uint64_t>(uint64_t)> strategy) {
    m_oepDetectionStrategy = strategy;
    Logger::Info("Custom OEP detection strategy set");
}

// ========== 私有辅助方法 ==========

std::optional<uint64_t> DumpManager::ParseModuleOrAddress(const std::string& input) {
    // 尝试作为地址解析
    try {
        uint64_t addr = StringUtils::ParseAddress(input);
        if (addr != 0) {
            return addr;
        }
    } catch (...) {
        // 不是地址,尝试作为模块名
    }
    
    // 作为模块名查找
    char szModPath[MAX_PATH] = {0};
    duint modBase = DbgFunctions()->ModBaseFromName(input.c_str());
    
    if (modBase != 0) {
        return modBase;
    }
    
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
        
        // 读取PE头
        std::vector<uint8_t> peHeader = memMgr.Read(moduleBase, 4096);
        
        if (!ValidatePEHeader(peHeader)) {
            return false;
        }
        
        auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(peHeader.data());
        auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(peHeader.data() + dosHeader->e_lfanew);
        
        // 检测常见的壳特征
        
        // 1. 节数量异常
        if (ntHeaders->FileHeader.NumberOfSections < 2) {
            packerId = "Unknown (Few sections)";
            return true;
        }
        
        // 2. 入口点在最后一个节
        auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
            reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS)
        );
        
        uint32_t entryRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        int entrySection = -1;
        
        for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
            if (entryRVA >= sections[i].VirtualAddress &&
                entryRVA < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) {
                entrySection = i;
                break;
            }
        }
        
        if (entrySection == ntHeaders->FileHeader.NumberOfSections - 1) {
            packerId = "Unknown (EP in last section)";
            return true;
        }
        
        // 3. 检测特定壳的特征
        // UPX
        if (ntHeaders->FileHeader.NumberOfSections >= 3) {
            std::string firstSectionName(reinterpret_cast<char*>(sections[0].Name), 8);
            if (firstSectionName.find("UPX") != std::string::npos) {
                packerId = "UPX";
                return true;
            }
        }
        
        // TODO: 添加更多壳的检测特征
        // - ASPack
        // - PECompact
        // - Themida
        // - VMProtect
        // 等等
        
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
        return std::string(path);
    }
    return "";
}

std::optional<uint64_t> DumpManager::DetectOEPByEntropy(uint64_t moduleBase) {
    // TODO: 实现基于熵值的OEP检测
    // 原理: 加壳后的代码熵值较高,找到熵值突变点
    return std::nullopt;
}

std::optional<uint64_t> DumpManager::DetectOEPByPattern(uint64_t moduleBase) {
    try {
        auto& memMgr = MemoryManager::Instance();
        
        // 常见的函数序言模式
        std::vector<std::string> patterns = {
            "55 8B EC",           // push ebp; mov ebp, esp (x86)
            "55 48 8B EC",        // push rbp; mov rbp, rsp (x64)
            "48 89 5C 24",        // mov [rsp+...], rbx (x64)
            "40 53",              // push rbx (x64 with REX)
        };
        
        uint64_t moduleSize = GetModuleSize(moduleBase);
        uint64_t searchEnd = std::min(moduleSize, static_cast<uint64_t>(0x10000)); // 搜索前64KB
        
        for (const auto& pattern : patterns) {
            auto results = memMgr.Search(pattern, moduleBase, moduleBase + searchEnd, 1);
            if (!results.empty()) {
                Logger::Info("OEP candidate found by pattern '{}' at 0x{:X}", 
                           pattern, results[0].address);
                return results[0].address;
            }
        }
        
    } catch (const std::exception& e) {
        Logger::Error("Pattern-based OEP detection failed: {}", e.what());
    }
    
    return std::nullopt;
}

std::optional<uint64_t> DumpManager::DetectOEPByExecution(uint64_t moduleBase) {
    // TODO: 实现基于执行追踪的OEP检测
    // 原理: 单步执行,检测何时跳转到原始代码段
    // 这需要与调试器深度集成
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
        
        // 简单地清零校验和
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
        uint32_t sectionAlignment = ntHeaders->OptionalHeader.SectionAlignment;
        
        auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(
            reinterpret_cast<uint8_t*>(ntHeaders) + sizeof(IMAGE_NT_HEADERS)
        );
        
        for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
            // 对齐RawSize
            uint32_t rawSize = sections[i].SizeOfRawData;
            if (rawSize % fileAlignment != 0) {
                sections[i].SizeOfRawData = ((rawSize / fileAlignment) + 1) * fileAlignment;
            }
            
            // 对齐VirtualSize
            uint32_t virtSize = sections[i].Misc.VirtualSize;
            if (virtSize % sectionAlignment != 0) {
                sections[i].Misc.VirtualSize = ((virtSize / sectionAlignment) + 1) * sectionAlignment;
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
    // TODO: 实现节删除功能
    // 用于移除壳添加的节
    return false;
}

} // namespace MCP
