# 更新日志

x64dbg MCP 服务器插件的所有重要更改都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/spec/v2.0.0.html)。

## [1.0.2] - 2025-12-16

### 修复
- **来自自动化测试的关键缺陷修复**
  - 修复 `breakpoint_toggle` 返回不一致的启用状态
  - 实现了 `memory_search` 的真正搜索功能（之前是占位代码）
  - 修复 `memory_get_info` 使用 VirtualQueryEx API 返回正确的内存区域基址
  - 修复 `debug_step_over` 与 x64dbg 状态更新的 RIP 同步时序
  - 增强 `dump_detect_oep` 策略验证，提供清晰的错误消息
  - 为多个工具添加缺失的诊断字段：
    - `script_get_last_result`：添加 `error` 字段
    - `stack_read_frame`：添加 `encoding` 字段
    - `dump_memory_region`：添加完整的 `progress` 信息

### 变更
- **构建系统改进**
  - 新的双架构构建脚本：一次命令编译 x64 和 x86
  - 统一输出目录（`dist/`）用于两个架构
  - 使用 `-j` 标志并行编译，加快构建速度
  - 简化的构建选项：`--clean`、`--x64-only`、`--x86-only`
  - 构建脚本不再阻塞于交互式提示

### 删除
- **文档清理**
  - 删除冗余的技术文档文件
  - 精简为核心文档

## [1.0.1] - 2025-11-20

### 新增
- 完整的线程管理 API
  - `thread.list`：列出所有线程
  - `thread.get_current`：获取当前线程信息
  - `thread.get`：获取特定线程详情
  - `thread.switch`：切换活动线程
  - `thread.suspend`：挂起线程执行
  - `thread.resume`：恢复线程执行
  - `thread.get_count`：获取线程数量
- 完整的调用栈跟踪 API
  - `stack.get_trace`：获取完整的调用栈
  - `stack.read_frame`：读取特定栈帧
  - `stack.get_pointers`：获取栈指针（ESP/RSP、EBP/RBP）
  - `stack.is_on_stack`：检查地址是否在栈上

### 变更
- 改进了所有处理器的错误处理
- 增强了日志系统，格式化更好

### 修复
- 线程切换可靠性
- 栈帧读取准确性

## [1.0.0] - 2025-11-20

### 新增
- 初始发布，包含完整的核心功能
- JSON-RPC 2.0 协议实现
- 支持事件流的 HTTP 服务器和 SSE
- 跨多个类别的 50+ 调试方法：
  - **调试控制**：run、pause、step_into、step_over、step_out、run_to、restart、stop、get_state
  - **内存操作**：read、write、search、get_info、enumerate、allocate、free、get_protection
  - **寄存器操作**：get、set、list、get_batch（50+ 寄存器）
  - **断点管理**：set、delete、enable、disable、toggle、list、get、delete_all、set_condition、set_log
  - **反汇编**：at、range、function
  - **符号解析**：resolve、from_address、search、list、modules
  - **注释/标签管理**：set_comment、get_comment、set_label
  - **模块信息**：list、get、get_main、find
- 基于权限的访问控制系统
- 配置文件支持（config.json）
- 多级日志系统
- 通过 SSE 的事件通知系统
- 带有 JSON-RPC 错误代码的全面错误处理
- Python 客户端示例

### 技术细节
- 使用 C++17 构建
- 使用 x64dbg 插件 SDK
- 依赖项：通过 vcpkg 的 nlohmann/json
- 支持 Windows (x64) 上的 x64dbg
- 插件大小：约 445 KB

## [0.3.0] - 2025-11-15（内部版本）

### 新增
- 硬件断点支持（DR0-DR3）
- 内存断点实现
- 条件断点系统
- 日志断点功能
- 内存搜索功能
- 批量寄存器读取操作

## [0.2.0] - 2025-11-12（内部版本）

### 新增
- 完整的断点管理
- 符号解析系统
- 注释和标签管理
- 事件回调处理器
- 权限检查器系统
- 配置管理

## [0.1.0] - 2025-11-10（内部版本）

### 新增
- 基础插件框架
- HTTP 服务器实现
- JSON-RPC 协议解析器
- 核心调试方法
- 内存和寄存器操作
- 初始文档

---

## 版本历史摘要

| 版本 | 发布日期 | 主要功能 |
|---------|-------------|--------------|
| 1.0.2 | 2025-12-16 | 缺陷修复与构建改进 |
| 1.0.1 | 2025-11-20 | 线程和栈管理 |
| 1.0.0 | 2025-11-20 | 首次公开发布 |
| 0.3.0 | 2025-11-15 | 高级断点 |
| 0.2.0 | 2025-11-12 | 符号和权限 |
| 0.1.0 | 2025-11-10 | 核心框架 |

---

## 未来路线图

### v1.2.0 计划
- 性能优化和基准测试
- 增强的安全功能（Token/API Key 认证）
- WebSocket 协议支持
- 多客户端连接支持

### v2.0.0 计划
- AI 辅助分析功能
- 自动模式识别
- 调用图生成
- CFG（控制流图）导出
- 高级内存可视化

---

## 升级说明

### 从 1.0.1 升级到 1.0.2
- 无破坏性更改
- 仅修复缺陷和改进构建系统
- 不需要修改配置文件或客户端代码

### 从 1.0.0 升级到 1.0.1
- 无破坏性更改
- 新的线程和栈方法仅是添加
- 配置文件格式未更改

---

更多信息，请参阅：
- [README_CN.md](docs/README_CN.md) - 主要文档
- [QUICKSTART_CN.md](docs/QUICKSTART_CN.md) - 快速入门指南
- [Plan.md](Plan.md) - 开发路线图
- [examples/](examples/) - 代码示例
