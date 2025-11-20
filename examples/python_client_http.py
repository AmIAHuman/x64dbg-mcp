"""
x64dbg MCP Server - Python HTTP 客户端示例

演示如何通过 HTTP 连接到 x64dbg MCP Server 并使用 MCP 协议。
支持 JSON-RPC 请求和 SSE 事件订阅。

安装依赖:
    pip install requests

使用方法:
    python python_client_http.py
"""

import requests
import json
import sys
from typing import Dict, Any, Optional, Iterator


class MCPHttpClient:
    """x64dbg MCP Server HTTP 客户端"""
    
    def __init__(self, host: str = '127.0.0.1', port: int = 3000):
        """
        初始化客户端
        
        Args:
            host: 服务器地址
            port: 服务器端口 (默认 3000)
        """
        self.host = host
        self.port = port
        self.base_url = f"http://{host}:{port}"
        self.request_id = 0
        self.session = requests.Session()
    
    def connect(self) -> bool:
        """
        测试连接到服务器
        
        Returns:
            bool: 连接是否成功
        """
        try:
            response = self.session.get(self.base_url, timeout=5)
            print(f"✅ 已连接到 {self.base_url}")
            return response.status_code == 200
        except Exception as e:
            print(f"❌ 连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.session:
            self.session.close()
            print("✅ 已断开连接")
    
    def call(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        发送 JSON-RPC 请求
        
        Args:
            method: 方法名 (如 "initialize", "tools/list")
            params: 参数字典
            
        Returns:
            Dict: 响应数据
        """
        self.request_id += 1
        
        # 构建 JSON-RPC 请求
        request = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {}
        }
        
        try:
            # 发送 HTTP POST 请求
            response = self.session.post(
                f"{self.base_url}/rpc",
                json=request,
                headers={"Content-Type": "application/json"},
                timeout=30
            )
            response.raise_for_status()
            
            # 解析响应
            result = response.json()
            
            # 检查错误
            if 'error' in result:
                error = result['error']
                raise RuntimeError(f"RPC 错误 {error.get('code', 0)}: {error.get('message', '未知错误')}")
            
            return result.get('result', {})
            
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"请求失败: {e}")
    
    def notify(self, method: str, params: Optional[Dict[str, Any]] = None):
        """
        发送通知 (无需响应)
        
        Args:
            method: 方法名
            params: 参数字典
        """
        # 构建通知 (无 id 字段)
        notification = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {}
        }
        
        try:
            self.session.post(
                f"{self.base_url}/rpc",
                json=notification,
                headers={"Content-Type": "application/json"},
                timeout=5
            )
        except requests.exceptions.RequestException as e:
            print(f"⚠️  通知发送失败: {e}")
    
    def subscribe_events(self) -> Iterator[str]:
        """
        订阅 SSE 事件流
        
        Yields:
            str: 事件数据
        """
        try:
            response = self.session.get(
                f"{self.base_url}/sse",
                stream=True,
                headers={"Accept": "text/event-stream"},
                timeout=None
            )
            response.raise_for_status()
            
            for line in response.iter_lines():
                if line:
                    line_str = line.decode('utf-8')
                    if line_str.startswith('data:'):
                        yield line_str[5:].strip()
                        
        except requests.exceptions.RequestException as e:
            print(f"❌ SSE 订阅失败: {e}")


# ========== 示例函数 ==========

def demo_mcp_initialize(client: MCPHttpClient):
    """演示: 初始化 MCP 会话"""
    print("\n" + "="*60)
    print("示例 1: 初始化 MCP 会话")
    print("="*60)
    
    result = client.call("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {
            "name": "python-mcp-client",
            "version": "1.0.0"
        }
    })
    
    print(f"  协议版本: {result.get('protocolVersion', 'N/A')}")
    print(f"  服务器名称: {result.get('serverInfo', {}).get('name', 'N/A')}")
    print(f"  服务器版本: {result.get('serverInfo', {}).get('version', 'N/A')}")
    
    # 发送 initialized 通知
    client.notify("notifications/initialized")
    print("  ✅ 已发送 initialized 通知")


def demo_list_tools(client: MCPHttpClient):
    """演示: 列出可用工具"""
    print("\n" + "="*60)
    print("示例 2: 列出可用工具")
    print("="*60)
    
    result = client.call("tools/list")
    tools = result.get('tools', [])
    
    print(f"  发现 {len(tools)} 个工具:")
    for tool in tools[:10]:  # 只显示前10个
        print(f"    - {tool.get('name', 'N/A')}")
        if tool.get('description'):
            print(f"      描述: {tool['description']}")


def demo_call_tool(client: MCPHttpClient):
    """演示: 调用工具"""
    print("\n" + "="*60)
    print("示例 3: 调用工具 (获取调试状态)")
    print("="*60)
    
    try:
        result = client.call("tools/call", {
            "name": "debug_get_status",
            "arguments": {}
        })
        
        print("  调试状态:")
        for item in result.get('content', []):
            if item.get('type') == 'text':
                print(f"    {item.get('text', 'N/A')}")
                
    except Exception as e:
        print(f"  ⚠️  调用失败: {e}")


def demo_sse_events(client: MCPHttpClient):
    """演示: 订阅 SSE 事件 (前5个事件)"""
    print("\n" + "="*60)
    print("示例 4: 订阅 SSE 事件流 (显示前5个)")
    print("="*60)
    print("  等待事件... (在 x64dbg 中执行调试操作)")
    
    try:
        event_count = 0
        for event_data in client.subscribe_events():
            print(f"  事件 {event_count + 1}: {event_data}")
            event_count += 1
            if event_count >= 5:
                break
    except KeyboardInterrupt:
        print("\n  ⚠️  用户中断")
    except Exception as e:
        print(f"  ⚠️  订阅失败: {e}")


def main():
    """主函数"""
    print("="*60)
    print("x64dbg MCP Server - Python HTTP 客户端示例")
    print("="*60)
    print("\n确保:")
    print("  1. x64dbg 正在运行")
    print("  2. MCP HTTP Server 已启动 (端口 3000)")
    print("  3. (可选) 在 x64dbg 中加载了调试目标")
    
    # 创建客户端
    client = MCPHttpClient(host='127.0.0.1', port=3000)
    
    # 连接
    if not client.connect():
        print("\n❌ 无法连接到服务器")
        print("   请确保:")
        print("   - x64dbg 正在运行")
        print("   - 通过菜单启动了 MCP HTTP Server")
        print("   - 服务器运行在 http://127.0.0.1:3000")
        sys.exit(1)
    
    try:
        # 运行演示
        demo_mcp_initialize(client)
        demo_list_tools(client)
        demo_call_tool(client)
        
        # SSE 事件订阅 (可选,需要手动中断)
        print("\n是否订阅 SSE 事件? (需要在 x64dbg 中执行调试操作)")
        print("按 Ctrl+C 跳过...")
        try:
            import time
            time.sleep(2)  # 给用户2秒思考时间
            # demo_sse_events(client)  # 取消注释以启用
        except KeyboardInterrupt:
            print("\n  ⏭️  跳过 SSE 演示")
        
        print("\n" + "="*60)
        print("✅ 所有示例完成!")
        print("="*60)
        print("\n提示:")
        print("  - 在 x64dbg 中加载程序后调试,可以看到更多工具")
        print("  - 取消注释 demo_sse_events() 以测试事件订阅")
        print("  - 查看 docs/ 了解更多 API 文档")
        
    except Exception as e:
        print(f"\n❌ 错误: {e}")
        import traceback
        traceback.print_exc()
    
    finally:
        # 断开连接
        client.disconnect()


if __name__ == '__main__':
    main()
