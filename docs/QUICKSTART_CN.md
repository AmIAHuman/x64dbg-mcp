# 快速入门指南

[English](../QUICKSTART.md) | 中文

几分钟内开始使用 x64dbg MCP 服务器。

## 前置要求

- Windows 10/11 (x64)
- 已安装 x64dbg 调试器
- Visual Studio 2022 with C++ Desktop Development 工作负载
- CMake 3.15+
- vcpkg 包管理器

## 安装

### 方式一：使用预编译版本（推荐）

1. 从 [GitHub Releases](https://github.com/SetsunaYukiOvO/x64dbg-mcp/releases) 下载最新版本
2. 解压 `x64dbg_mcp.dp64` 到 x64dbg 插件目录
3. 复制 `config.json` 到 `plugins/x64dbg-mcp/`
4. 重启 x64dbg

### 方式二：从源码构建

#### 1. 安装 vcpkg

```powershell
# 克隆 vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg

# 引导 vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# 设置环境变量（可选）
setx VCPKG_ROOT "C:\vcpkg"
```

#### 2. 构建插件

```powershell
# 克隆仓库
git clone https://github.com/SetsunaYukiOvO/x64dbg-mcp.git
cd x64dbg-mcp

# 使用构建脚本（推荐）
.\build.bat

# 或使用特定选项构建
.\build.bat --clean          # 清理构建
.\build.bat --debug          # 调试构建
.\build.bat --help           # 显示所有选项
```

构建脚本将：
- 自动检测 vcpkg 安装
- 下载并编译依赖项（nlohmann_json）
- 使用 Visual Studio 构建插件
- 可选择自动安装到 x64dbg 插件目录

#### 3. 手动构建（高级）

```powershell
# 使用 vcpkg 工具链配置
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# 编译
cmake --build build --config Release

# 输出：build\bin\Release\x64dbg_mcp.dp64
```

## 1. 安装插件

如果你没有使用构建脚本的自动安装：

```powershell
# 复制插件
copy build\bin\Release\x64dbg_mcp.dp64 C:\x64dbg\x64\plugins\

# 复制配置文件
mkdir C:\x64dbg\x64\plugins\x64dbg-mcp
copy config.json C:\x64dbg\x64\plugins\x64dbg-mcp\
```

## 2. 启动服务器

1. 启动 x64dbg
2. 加载目标程序进行调试
3. 菜单：**插件 → MCP Server → Start MCP HTTP Server**
4. 服务器在端口 3000 上启动（可在 config.json 中配置）
5. 在浏览器中访问 http://127.0.0.1:3000 验证服务器运行

## 3. 连接客户端

### Python 示例

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

# 使用客户端
client = MCPClient()
print(client.call("initialize"))
print(client.call("tools/list"))
```

### MCP 协议示例

```python
# 初始化 MCP 会话
init_response = client.call("initialize", {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": {
        "name": "my-client",
        "version": "1.0.0"
    }
})

# 发送 initialized 通知
client.call("notifications/initialized")

# 列出可用工具
tools = client.call("tools/list")
print(tools)
```

## 常用操作

### 获取系统信息
```python
response = client.call("system.info")
```

### 读取寄存器
```python
response = client.call("register.get", {"name": "rax"})
value = response["result"]["value"]
```

### 读取内存
```python
response = client.call("memory.read", {
    "address": "0x140001000",
    "size": 100
})
data = response["result"]["data"]  # 十六进制字符串
```

### 设置断点
```python
response = client.call("breakpoint.set", {
    "address": "0x140001000",
    "type": "software"
})
```

### 反汇编
```python
response = client.call("disassembly.at", {
    "address": "0x140001000",
    "count": 10
})
instructions = response["result"]["instructions"]
```

## 配置

编辑 `config.json` 进行自定义：

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

## 下一步

- 查看 [README_CN.md](README_CN.md) 获取完整的 API 参考
- 使用 `system.methods` API 调用来发现所有可用方法
- 探索 [examples/](../examples/) 查看更多客户端实现

## 故障排除

### 构建问题

**CMake 找不到 vcpkg**
- 确保设置了 `VCPKG_ROOT` 环境变量
- 或在 CMAKE_TOOLCHAIN_FILE 中使用完整路径
- 默认位置：`C:\vcpkg`

**构建时出现链接错误**
- 确保 x64dbg SDK 库文件存在于 `include/x64dbg-pluginsdk/`
- 尝试清理重建：`.\build.bat --clean`
- 验证是否为 x64 架构构建

**找不到 vcpkg**
- 安装 vcpkg：`git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg`
- 引导：`C:\vcpkg\bootstrap-vcpkg.bat`

### 运行时问题

**插件无法加载**
- 确保插件文件名为 `x64dbg_mcp.dp64`
- 检查 x64dbg 版本（需要 64 位版本）
- 查看 x64dbg 日志了解错误信息

**服务器无法启动**
- 检查端口 3000 是否未被占用
- 验证 config.json 是否为有效的 JSON
- 确保在 x64dbg 中加载了程序
- 查看 x64dbg 日志获取详细错误信息

**连接被拒绝**
- 确保通过插件菜单启动了 HTTP 服务器（"Start MCP HTTP Server"）
- 检查防火墙设置，允许端口 3000
- 验证客户端连接到 http://127.0.0.1:3000
- 在浏览器中测试：http://127.0.0.1:3000

## 构建脚本选项

`build.bat` 脚本支持以下选项：

```powershell
build.bat [选项]

选项:
  --debug         构建 Debug 模式（默认：Release）
  --clean         构建前清理构建目录
  --help          显示帮助信息

示例:
  build.bat                    # Release 构建
  build.bat --debug            # Debug 构建
  build.bat --clean            # 清理并重建
  build.bat --clean --debug    # 清理 Debug 构建
```

## 开发技巧

### 快速重建周期

```powershell
# 修改代码...

# 重建（更快，增量编译）
.\build.bat

# x64dbg 必须重启才能重新加载插件
```

### 开发用的 Debug 构建

```powershell
# 构建带调试符号的版本
.\build.bat --debug

# 调试输出：build\bin\Debug\x64dbg_mcp.dp64
```

## 下一步
