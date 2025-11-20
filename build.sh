#!/bin/bash
# x64dbg MCP Server - Build Script (Linux/macOS - Cross Compilation)
# ====================================================================
# Note: This is for cross-compilation only. x64dbg is Windows-only.

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 默认配置
BUILD_TYPE="Release"
CLEAN_BUILD="NO"

# 显示帮助
show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
  --debug         Build in Debug mode (default: Release)
  --clean         Clean build directory before building
  --help          Show this help message

Environment Variables:
  VCPKG_ROOT      Path to vcpkg installation

Examples:
  $0
  $0 --debug
  $0 --clean
  $0 --clean --debug

Requirements:
  - CMake 3.15 or higher
  - MinGW-w64 or Wine for cross-compilation
  - vcpkg (for dependencies)

Note:
  This script is for cross-compilation on Linux/macOS.
  x64dbg is Windows-only, so you'll need to deploy the
  plugin to a Windows system or use Wine.
EOF
    exit 0
}

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --clean)
            CLEAN_BUILD="YES"
            shift
            ;;
        --help)
            show_help
            ;;
        *)
            echo -e "${RED}[ERROR]${NC} Unknown option: $1"
            show_help
            ;;
    esac
done

echo ""
echo "========================================"
echo "x64dbg MCP Server - Build Script"
echo "========================================"
echo ""
echo -e "${YELLOW}[WARNING]${NC} This is a cross-compilation build for Windows."
echo "x64dbg only runs on Windows. You'll need to deploy the plugin to Windows."
echo ""

# 显示配置
echo "Build Configuration:"
echo "  Build Type: $BUILD_TYPE"
echo "  Clean Build: $CLEAN_BUILD"
echo ""

# 检查 CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}[ERROR]${NC} CMake not found. Please install CMake."
    exit 1
fi

# 检查 vcpkg
if [ -z "$VCPKG_ROOT" ]; then
    echo -e "${YELLOW}[WARNING]${NC} VCPKG_ROOT not set, trying default locations..."
    if [ -d "/usr/local/vcpkg" ]; then
        VCPKG_ROOT="/usr/local/vcpkg"
    elif [ -d "$HOME/vcpkg" ]; then
        VCPKG_ROOT="$HOME/vcpkg"
    else
        echo -e "${RED}[ERROR]${NC} vcpkg not found. Please install vcpkg or set VCPKG_ROOT."
        echo ""
        echo "Install vcpkg:"
        echo "  git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg"
        echo "  ~/vcpkg/bootstrap-vcpkg.sh"
        echo "  export VCPKG_ROOT=~/vcpkg"
        echo ""
        exit 1
    fi
fi

VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
if [ ! -f "$VCPKG_TOOLCHAIN" ]; then
    echo -e "${RED}[ERROR]${NC} vcpkg toolchain file not found at: $VCPKG_TOOLCHAIN"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Using vcpkg toolchain: $VCPKG_TOOLCHAIN"
echo ""

# 清理构建目录
if [ "$CLEAN_BUILD" == "YES" ]; then
    echo "[1/4] Cleaning build directory..."
    if [ -d "build" ]; then
        echo "Removing build directory..."
        rm -rf build
    fi
    echo ""
fi

# 配置 CMake
echo ""
echo "[2/4] Configuring CMake..."
echo ""

cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE

# 编译
echo ""
echo "[3/4] Building..."
echo ""

cmake --build build --config $BUILD_TYPE -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 检查输出
OUTPUT_FILE="build/bin/$BUILD_TYPE/x64dbg_mcp.dp64"
if [ ! -f "$OUTPUT_FILE" ]; then
    echo ""
    echo -e "${RED}[ERROR]${NC} Output file not found: $OUTPUT_FILE"
    exit 1
fi

# 编译 MCP 独立服务器 (仅限 Windows，跳过)
echo ""
echo "[4/5] Building MCP standalone server..."
echo ""

MCP_SERVER_SRC="standalone/mcp_server_standalone.cpp"
MCP_SERVER_EXE="standalone/x64dbg-mcp-server.exe"

if [ -f "$MCP_SERVER_SRC" ]; then
    # 检查是否有 MinGW 或交叉编译器
    if command -v x86_64-w64-mingw32-g++ &> /dev/null; then
        echo "Cross-compiling MCP server with MinGW..."
        x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o "$MCP_SERVER_EXE" "$MCP_SERVER_SRC" -lws2_32
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}[OK]${NC} MCP server built successfully"
            echo "  Output: $MCP_SERVER_EXE"
            FILE_SIZE=$(stat -c%s "$MCP_SERVER_EXE" 2>/dev/null || echo "unknown")
            echo "  Size: $FILE_SIZE bytes"
        else
            echo -e "${YELLOW}[WARNING]${NC} MCP server build failed"
        fi
    else
        echo -e "${YELLOW}[INFO]${NC} MinGW not found, skipping MCP server build"
        echo "  MCP server is Windows-only, compile it on Windows or install MinGW:"
        echo "  sudo apt-get install mingw-w64  # Ubuntu/Debian"
        echo "  brew install mingw-w64          # macOS"
    fi
else
    echo -e "${YELLOW}[INFO]${NC} MCP server source not found, skipping"
fi

# 完成
echo ""
echo "[5/5] Build complete!"
echo ""
echo "Outputs:"
echo "  Plugin: $OUTPUT_FILE"
FILE_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null || echo "unknown")
echo "  Size: $FILE_SIZE bytes"

if [ -f "$MCP_SERVER_EXE" ]; then
    echo "  MCP Server: $MCP_SERVER_EXE"
    FILE_SIZE=$(stat -f%z "$MCP_SERVER_EXE" 2>/dev/null || stat -c%s "$MCP_SERVER_EXE" 2>/dev/null || echo "unknown")
    echo "  Size: $FILE_SIZE bytes"
fi
echo ""
echo ""
echo "========================================"
echo -e "${GREEN}Build completed successfully!${NC}"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Copy $OUTPUT_FILE to Windows system"
echo "  2. Place in x64dbg plugins directory (e.g., C:\\x64dbg\\x64\\plugins\\)"
echo "  3. Copy config.json to plugins\\x64dbg-mcp\\"
echo "  4. Start x64dbg and load the plugin"
echo ""
