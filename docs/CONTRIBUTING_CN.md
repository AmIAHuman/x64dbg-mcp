# 为 x64dbg MCP Server 做贡献

[English](../CONTRIBUTING.md) | 中文

感谢您对贡献的兴趣！本文档提供了为本项目做贡献的指南。

## 如何贡献

### 报告错误

1. 检查错误是否已在 [Issues](https://github.com/SetsunaYukiOvO/x64dbg-mcp/issues) 中报告
2. 如果没有，创建一个新问题，包括：
   - 清晰的标题和描述
   - 重现步骤
   - 期望行为与实际行为
   - x64dbg 版本和插件版本
   - 错误日志（如果有）

### 建议功能

1. 使用 `enhancement` 标签创建问题
2. 描述功能及其用例
3. 解释为什么它对用户有用

### 代码贡献

#### 入门

1. Fork 本仓库
2. 克隆你的 fork：`git clone https://github.com/yourusername/x64dbg-mcp.git`
3. 创建功能分支：`git checkout -b feature/your-feature-name`
4. 进行更改
5. 彻底测试
6. 使用清晰的消息提交
7. 推送到你的 fork
8. 打开 Pull Request

#### 代码风格

- 遵循 C++17 标准
- 使用 4 个空格缩进（不使用制表符）
- 将左花括号放在同一行
- 使用有意义的变量和函数名
- 为复杂逻辑添加注释
- 保持函数专注和简洁

示例：
```cpp
namespace MCP {

class MyClass {
public:
    void MyFunction(int param) {
        if (param > 0) {
            // 做某事
        }
    }
};

} // namespace MCP
```

#### 提交消息

- 使用现在时（"Add feature" 而不是 "Added feature"）
- 第一行：简短摘要（50 个字符或更少）
- 空行，然后是详细描述（如果需要）
- 引用问题："Fixes #123"

示例：
```
Add memory search pattern support

- 实现通配符模式匹配
- 添加十六进制模式解析
- 更新内存处理程序测试
Fixes #42
```

#### 测试

- 使用 x64dbg 测试你的更改
- 验证所有现有功能仍然正常工作
- 为新功能添加测试（如果适用）
- 检查内存泄漏
- 使用 32 位和 64 位目标进行测试

#### Pull Request 流程

1. 如果需要，更新文档
2. 确保代码编译无警告
3. 彻底测试
4. 填写 PR 模板，包括：
   - 更改描述
   - 相关问题编号
   - 执行的测试
   - 破坏性更改（如果有）

## 开发设置

构建说明请参见 [README_CN.md](../README_CN.md)。

### 目录结构

```
x64dbg-mcp/
├── src/
│   ├── core/          # 核心工具
│   ├── communication/ # 网络层
│   ├── business/      # 业务逻辑
│   └── handlers/      # 请求处理程序
├── docs/              # 文档
└── examples/          # 示例客户端
```

## 代码审查

所有提交都需要审查。我们使用 GitHub Pull Request 进行此目的。

审查人员将检查：
- 代码质量和风格
- 测试覆盖率
- 文档更新
- 性能影响
- 安全影响

## 有疑问？

如果有问题，请随时提交问题或讨论！

## 许可证

通过贡献，您同意您的贡献将根据 MIT 许可证授权。
