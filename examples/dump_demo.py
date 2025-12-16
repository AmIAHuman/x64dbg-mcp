"""
x64dbg MCP Dump功能演示脚本
展示如何使用MCP协议进行自动化内存dump和脱壳操作
"""

import requests
import json
import time
from typing import Dict, Any, Optional

class X64DBGDumpClient:
    """x64dbg MCP Dump客户端"""
    
    def __init__(self, base_url: str = "http://127.0.0.1:3000"):
        self.base_url = base_url
        self.request_id = 0
        
    def _send_request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """发送JSON-RPC 2.0请求"""
        self.request_id += 1
        
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id
        }
        
        response = requests.post(
            f"{self.base_url}/rpc",
            json=payload,
            headers={"Content-Type": "application/json"}
        )
        
        result = response.json()
        
        if "error" in result:
            raise Exception(f"RPC Error: {result['error']}")
        
        return result.get("result", {})
    
    # ========== Dump相关方法 ==========
    
    def dump_module(self, module: str, output_path: str, 
                   fix_imports: bool = True,
                   fix_oep: bool = True,
                   auto_detect_oep: bool = False,
                   rebuild_pe: bool = True) -> Dict[str, Any]:
        """
        Dump指定模块
        
        Args:
            module: 模块名或基址 (如 "ntdll.dll" 或 "0x7FF800000000")
            output_path: 输出文件路径
            fix_imports: 是否修复导入表
            fix_oep: 是否修复入口点
            auto_detect_oep: 是否自动检测OEP
            rebuild_pe: 是否重建PE头
            
        Returns:
            Dump结果字典
        """
        params = {
            "module": module,
            "output_path": output_path,
            "options": {
                "fix_imports": fix_imports,
                "fix_oep": fix_oep,
                "auto_detect_oep": auto_detect_oep,
                "rebuild_pe": rebuild_pe,
                "remove_integrity_check": True
            }
        }
        
        print(f"[*] Dumping module: {module}")
        result = self._send_request("dump.module", params)
        
        if result.get("success"):
            print(f"[+] Dump successful!")
            print(f"    File: {result['file_path']}")
            print(f"    Size: {result['dumped_size']} bytes")
            if "original_ep" in result:
                print(f"    Original EP: {result['original_ep']}")
            if "new_ep" in result:
                print(f"    New EP: {result['new_ep']}")
        else:
            print(f"[-] Dump failed: {result.get('error', 'Unknown error')}")
        
        return result
    
    def dump_memory_region(self, address: str, size: int, 
                          output_path: str, 
                          as_raw_binary: bool = False) -> Dict[str, Any]:
        """
        Dump指定内存区域
        
        Args:
            address: 起始地址 (如 "0x400000")
            size: 大小(字节)
            output_path: 输出文件路径
            as_raw_binary: 是否保存为原始二进制(否则尝试修复PE)
            
        Returns:
            Dump结果字典
        """
        params = {
            "address": address,
            "size": size,
            "output_path": output_path,
            "as_raw_binary": as_raw_binary
        }
        
        print(f"[*] Dumping memory region: {address} ({size} bytes)")
        result = self._send_request("dump.memory_region", params)
        
        if result.get("success"):
            print(f"[+] Memory dump successful!")
            print(f"    File: {result['file_path']}")
            print(f"    Size: {result['dumped_size']} bytes")
        else:
            print(f"[-] Memory dump failed: {result.get('error', 'Unknown error')}")
        
        return result
    
    def auto_unpack(self, module: str, output_path: str, 
                   max_iterations: int = 3) -> Dict[str, Any]:
        """
        自动脱壳dump
        
        Args:
            module: 目标模块名或基址
            output_path: 输出文件路径
            max_iterations: 最大迭代次数(用于多层壳)
            
        Returns:
            Dump结果字典
        """
        params = {
            "module": module,
            "output_path": output_path,
            "max_iterations": max_iterations
        }
        
        print(f"[*] Starting automatic unpacking: {module}")
        print(f"    Max iterations: {max_iterations}")
        
        result = self._send_request("dump.auto_unpack", params)
        
        if result.get("success"):
            print(f"[+] Auto-unpack successful!")
            print(f"    File: {result['file_path']}")
            if "detected_oep" in result:
                print(f"    Detected OEP: {result['detected_oep']}")
        else:
            print(f"[-] Auto-unpack failed: {result.get('error', 'Unknown error')}")
        
        return result
    
    def analyze_module(self, module: str) -> Dict[str, Any]:
        """
        分析模块是否加壳
        
        Args:
            module: 模块名或基址
            
        Returns:
            模块分析结果
        """
        params = {"module": module}
        
        print(f"[*] Analyzing module: {module}")
        result = self._send_request("dump.analyze_module", params)
        
        print(f"    Name: {result['name']}")
        print(f"    Path: {result['path']}")
        print(f"    Base: {result['base_address']}")
        print(f"    Size: {result['size']} bytes")
        print(f"    Entry Point: {result['entry_point']}")
        print(f"    Packed: {result['is_packed']}")
        if result['is_packed']:
            print(f"    Packer: {result['packer_id']}")
        
        return result
    
    def detect_oep(self, module_base: str) -> Optional[str]:
        """
        检测原始入口点(OEP)
        
        Args:
            module_base: 模块基址
            
        Returns:
            OEP地址(如果检测到)
        """
        params = {"module_base": module_base}
        
        print(f"[*] Detecting OEP for module at: {module_base}")
        result = self._send_request("dump.detect_oep", params)
        
        if result.get("detected"):
            print(f"[+] OEP detected: {result['oep']} (RVA: {result['rva']})")
            return result['oep']
        else:
            print(f"[-] OEP not detected")
            return None
    
    def get_dumpable_regions(self, module_base: Optional[str] = None) -> list:
        """
        获取可dump的内存区域
        
        Args:
            module_base: 模块基址(可选,None表示获取所有)
            
        Returns:
            内存区域列表
        """
        params = {}
        if module_base:
            params["module_base"] = module_base
        
        result = self._send_request("dump.get_dumpable_regions", params)
        
        print(f"[*] Found {result['count']} dumpable regions")
        for region in result['regions']:
            print(f"    {region['address']}: {region['size']} bytes "
                  f"({region['protection']}) - {region['name']}")
        
        return result['regions']


