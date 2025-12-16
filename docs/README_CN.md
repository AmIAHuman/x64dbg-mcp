# x64dbg MCP 服务器插件

[English](../README.md) | 中文

一个为 x64dbg 和 x32dbg 实现的模型上下文协议（MCP）服务器插件，通过 JSON-RPC 2.0 接口实现远程调试。该插件允许外部应用程序和 AI 代理以编程方式与调试器交互。

**现已支持 x64 和 x86 双架构!**

## 功能特性

- **JSON-RPC 2.0 协议**：标准的、与语言无关的接口
- **HTTP + SSE 通信**：基于 Web 的现代化集成方式，使用服务器发送事件
- **MCP 协议支持**：兼容模型上下文协议，支持 AI 代理集成
- **全面的调试 API（69+ 方法）**：
  - 执行控制（运行、暂停、单步、运行到指定地址）
  - 内存读取/写入/搜索/分配
  - 寄存器访问（50+ 寄存器，包括 GPR、SSE、AVX）
  - 断点管理（软件断点、硬件断点、内存断点、条件断点、日志断点）
  - 反汇编和符号解析
  - 线程管理（列表、切换、挂起、恢复）
  - 调用栈跟踪和分析
  - **Dump与脱壳**（模块dump、内存dump、自动脱壳、OEP检测、IAT重建）
  - **脚本执行**（执行 x64dbg 命令、批量操作）
  - **上下文快照**（捕获和比较调试状态）
  - 通过 SSE 实现事件通知
- **安全性**：基于权限的访问控制
- **可扩展**：支持自定义方法的插件架构

## v1.0.2 新特性

- 🐛 **缺陷修复**：修复了自动化测试发现的关键问题
  - 修复 `breakpoint_toggle` 状态一致性问题
  - 实现了真正的 `memory_search` 搜索功能
  - 修复 `memory_get_info` 返回正确的内存区域基址
  - 修复 `debug_step_over` RIP 同步时序问题
  - 增强 `dump_detect_oep` 策略验证，提供清晰的错误消息
  - 添加缺失的诊断字段（`error`、`encoding`、`progress`）

- 🔧 **构建系统改进**
  - 双架构构建脚本：一次命令编译 x64 和 x86 两个版本
  - 统一输出目录（`dist/`），方便管理
  - 使用 `-j` 标志实现更快的并行编译
  - 简化的构建选项：`--clean`、`--x64-only`、`--x86-only`

- 📚 **文档清理**
  - 删除冗余的技术文档
  - 精简核心文档

## 历史版本

### v1.0.1

- 线程和调用栈管理 API
- 增强的错误处理和日志系统

完整的版本历史请参见 [CHANGELOG_CN.md](../CHANGELOG_CN.md)

## 从源码构建

### 环境要求

- **Windows 10/11** (x64)
- **CMake** 3.15 或更高版本
- **Visual Studio 2022** 及 C++ 桌面开发工作负载
- **vcpkg** - C++ 包管理器
- **Git** - 用于克隆仓库

### 快速构建

使用提供的构建脚本最为简便：

```powershell
# 克隆仓库
git clone https://github.com/SetsunaYukiOvO/x64dbg-mcp.git
cd x64dbg-mcp

# 同时构建 x64 和 x86 两个版本（推荐）
.\build.bat

# 仅构建 x64 版本
.\build.bat --x64-only

# 仅构建 x86 版本
.\build.bat --x86-only

# 清理重新构建
.\build.bat --clean

# 脚本将自动：
# 1. 检测 vcpkg 安装
# 2. 下载依赖项 (nlohmann_json)
# 3. 为两个架构配置 CMake
# 4. 使用 Visual Studio 并行编译
# 5. 将输出文件复制到 dist/ 目录
```

构建脚本选项：
```powershell
.\build.bat               # 构建 x64 和 x86 两个版本（Release）
.\build.bat --clean       # 清理后重新构建两个版本
.\build.bat --x64-only    # 仅构建 x64 版本
.\build.bat --x86-only    # 仅构建 x86 版本
.\build.bat --debug       # Debug 构建（未来支持）
.\build.bat --help        # 显示所有选项
```

**输出文件**（位于 `dist/` 目录）：
- x64 插件：`dist\x64dbg_mcp.dp64`（约 837 KB）
- x86 插件：`dist\x32dbg_mcp.dp32`（约 800 KB）

