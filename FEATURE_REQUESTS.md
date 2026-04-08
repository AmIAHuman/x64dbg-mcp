# Feature Requests for x64dbg MCP

An LLM-based agent uses x64dbg MCP tools to manually unpack packed Windows PE x86-64
binaries inside a QEMU VM. The agent follows strategy playbooks that describe step-by-step
unpacking procedures using x64dbg MCP tool calls.

**Policy:** All unpacking is manual — no `dump.auto_unpack`. The agent uses `debug.*`,
`breakpoint.*`, `register.*`, `memory.*`, `disassembly.*`, `symbol.*` tools for unpacking,
then dump module tools (`dump.module`, `dump.fix_imports`, `dump.rebuild_pe`,
`dump.detect_oep`) for PE fixing and dumping. The agent has access to the source code
of these tools (from `DumpManager.cpp`) so it understands their internals.

**Existing tools used by the agent:**
- `debug.load_binary`, `debug.run`, `debug.step_over`, `debug.step_into`, `debug.step_out`
- `debug.hide_debugger` (ScyllaHide anti-anti-debug — requires ScyllaHide in VM)
- `breakpoint.set`, `breakpoint.set_condition`
- `register.get`
- `memory.read`, `memory.write`, `memory.search`
- `disassembly.at`
- `symbol.resolve`
- `module.list_imports` (tool 71), `module.list_strings` (tool 72)
- `module.get_sections` (per-section entropy, names, characteristics)
- `dump.module` (PE dump with configurable fixing: fix_imports, fix_oep, rebuild_pe)
- `dump.fix_imports` (standalone IAT reconstruction)
- `dump.rebuild_pe` (standalone PE header rebuild with optional new entry point)
- `dump.detect_oep` (OEP detection via multiple strategies)
- `dump.memory_region` (raw or PE-fixed memory dump)
- `dump.analyze_module` (read-only — module base address, size, packer detection)
- `dump.get_dumpable_regions` (read-only — list memory regions)

---

## Implemented

### 1. `module.list_imports` tool — DONE

Import listing for cross-checking against Ghidra's static parse. Used to verify IAT
population after unpacking (import count > 0 indicates successful unpack).

**Method:** `module.list_imports`
**Status:** Implemented (tool 71)

---

### 2. `module.list_strings` tool — DONE

String listing for finding packer markers and meaningful strings. Used to confirm
unpacking reached real code (presence of meaningful strings vs packer stubs).

**Method:** `module.list_strings`
**Status:** Implemented (tool 72)

---

## Required

### 3. Remove `dump.auto_unpack` tool

The `dump.auto_unpack` tool (method `dump.auto_unpack`, handler `AutoUnpackAndDump`)
is a black-box generic auto-unpacker. It should be removed from the MCP plugin.

**Why:** The agent must learn unpacking through manual tool use and playbook evolution.
`dump.auto_unpack` bypasses this by automating the entire unpacking process — OEP
detection, breakpoint placement, execution control, and dumping — in a single opaque
call. The agent cannot learn from it or improve its approach based on failures.

The PE fixing tools (`dump.module`, `dump.fix_imports`, `dump.rebuild_pe`,
`dump.detect_oep`) are kept because their source code is transparent to the agent
and they handle post-unpacking PE reconstruction, not the unpacking itself.

**Files to modify:**
- `src/handlers/DumpHandler.cpp` — remove `AutoUnpackAndDump` handler
- `src/handlers/DumpHandler.h` — remove declaration
- `src/business/DumpManager.cpp` — remove `AutoUnpackAndDump` implementation
- `src/business/DumpManager.h` — remove declaration
- `src/core/MCPToolRegistry.cpp` — remove tool registration for `dump.auto_unpack`

---

### 4. `debug.hide_debugger` tool — DONE

Configures ScyllaHide anti-anti-debug protections via the MCP. Requires ScyllaHide
plugin installed in x64dbg on the VM.

**Method:** `debug.hide_debugger`
**Status:** Implemented. Techniques: `peb`, `ntquery`, `timing`, `hardware`, `window`,
`handle`, `thread`, `misc`, `all`.
**Prerequisite:** ScyllaHide must be installed as an x64dbg plugin in the QEMU VM.

Used in the unpacker playbook as general step 5:
`debug.hide_debugger(techniques=["all"])`

---

### 7. Rewrite `dump.fix_imports` — file-based API with real Scylla reconstruction — DONE

The current `dump.fix_imports` was broken for packed binaries:

1. **`ScyllaRebuildImports` is unimplemented** — the function in `DumpManager.cpp:1475` is a
   stub that always returns `false`. The TODO comments describe what it should do but
   no logic exists.

2. **Buffer parameter is impractical** — the tool requires the entire PE file as a JSON
   byte array (`"buffer": [77, 90, ...]`). A 500KB dump becomes a 500K-element JSON array.
   The agent typically passes `buffer=[]` (empty) which silently fails.

3. **Fallback `FixImportTable` copies from packed PE** — when Scylla fails, it reads the
   original on-disk PE file and copies its import table. For packed binaries, the
   on-disk file only has the packer's stub imports (e.g., 5 imports for UPX), not
   the real application imports.

**Proposed API change:** Accept a file path instead of a buffer.

```
dump_fix_imports:
  file_path (string, required): Path to dumped PE file on disk (e.g., "C:\analysis\dump.exe")
  module_base (string, required): Module base address in the debugged process
  oep (string, optional): Original entry point RVA for the dump
```

