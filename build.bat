@echo off
setlocal

echo ========================================
echo x64dbg MCP Server - Build Script
echo ========================================
echo.

REM Check vcpkg
if not defined VCPKG_ROOT set VCPKG_ROOT=C:\vcpkg
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [ERROR] vcpkg not found at %VCPKG_ROOT%
    exit /b 1
)
set VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

echo Using vcpkg: %VCPKG_ROOT%
echo.

REM Clean
if exist build_x64 rmdir /s /q build_x64
if exist build_x86 rmdir /s /q build_x86
if exist dist rmdir /s /q dist
mkdir dist

echo ========================================
echo Building x64
echo ========================================
echo.

cmake -B build_x64 -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x64-windows -DXDBG_ARCH=x64

if errorlevel 1 (
    echo [ERROR] x64 configure failed
    goto :build_x86
)

cmake --build build_x64 --config Release -j

if errorlevel 1 (
    echo [ERROR] x64 build failed
    goto :build_x86
)

if exist "build_x64\bin\Release\x64dbg_mcp.dp64" (
    copy /Y "build_x64\bin\Release\x64dbg_mcp.dp64" "dist\" >nul
    echo [OK] x64 plugin: dist\x64dbg_mcp.dp64
)

:build_x86
echo.
echo ========================================
echo Building x86
echo ========================================
echo.

cmake -B build_x86 -G "Visual Studio 17 2022" -A Win32 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x86-windows -DXDBG_ARCH=x86

if errorlevel 1 (
    echo [ERROR] x86 configure failed
    goto :done
)

cmake --build build_x86 --config Release -j

if errorlevel 1 (
    echo [ERROR] x86 build failed
    goto :done
)

if exist "build_x86\bin\Release\x32dbg_mcp.dp32" (
    copy /Y "build_x86\bin\Release\x32dbg_mcp.dp32" "dist\" >nul
    echo [OK] x86 plugin: dist\x32dbg_mcp.dp32
)

:done
echo.
echo ========================================
echo Build Complete
echo ========================================
echo.

dir /b dist\*.dp* 2>nul

echo.
echo Plugins are in: dist\
echo.