### 手动构建步骤

如果您更喜欢手动控制：

1. **安装 vcpkg**（如果尚未安装）：
```powershell
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT "C:\vcpkg"
```

2. **克隆仓库**：
```powershell
git clone https://github.com/SetsunaYukiOvO/x64dbg-mcp.git
cd x64dbg-mcp
```

3. **配置 CMake**：
```powershell
# x64 构建
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DXDBG_ARCH=x64

# x86 构建
cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DXDBG_ARCH=x86
```

4. **构建**：
```powershell
cmake --build build --config Release
```

5. **输出文件**：
- 插件文件：`build\bin\Release\x64dbg_mcp.dp64`（约 611 KB）

## 安装

1. 将编译好的插件复制到对应的调试器目录：

```powershell
# 对于 x64dbg（64位）
# 将 <x64dbg路径> 替换为你的实际 x64dbg 安装目录
copy dist\x64dbg_mcp.dp64 <x64dbg路径>\x64\plugins\

# 对于 x32dbg（32位）
copy dist\x32dbg_mcp.dp32 <x64dbg路径>\x32\plugins\

# 示例（如果安装在 D:\Tools\x64dbg）：
# copy dist\x64dbg_mcp.dp64 D:\Tools\x64dbg\x64\plugins\
# copy dist\x32dbg_mcp.dp32 D:\Tools\x64dbg\x32\plugins\
```

2. （可选）复制配置文件：
```powershell
# 为 x64dbg 创建配置目录
mkdir <x64dbg路径>\x64\plugins\x64dbg-mcp
copy config.json <x64dbg路径>\x64\plugins\x64dbg-mcp\

# 为 x32dbg 创建配置目录
mkdir <x64dbg路径>\x32\plugins\x32dbg-mcp
copy config.json <x64dbg路径>\x32\plugins\x32dbg-mcp\
```

3. 重启 x64dbg/x32dbg 以加载插件

## 使用方法

### 启动服务器

1. 打开 x64dbg
2. 导航到 **插件 → MCP Server → Start MCP HTTP Server**
3. 服务器将在配置的端口上启动（默认：3000）
4. 访问 `http://127.0.0.1:3000` 可测试服务器

### 配置

编辑 `config.json` 来自定义设置：

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

### 客户端示例

Python 客户端示例（使用 HTTP）：

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
    
    def subscribe_events(self):
        """订阅 SSE 事件"""
        response = requests.get(
            f"{self.base_url}/sse",
            stream=True,
            headers={"Accept": "text/event-stream"}
        )
        for line in response.iter_lines():
            if line:
                yield line.decode('utf-8')

# 使用
client = MCPClient()
print(client.call("initialize"))
print(client.call("tools/list"))

# 订阅调试事件
for event in client.subscribe_events():
    print(f"事件: {event}")
