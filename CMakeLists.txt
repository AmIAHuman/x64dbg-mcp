cmake_minimum_required(VERSION 3.15)
project(x64dbg_mcp VERSION 1.0.1 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 选项
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_LOGGING "Enable logging" ON)

# 使用项目内置的 x64dbg SDK
set(X64DBG_BUNDLED_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/x64dbg-pluginsdk")

# 检查内置 SDK 是否存在
if(EXISTS "${X64DBG_BUNDLED_SDK_DIR}/_plugins.h")
    message(STATUS "Using bundled x64dbg SDK from: ${X64DBG_BUNDLED_SDK_DIR}")
    set(SDK_AVAILABLE TRUE)
else()
    message(FATAL_ERROR "Bundled x64dbg SDK not found at: ${X64DBG_BUNDLED_SDK_DIR}")
endif()

# 第三方库 - 使用 vcpkg 引入 nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# 源文件
set(CORE_SOURCES
    src/core/Exceptions.cpp
    src/core/Logger.cpp
    src/core/ConfigManager.cpp
    src/core/JSONRPCParser.cpp
    src/core/ResponseBuilder.cpp
    src/core/RequestValidator.cpp
    src/core/MethodDispatcher.cpp
    src/core/PermissionChecker.cpp
)

set(COMMUNICATION_SOURCES
    src/communication/MCPHttpServer.cpp
)

set(BUSINESS_SOURCES
    src/business/DebugController.cpp
    src/business/RegisterManager.cpp
    src/business/MemoryManager.cpp
    src/business/BreakpointManager.cpp
    src/business/DisassemblyEngine.cpp
    src/business/SymbolResolver.cpp
    src/business/StackManager.cpp
    src/business/ThreadManager.cpp
)

set(HANDLER_SOURCES
    src/handlers/DebugHandler.cpp
    src/handlers/RegisterHandler.cpp
    src/handlers/MemoryHandler.cpp
    src/handlers/BreakpointHandler.cpp
    src/handlers/DisassemblyHandler.cpp
    src/handlers/StackHandler.cpp
    src/handlers/ThreadHandler.cpp
    src/handlers/ModuleHandler.cpp
    src/handlers/EventCallbackHandler.cpp
)

set(UTILS_SOURCES
    src/utils/StringUtils.cpp
    src/utils/ThreadPool.cpp
)

set(PLUGIN_SOURCES
    src/PluginEntry.cpp
)

# 主库
add_library(x64dbg_mcp SHARED
    ${CORE_SOURCES}
    ${COMMUNICATION_SOURCES}
    ${BUSINESS_SOURCES}
    ${HANDLER_SOURCES}
    ${UTILS_SOURCES}
    ${PLUGIN_SOURCES}
)

# 包含目录
target_include_directories(x64dbg_mcp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${X64DBG_BUNDLED_SDK_DIR}
)

# 添加 SDK 定义
target_compile_definitions(x64dbg_mcp PRIVATE X64DBG_SDK_AVAILABLE)
message(STATUS "Building with bundled x64dbg SDK support")

# 链接库
target_link_libraries(x64dbg_mcp PRIVATE
    nlohmann_json::nlohmann_json
    ws2_32
    ${X64DBG_BUNDLED_SDK_DIR}/x64bridge.lib
    ${X64DBG_BUNDLED_SDK_DIR}/x64dbg.lib
)

# 编译定义
if(ENABLE_LOGGING)
    target_compile_definitions(x64dbg_mcp PRIVATE ENABLE_LOGGING)
endif()

# Windows 特定设置
if(WIN32)
    target_compile_definitions(x64dbg_mcp PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# 警告级别
if(MSVC)
    target_compile_options(x64dbg_mcp PRIVATE /W4 /utf-8)
else()
    target_compile_options(x64dbg_mcp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 导出符号
set_target_properties(x64dbg_mcp PROPERTIES
    OUTPUT_NAME "x64dbg_mcp"
    SUFFIX ".dp64"
    PREFIX ""
)

# 使用 DEF 文件导出符号
if(MSVC)
    target_sources(x64dbg_mcp PRIVATE src/plugin.def)
endif()

# 测试
if(BUILD_TESTS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Tests directory not found, skipping tests")
    endif()
endif()

# 安装
install(TARGETS x64dbg_mcp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    DESTINATION bin
)
