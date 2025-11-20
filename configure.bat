@echo off
REM x64dbg-mcp CMake Configuration Script
REM Usage: configure.bat [SDK_PATH]

setlocal

REM Check if SDK path is provided as argument
if "%~1" NEQ "" (
    set X64DBG_SDK=%~1
) else (
    REM Prompt user for SDK path
    set /p X64DBG_SDK="Enter x64dbg SDK path (e.g., C:\x64dbg\pluginsdk): "
)

REM Set vcpkg path (use environment variable or prompt)
if not defined VCPKG_ROOT (
    set /p VCPKG_ROOT="Enter vcpkg root path (e.g., C:\vcpkg): "
)

REM Verify SDK path exists
if not exist "%X64DBG_SDK%" (
    echo ERROR: SDK path does not exist: %X64DBG_SDK%
    echo Please check the path and try again.
    exit /b 1
)

REM Clean old build directory
if exist build (
    echo Cleaning old build directory...
    rmdir /s /q build
)

REM Run CMake configuration
echo Configuring project...
echo SDK Path: %X64DBG_SDK%
echo vcpkg: %VCPKG_ROOT%
echo.

cmake -B build ^
    -DX64DBG_SDK_DIR="%X64DBG_SDK%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Configuration failed!
    exit /b 1
)

echo.
echo Configuration successful!
echo.
echo Next steps:
echo   1. Build: cmake --build build --config Release
echo   2. Or open: build\x64dbg_mcp.sln in Visual Studio
echo.

endlocal