```

### VS Code 集成

在 VS Code 设置或 MCP 客户端配置中：

```json
{
  "mcpServers": {
    "x64dbg": {
      "url": "http://127.0.0.1:3000",
      "transport": "sse"
    }
  }
}
```

## 可用方法

### 系统方法
- `system.info` - 获取服务器信息
- `system.ping` - 测试连接
- `system.methods` - 列出所有可用方法

### 调试控制
- `debug.run` - 继续执行
- `debug.pause` - 暂停执行
- `debug.step_into` - 单步进入指令
- `debug.step_over` - 单步跨过指令
- `debug.step_out` - 跳出函数
- `debug.get_state` - 获取当前调试状态
- `debug.run_to` - 运行到指定地址
- `debug.restart` - 重启调试会话
- `debug.stop` - 停止调试

### 寄存器操作
- `register.get` - 读取单个寄存器
- `register.set` - 写入寄存器值
- `register.list` - 列出所有寄存器
- `register.get_batch` - 读取多个寄存器

### 内存操作
- `memory.read` - 读取内存区域
- `memory.write` - 写入内存区域
- `memory.search` - 搜索内存模式
- `memory.get_info` - 获取内存区域信息
- `memory.enumerate` - 列出所有内存区域
- `memory.allocate` - 分配内存
- `memory.free` - 释放已分配的内存

### 断点管理
- `breakpoint.set` - 设置断点
- `breakpoint.delete` - 删除断点
- `breakpoint.enable` - 启用断点
- `breakpoint.disable` - 禁用断点
- `breakpoint.toggle` - 切换断点状态
- `breakpoint.list` - 列出所有断点
- `breakpoint.get` - 获取断点详情
- `breakpoint.delete_all` - 删除所有断点
- `breakpoint.set_condition` - 设置断点条件
- `breakpoint.set_log` - 设置断点日志消息
- `breakpoint.reset_hitcount` - 重置断点命中计数

### 反汇编
- `disassembly.at` - 在指定地址反汇编
- `disassembly.range` - 反汇编地址范围
- `disassembly.function` - 反汇编整个函数

### 符号解析
- `symbol.resolve` - 将符号解析为地址
- `symbol.from_address` - 从地址获取符号
- `symbol.search` - 按模式搜索符号
- `symbol.list` - 列出所有符号
- `symbol.modules` - 列出已加载的模块
- `symbol.set_label` - 设置符号标签
- `symbol.set_comment` - 设置符号注释
- `symbol.get_comment` - 获取符号注释

### 模块操作
- `module.list` - 列出所有已加载的模块
- `module.get` - 获取模块信息
- `module.get_main` - 获取主模块

### 线程操作
- `thread.list` - 列出所有线程
- `thread.get_current` - 获取当前线程
- `thread.get` - 获取线程信息
- `thread.switch` - 切换到指定线程
- `thread.suspend` - 挂起线程
- `thread.resume` - 恢复线程
- `thread.get_count` - 获取线程数量

### 栈操作
- `stack.get_trace` - 获取栈回溯
- `stack.read_frame` - 读取栈帧
- `stack.get_pointers` - 获取栈指针（RSP、RBP）
- `stack.is_on_stack` - 检查地址是否在栈上

有关完整的方法签名和示例，请参见源代码中的内联文档或使用 `system.methods` API 调用。

## 架构

插件分为四层：

1. **通信层**：HTTP 服务器，支持 SSE 实时事件推送
2. **协议层**：JSON-RPC 和 MCP 协议解析、验证、分发
3. **业务层**：调试操作、内存管理、符号解析
4. **插件层**：x64dbg 集成、事件处理、回调管理

### 核心组件

- **MCPHttpServer**：HTTP 服务器，带 SSE 端点用于事件流
- **MethodDispatcher**：将 JSON-RPC 调用路由到相应的处理器
- **业务管理器**：DebugController、MemoryManager、RegisterManager 等
- **事件系统**：通过 SSE 实现实时调试事件通知

## 安全注意事项

- 默认情况下，内存和寄存器写入操作是**禁用**的
- 仅在需要时在 `config.json` 中启用写入权限
- 服务器默认监听本地主机（127.0.0.1）
- 单客户端连接限制可防止资源耗尽
- 所有操作都要求调试器处于暂停状态

## 故障排除

### 插件未加载
- 确保插件文件在正确的目录中
- 检查 x64dbg 日志以查找错误消息
- 验证 x64dbg 版本兼容性（需要 x64dbg build 2023+）

### 服务器无法启动
- 检查端口 3000 是否已被占用
- 验证 config.json 是否为有效的 JSON
- 检查插件目录的文件权限
- 查看 x64dbg 日志文件获取详细错误信息

### 连接被拒绝
- 确保通过插件菜单启动了 HTTP 服务器（"Start MCP HTTP Server"）
- 检查防火墙设置，允许端口 3000
- 验证客户端连接到 http://127.0.0.1:3000
- 可在浏览器中访问 http://127.0.0.1:3000 测试连接

## 贡献

欢迎贡献！请：
1. Fork 本仓库
2. 创建功能分支
3. 使用清晰的提交消息进行更改
4. 提交 Pull Request

详细信息请参见 [CONTRIBUTING_CN.md](CONTRIBUTING_CN.md)。

## 许可证

本项目采用 MIT 许可证 - 详见 LICENSE 文件。

## 致谢

- [x64dbg](https://x64dbg.com/) - 本插件扩展的调试器
- [nlohmann/json](https://github.com/nlohmann/json) - JSON 库
- 模型上下文协议规范

## 联系方式

- GitHub Issues：用于错误报告和功能请求

---

**注意**：这是实验性软件。使用风险自负。在用于关键应用程序之前，请务必在安全环境中进行测试。
