# Contributing to x64dbg MCP Server

English | [中文](docs/CONTRIBUTING_CN.md)

Thank you for your interest in contributing! This document provides guidelines for contributing to the project.

## How to Contribute

### Reporting Bugs

1. Check if the bug has already been reported in [Issues](https://github.com/SetsunaYukiOvO/x64dbg-mcp/issues)
2. If not, create a new issue with:
   - Clear title and description
   - Steps to reproduce
   - Expected vs actual behavior
   - x64dbg version and plugin version
   - Error logs if available

### Suggesting Features

1. Open an issue with the `enhancement` label
2. Describe the feature and its use case
3. Explain why it would be useful to users

### Code Contributions

#### Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/x64dbg-mcp.git`
3. Create a feature branch: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Test thoroughly
6. Commit with clear messages
7. Push to your fork
8. Open a Pull Request

#### Code Style

- Follow C++17 standards
- Use 4 spaces for indentation (no tabs)
- Place opening braces on the same line
- Use meaningful variable and function names
- Add comments for complex logic
- Keep functions focused and concise

Example:
```cpp
namespace MCP {

class MyClass {
public:
    void MyFunction(int param) {
        if (param > 0) {
            // Do something
        }
    }
};

} // namespace MCP
```

#### Commit Messages

- Use present tense ("Add feature" not "Added feature")
- First line: brief summary (50 chars or less)
- Blank line, then detailed description if needed
- Reference issues: "Fixes #123"

Example:
```
Add memory search pattern support

- Implement wildcard pattern matching
- Add hex pattern parsing
- Update memory handler tests
Fixes #42
```

#### Testing

- Test your changes with x64dbg
- Verify all existing functionality still works
- Add tests for new features if applicable
- Check for memory leaks
- Test with both 32-bit and 64-bit targets

#### Pull Request Process

1. Update documentation if needed
2. Ensure code compiles without warnings
3. Test thoroughly
4. Fill out PR template with:
   - Description of changes
   - Related issue numbers
   - Testing performed
   - Breaking changes (if any)

## Development Setup

See [README.md](README.md) for build instructions.

### Directory Structure

```
x64dbg-mcp/
├── src/
│   ├── core/          # Core utilities
│   ├── communication/ # Network layer
│   ├── business/      # Business logic
│   └── handlers/      # Request handlers
├── docs/              # Documentation
└── examples/          # Example clients
```

## Code Review

All submissions require review. We use GitHub pull requests for this purpose.

Reviewers will check:
- Code quality and style
- Test coverage
- Documentation updates
- Performance impact
- Security implications

## Questions?

Feel free to open an issue or discussion if you have questions!

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
