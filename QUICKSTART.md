# Quick Start Guide

English | [中文](docs/QUICKSTART_CN.md)

Get up and running with x64dbg MCP Server in minutes.

## Prerequisites

- Windows 10/11 (x64)
- x64dbg debugger installed
- Visual Studio 2022 with C++ Desktop Development workload
- CMake 3.15+
- vcpkg package manager

## Installation

### Option 1: Pre-built Release (Recommended)

1. Download the latest release from [GitHub Releases](https://github.com/SetsunaYukiOvO/x64dbg-mcp/releases)
2. Extract `x64dbg_mcp.dp64` to your x64dbg plugins directory
3. Copy `config.json` to `plugins/x64dbg-mcp/`
4. Restart x64dbg

### Option 2: Build from Source

#### 1. Install vcpkg

```powershell
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg

# Bootstrap vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Set environment variable (optional)
setx VCPKG_ROOT "C:\vcpkg"
```

#### 2. Build the Plugin

```powershell
# Clone repository
git clone https://github.com/SetsunaYukiOvO/x64dbg-mcp.git
cd x64dbg-mcp

# Build using the build script (recommended)
.\build.bat

# Or build with specific options
.\build.bat --clean          # Clean build
.\build.bat --debug          # Debug build
.\build.bat --help           # Show all options
```

The build script will:
- Automatically detect vcpkg installation
- Download and compile dependencies (nlohmann_json)
- Build the plugin using Visual Studio
- Optionally install to x64dbg plugins directory

#### 3. Manual Build (Advanced)

```powershell
# Configure with vcpkg toolchain
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Compile
cmake --build build --config Release

# Output: build\bin\Release\x64dbg_mcp.dp64
```

## 1. Install Plugin

If you didn't use the automatic installation in the build script:

```powershell
# Copy plugin
copy build\bin\Release\x64dbg_mcp.dp64 C:\x64dbg\x64\plugins\

# Copy config
mkdir C:\x64dbg\x64\plugins\x64dbg-mcp
copy config.json C:\x64dbg\x64\plugins\x64dbg-mcp\
```

## 2. Start Server

1. Launch x64dbg
2. Load a target program for debugging
3. Menu: **Plugins → MCP Server → Start MCP HTTP Server**
4. Server starts on port 3000 (configurable in config.json)
5. Verify server is running by accessing http://127.0.0.1:3000 in a browser

## 3. Connect Client

### Python Example

```python
import requests
import json

class MCPClient:
    def __init__(self, host='127.0.0.1', port=3000):
        self.base_url = f"http://{host}:{port}"
        self.request_id = 1
    
    def call(self, method, params=None):
        request = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {}
        }
        self.request_id += 1
        
        response = requests.post(
            f"{self.base_url}/rpc",
            json=request,
            headers={"Content-Type": "application/json"}
        )
        return response.json()

# Use the client
client = MCPClient()
print(client.call("initialize"))
print(client.call("tools/list"))
```

### MCP Protocol Example

```python
# Initialize MCP session
init_response = client.call("initialize", {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": {
        "name": "my-client",
        "version": "1.0.0"
    }
})

# Send initialized notification
client.call("notifications/initialized")

# List available tools
tools = client.call("tools/list")
print(tools)
```

## Common Operations

### Get System Info
```python
response = client.call("system.info")
```

### Read Register
```python
response = client.call("register.get", {"name": "rax"})
value = response["result"]["value"]
```

### Read Memory
```python
response = client.call("memory.read", {
    "address": "0x140001000",
    "size": 100
})
data = response["result"]["data"]  # hex string
```

### Set Breakpoint
```python
response = client.call("breakpoint.set", {
    "address": "0x140001000",
    "type": "software"
})
```

### Disassemble
```python
response = client.call("disassembly.at", {
    "address": "0x140001000",
    "count": 10
})
instructions = response["result"]["instructions"]
```

## Configuration

Edit `config.json` to customize:

```json
{
  "version": "1.0.1",
  "server": {
    "address": "127.0.0.1",
    "port": 3000
  },
  "permissions": {
    "allow_memory_write": true,
    "allow_register_write": true,
    "allow_script_execution": true,
    "allow_breakpoint_modification": true
  },
  "logging": {
    "enabled": true,
    "level": "info",
    "file": "x64dbg_mcp.log"
  }
}
```

## Next Steps

- See [README.md](README.md) for complete API reference
- Use `system.methods` API call to discover all available methods
- Explore [examples/](examples/) for more client implementations

## Troubleshooting

### Build Issues

**CMake cannot find vcpkg**
- Ensure `VCPKG_ROOT` environment variable is set
- Or use the full path in CMAKE_TOOLCHAIN_FILE
- Default location: `C:\vcpkg`

**Link errors during build**
- Make sure x64dbg SDK libraries exist in `include/x64dbg-pluginsdk/`
- Try clean rebuild: `.\build.bat --clean`
- Verify you're building for x64 architecture

**vcpkg not found**
- Install vcpkg: `git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg`
- Bootstrap: `C:\vcpkg\bootstrap-vcpkg.bat`

### Runtime Issues

**Plugin won't load**
- Ensure plugin file is named `x64dbg_mcp.dp64`
- Check x64dbg version (requires 64-bit version)
- View x64dbg log for error messages

**Server won't start**
- Check port 3000 is not in use
- Verify config.json is valid JSON
- Ensure you have a program loaded in x64dbg
- Check x64dbg log for detailed error messages

**Connection refused**  
- Ensure HTTP server is started via plugin menu ("Start MCP HTTP Server")
- Check firewall settings for port 3000
- Verify client connects to http://127.0.0.1:3000
- Test with browser: http://127.0.0.1:3000

## Build Script Options

The `build.bat` script supports the following options:

```powershell
build.bat [OPTIONS]

Options:
  --debug         Build in Debug mode (default: Release)
  --clean         Clean build directory before building
  --help          Show help message

Examples:
  build.bat                    # Release build
  build.bat --debug            # Debug build
  build.bat --clean            # Clean and rebuild
  build.bat --clean --debug    # Clean debug build
```

## Development Tips

### Quick Rebuild Cycle

```powershell
# Make code changes...

# Rebuild (faster, incremental)
.\build.bat

# x64dbg must be restarted to reload the plugin
```

### Debug Build for Development

```powershell
# Build with debug symbols
.\build.bat --debug

# Debug output: build\bin\Debug\x64dbg_mcp.dp64
```

## Next Steps