def demo_basic_dump():
    """演示基本dump功能"""
    print("\n=== 基本Dump演示 ===\n")
    
    client = X64DBGDumpClient()
    
    # 1. 分析模块
    print("1. 分析目标模块")
    module_info = client.analyze_module("notepad.exe")
    
    # 2. Dump模块
    print("\n2. Dump模块到文件")
    client.dump_module(
        module="notepad.exe",
        output_path="C:\\dump\\notepad_dumped.exe",
        fix_imports=True,
        rebuild_pe=True
    )
    
    # 3. 获取可dump区域
    print("\n3. 获取可dump的内存区域")
    base_addr = module_info['base_address']
    regions = client.get_dumpable_regions(base_addr)


def demo_auto_unpack():
    """演示自动脱壳功能"""
    print("\n=== 自动脱壳演示 ===\n")
    
    client = X64DBGDumpClient()
    
    # 1. 分析加壳程序
    print("1. 分析加壳程序")
    module_info = client.analyze_module("packed.exe")
    
    if not module_info['is_packed']:
        print("    程序未加壳,使用普通dump")
        client.dump_module("packed.exe", "C:\\dump\\packed_normal.exe")
        return
    
    # 2. 自动脱壳
    print("\n2. 启动自动脱壳")
    result = client.auto_unpack(
        module="packed.exe",
        output_path="C:\\dump\\packed_unpacked.exe",
        max_iterations=3
    )


def demo_memory_region_dump():
    """演示内存区域dump"""
    print("\n=== 内存区域Dump演示 ===\n")
    
    client = X64DBGDumpClient()
    
    # 1. Dump代码段
    print("1. Dump代码段")
    client.dump_memory_region(
        address="0x400000",
        size=0x1000,
        output_path="C:\\dump\\code_section.bin",
        as_raw_binary=True
    )
    
    # 2. Dump数据段(尝试修复PE)
    print("\n2. Dump PE格式")
    client.dump_memory_region(
        address="0x400000",
        size=0x10000,
        output_path="C:\\dump\\pe_fixed.exe",
        as_raw_binary=False
    )


def demo_oep_detection():
    """演示OEP检测"""
    print("\n=== OEP检测演示 ===\n")
    
    client = X64DBGDumpClient()
    
    # 检测OEP
    print("1. 检测加壳程序的OEP")
    module_info = client.analyze_module("packed.exe")
    base_addr = module_info['base_address']
    
    oep = client.detect_oep(base_addr)
    
    if oep:
        print(f"\n2. 使用检测到的OEP进行dump")
        # 可以在这里设置断点并运行到OEP
        # 然后再dump


def demo_advanced_workflow():
    """演示高级工作流程"""
    print("\n=== 高级工作流程演示 ===\n")
    
    client = X64DBGDumpClient()
    
    # 1. 分析目标
    print("步骤1: 分析目标程序")
    module_info = client.analyze_module("target.exe")
    
    # 2. 检查是否加壳
    if module_info['is_packed']:
        print(f"\n步骤2: 检测到壳 - {module_info['packer_id']}")
        
        # 2a. 尝试自动脱壳
        print("步骤3: 尝试自动脱壳")
        result = client.auto_unpack(
            module="target.exe",
            output_path="C:\\dump\\target_unpacked.exe",
            max_iterations=5
        )
        
        if not result.get('success'):
            # 2b. 自动脱壳失败,手动检测OEP
            print("步骤4: 自动脱壳失败,手动检测OEP")
            oep = client.detect_oep(module_info['base_address'])
            
            if oep:
                print(f"步骤5: 在OEP设置断点: {oep}")
                # TODO: 设置断点并运行
                # client.set_breakpoint(oep)
                # client.run()
                
                print("步骤6: 运行到OEP后dump")
                client.dump_module(
                    module="target.exe",
                    output_path="C:\\dump\\target_manual.exe",
                    auto_detect_oep=False
                )
    else:
        print("\n步骤2: 未检测到壳,直接dump")
        client.dump_module(
            module="target.exe",
            output_path="C:\\dump\\target.exe"
        )
    
    # 3. 获取所有内存区域
    print("\n步骤7: 枚举所有内存区域")
    regions = client.get_dumpable_regions(module_info['base_address'])
    
    # 4. Dump特定区域
    print("\n步骤8: Dump感兴趣的内存区域")
    for region in regions[:3]:  # 只dump前3个区域
        if "EXECUTE" in region['protection']:
            output = f"C:\\dump\\region_{region['address']}.bin"
            client.dump_memory_region(
                address=region['address'],
                size=region['size'],
                output_path=output,
                as_raw_binary=True
            )


if __name__ == "__main__":
    print("x64dbg MCP Dump功能演示")
    print("=" * 60)
    
    try:
        # 运行各种演示
        # demo_basic_dump()
        # demo_auto_unpack()
        # demo_memory_region_dump()
        # demo_oep_detection()
        demo_advanced_workflow()
        
        print("\n演示完成!")
        
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()
