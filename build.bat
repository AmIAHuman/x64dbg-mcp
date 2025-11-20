@echo off
REM x64dbg MCP Server - Build Script
REM ===================================

setlocal enabledelayedexpansion

echo.
echo ========================================
echo x64dbg MCP Server - Build Script
echo ========================================
echo.

REM 检查参数
set "BUILD_TYPE=Release"
set "CLEAN_BUILD=NO"
set "GENERATOR=Visual Studio 17 2022"

:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="--debug" set "BUILD_TYPE=Debug"
if /i "%~1"=="--clean" set "CLEAN_BUILD=YES"
if /i "%~1"=="--help" goto :show_help
shift
goto :parse_args
:end_parse

REM 显示配置
echo Build Configuration:
echo   Build Type: %BUILD_TYPE%
echo   Generator: %GENERATOR%
echo   Clean Build: %CLEAN_BUILD%
echo.

REM 检查 CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found. Please install CMake.
    exit /b 1
)

REM 检查 vcpkg
if not defined VCPKG_ROOT (
    echo [WARNING] VCPKG_ROOT not set, trying default location...
    if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=C:\vcpkg"
        echo [OK] Found vcpkg at C:\vcpkg
    ) else (
        echo [ERROR] vcpkg not found. Please install vcpkg or set VCPKG_ROOT environment variable.
        echo.
        echo Install vcpkg:
        echo   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
        echo   C:\vcpkg\bootstrap-vcpkg.bat
        echo.
        exit /b 1
    )
)

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not exist "%VCPKG_TOOLCHAIN%" (
    echo [ERROR] vcpkg toolchain file not found at: %VCPKG_TOOLCHAIN%
    exit /b 1
)

echo [OK] Using vcpkg toolchain: %VCPKG_TOOLCHAIN%
echo.

REM 清理构建目录
if "%CLEAN_BUILD%"=="YES" (
    echo [1/4] Cleaning build directory...
    if exist build (
        echo Removing build directory...
        rd /s /q build 2>nul
        timeout /t 1 /nobreak >nul
    )
    echo.
)

REM 配置 CMake
echo [2/4] Configuring CMake...
echo.

cmake -B build -G "%GENERATOR%" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed
    exit /b 1
)

REM 编译
echo.
echo [3/4] Building...
echo.

cmake --build build --config %BUILD_TYPE% -- /m

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed
    exit /b 1
)

REM 检查输出
set "OUTPUT_FILE=build\bin\%BUILD_TYPE%\x64dbg_mcp.dp64"
if not exist "%OUTPUT_FILE%" (
    echo.
    echo [ERROR] Output file not found: %OUTPUT_FILE%
    exit /b 1
)

REM 编译
echo.
echo [3/4] Building...
echo.

cmake --build build --config %BUILD_TYPE% -- /m

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed
    exit /b 1
)

REM 检查输出
set "OUTPUT_FILE=build\bin\%BUILD_TYPE%\x64dbg_mcp.dp64"
set "MCP_SERVER_EXE=build\bin\%BUILD_TYPE%\x64dbg-mcp-server.exe"

if not exist "%OUTPUT_FILE%" (
    echo.
    echo [ERROR] Output file not found: %OUTPUT_FILE%
    exit /b 1
)

REM 完成
echo.
echo [4/4] Build complete!
echo.
echo Outputs:
echo   Plugin: %OUTPUT_FILE%
for %%F in ("%OUTPUT_FILE%") do echo   Size: %%~zF bytes

if exist "%MCP_SERVER_EXE%" (
    echo   MCP Server: %MCP_SERVER_EXE%
    for %%F in ("%MCP_SERVER_EXE%") do echo   Size: %%~zF bytes
) else (
    echo   MCP Server: Not built (CMake build may not include it)
)
echo.

REM 检查 x64dbg 安装
set "X64DBG_DIR=C:\x64dbg"
set "X64DBG_FOUND=NO"

if exist "%X64DBG_DIR%\x64\x64dbg.exe" (
    set "X64DBG_FOUND=YES"
    set "X64DBG_PLUGINS=%X64DBG_DIR%\x64\plugins"
) else if exist "%X64DBG_DIR%\release\x64\x64dbg.exe" (
    set "X64DBG_FOUND=YES"
    set "X64DBG_PLUGINS=%X64DBG_DIR%\release\x64\plugins"
) else if exist "%X64DBG_DIR%\x64dbg.exe" (
    set "X64DBG_FOUND=YES"
    set "X64DBG_PLUGINS=%X64DBG_DIR%\plugins"
)

if "%X64DBG_FOUND%"=="YES" (
    echo Found x64dbg at: %X64DBG_DIR%
    echo Plugin directory: %X64DBG_PLUGINS%
    echo.
    
    choice /C YN /M "Do you want to install the plugin to x64dbg"
    if !errorlevel!==1 (
        echo.
        echo Installing plugin...
        
        if not exist "%X64DBG_PLUGINS%" mkdir "%X64DBG_PLUGINS%"
        if not exist "%X64DBG_PLUGINS%\x64dbg-mcp" mkdir "%X64DBG_PLUGINS%\x64dbg-mcp"
        
        copy /Y "%OUTPUT_FILE%" "%X64DBG_PLUGINS%\" >nul
        if errorlevel 1 (
            echo [ERROR] Failed to copy plugin file
            exit /b 1
        )
        
        copy /Y "config.json" "%X64DBG_PLUGINS%\x64dbg-mcp\" >nul
        if errorlevel 1 (
            echo [WARNING] Failed to copy config file
        )
        
        echo.
        echo [OK] Plugin installed successfully!
        echo   Plugin: %X64DBG_PLUGINS%\x64dbg_mcp.dp64
        echo   Config: %X64DBG_PLUGINS%\x64dbg-mcp\config.json
        echo.
        echo To use the plugin:
        echo   1. Start x64dbg
        echo   2. Load a target program
        echo   3. Go to: Plugins ^> x64dbg MCP Server ^> Start Server
        echo   4. Connect to 127.0.0.1:5555 via JSON-RPC client
    )
) else (
    echo [INFO] x64dbg not found at default location: %X64DBG_DIR%
    echo You can manually copy the plugin file to x64dbg plugins directory.
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.

goto :eof

:show_help
echo Usage: build.bat [OPTIONS]
echo.
echo Options:
echo   --debug         Build in Debug mode (default: Release)
echo   --clean         Clean build directory before building
echo   --help          Show this help message
echo.
echo Environment Variables:
echo   VCPKG_ROOT      Path to vcpkg installation (default: C:\vcpkg)
echo.
echo Examples:
echo   build.bat
echo   build.bat --debug
echo   build.bat --clean
echo   build.bat --clean --debug
echo.
echo Requirements:
echo   - CMake 3.15 or higher
echo   - Visual Studio 2022 with C++ Desktop Development workload
echo   - vcpkg (for dependencies)
echo.
echo The script will:
echo   1. Check for vcpkg installation
echo   2. Configure CMake with vcpkg toolchain
echo   3. Build the project using MSBuild
echo   4. Optionally install to x64dbg plugins directory
echo.
exit /b 0