**Implementation:**

1. Read the dump file from disk (no JSON serialization needed)
2. Use x64dbg's debug session to access the live process memory
3. Scan the IAT region in the running process to find resolved function pointers
4. For each IAT entry: resolve the address to module+function name via x64dbg's
   symbol resolution (`Script::Symbol::GetModuleAndFunctionName`)
5. Rebuild the import directory and IAT in the dump buffer
6. Write the fixed PE back to disk
7. Return success/failure with import count

This is what Scylla does internally. The key insight is that the live process
(still running in x64dbg at the OEP) has a fully resolved IAT in memory — the
loader already fixed up all import addresses. We just need to read them and
translate back to names.

**Why this matters:** Without working import reconstruction, the dynamic analyzer
receives dumps with 0 imports visible to Ghidra, forcing it to rely solely on
decompiled code patterns. Import tables are the single most valuable source of
behavioral information for classification.

**Files to modify:**
- `src/core/MCPToolRegistry.cpp` — change parameter list (file_path instead of buffer)
- `src/handlers/DumpHandler.cpp` — rewrite `FixImports` to read from disk
- `src/business/DumpManager.cpp` — implement real `ScyllaRebuildImports` using
  x64dbg's debug session and symbol resolution
- `src/business/DumpManager.h` — update signatures

---

## Nice to Have

### 5. `breakpoint.set_return_override` tool

Currently, hooking an API's return value requires: set BP → run → hit BP → step out →
read registers → write return value → continue. This is 3-5 tool calls per API hook.
A BP with automatic return value override would collapse this to one call.

**Method:** `breakpoint.set_return_override`

**Parameters:**
```json
{
  "address": "0x7FF812345678",
  "return_value": 0,
  "action": "continue"
}
```

`action`: `"continue"` (auto-resume after override) or `"pause"` (break after override
for inspection).

**Returns:**
```json
{
  "success": true,
  "breakpoint_id": 42
}
```

When the BP fires, the plugin automatically:
1. Steps out of the function (or detects return instruction)
2. Sets RAX to `return_value`
3. If `action=="continue"`, resumes execution

**Why:** Anti-debug API hooking (feature #4) and runtime injection both need return value
manipulation. Each manual hook is 3 tool calls (step_out + register.set + run). With
N APIs hooked per attempt, this adds up. Useful beyond anti-debug — any API interception
scenario benefits.

---

### 6. `module.get_sections` tool — ALREADY IMPLEMENTED

**Status:** Already implemented in `ModuleHandler.cpp` (`GetSections`).

Return PE section info (name, virtual address, virtual size, raw size, characteristics,
entropy) for a loaded module. The agent uses section entropy and names to classify
packers (e.g. UPX0/UPX1 sections with high entropy → UPX) and to verify unpacking
success (entropy drops after decompression).

x64dbg sees the loaded (in-memory) section layout which may differ from the on-disk
PE headers that Ghidra parses. Comparing both views reveals packer modifications.

**Method:** `module.get_sections`

**Parameters:**
```json
{
  "module": "sample.exe"
}
```

**Returns:**
```json
{
  "sections": [
    {
      "name": "UPX0",
      "virtual_address": "0x401000",
      "virtual_size": 65536,
      "raw_size": 0,
      "characteristics": "0xE0000080",
      "entropy": 7.8
    }
  ],
  "count": 3
}
```

**Entropy calculation (current implementation in `ModuleHandler.cpp`):**

Per-section Shannon entropy computed over raw bytes read from loaded module memory:
```cpp
// Read section bytes in chunks from loaded memory
Script::Memory::Read(sec.addr + offset, buffer.data(), readSize, nullptr);

// Count byte frequencies
uint64_t freq[256] = {};
for (duint j = 0; j < readSize; ++j)
    freq[buffer[j]]++;

// Compute Shannon entropy
for (int b = 0; b < 256; ++b) {
    if (freq[b] > 0) {
        double p = (double)freq[b] / (double)totalBytes;
        entropy -= p * std::log2(p);
    }
}

// Round to 2 decimal places
entry["entropy"] = std::round(entropy * 100.0) / 100.0;
```

**Comparison with Ghidra headless script
(`adversarial-re-agent/src/utils/ghidra_scripts/analyze_pe.py`):**

| Aspect | x64dbg `module.get_sections` | Ghidra `analyze_pe.py` |
|--------|------------------------------|------------------------|
| Formula | `entropy -= p * log2(p)` | `entropy -= p * log(p) / log(2)` (equivalent) |
| Scope | Per-section (all sections) | Executable sections only, averaged |
| Byte source | Loaded memory (`Script::Memory::Read`) | Ghidra memory blocks (`block.getBytes`) |
| Rounding | 2 decimal places | 4 decimal places |
| Output | Per-section entropy in JSON array | Single `avg_text_entropy` float |

The per-byte Shannon formula is identical. Values are directly comparable for the
same memory region. Differences in scope (per-section vs averaged executable) serve
different purposes:
- x64dbg per-section: Unpacker checks individual sections during unpacking
- Ghidra averaged: R_unpack scoring compares dump vs original binary

**Use cases in the solver pipeline:**
- **Triage:** cross-check section names/entropy against Ghidra's static view
- **Unpacker:** verify entropy dropped after unpacking (packed >7.0 → unpacked <6.5)
  before committing to the dump step
- **R_unpack scoring:** headless Ghidra computes `avg_text_entropy` on the dump file
