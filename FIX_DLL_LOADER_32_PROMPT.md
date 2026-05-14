# Fix dll_loader_32 — Subagent Investigation Prompt

**Goal:** Fix the `dll_loader_32` sample crash (exit 139/SIGSEGV, EIP=0x0) and make it pass.

**What we know:**
- The 64-bit `dll_loader` sample passes fine (LoadLibraryA → GetProcAddress → FreeLibraryA all work).
- The 32-bit `dll_loader_32` crashes with `EIP=0x0` right after printing "=== DLL Loader Test ===", which means it crashes inside or immediately after `LoadLibraryA("exportlib.dll")`.
- GDB backtrace: `#0 0x0`, `#1 0x19` — null function pointer call, caller at a very low PE-space address.
- The 32-bit loader uses `int $0x80` inline syscalls, a standalone `my_wine32` ELF process, FS→TEB via `set_thread_area`, and hand-rolled string ops (no glibc in guest context).
- `dll_loader.c` (the DLL loader) is shared between 32 and 64 bit. `pe32_entry.c` is the 32-bit-only entry point.

**What to investigate (delegate each as a SEPARATE subagent task):**

---

## Investigation 1: Trace LoadLibraryA → load_dll call path in 32-bit

**Files to read:**
- `src/msvcrt/kernel32_module.c` (LoadLibraryA implementation)
- `src/loader/dll_loader.c` (load_dll)
- `src/loader/dll_loader.h`

**Question:** When `LoadLibraryA("exportlib.dll")` is called in the 32-bit build, what exact code path leads to `load_dll()`? Is `find_dll_path` correctly finding the DLL? Is there any 32-bit-specific code path difference from the 64-bit build?

**Deliverable:** A step-by-step call chain from `LoadLibraryA` to the crash point, noting any 32-vs-64 differences.

---

## Investigation 2: Check map_image_at behavior for 32-bit DLLs

**Files to read:**
- `src/loader/image_mapper.c` (or wherever `map_image_at` is defined)
- `src/syscall/syscalls_inline.h` (specifically `INLINE_SYSCALL_MMAP` and `INLINE_SYSCALL_MMAP2`)
- `src/loader/pe32_entry.c` (how DLL base allocation is initialized)

**Question:** When `load_dll` calls `map_image_at` to map a DLL below 4GB using the atomic base allocator, does the 32-bit build correctly use `mmap2` (page-offset syscall) instead of `mmap` (which can't handle full 64-bit offsets in 32-bit `int $0x80`)? Is `DLL_ALLOC_BASE` set to a value compatible with the 32-bit address space?

**Deliverable:** Confirm whether the mmap call for DLL mapping works correctly in 32-bit. Identify any truncation or alignment issues.

---

## Investigation 3: Check 32-bit relocation application

**Files to read:**
- `src/loader/image_mapper.c` (specifically the relocation application code)
- `src/pe_headers.c` or wherever relocation parsing lives

**Question:** When the DLL is mapped, are 32-bit relocations (IMAGE_REL_BASED_DIR32) correctly applied? Is there a difference in how relocations are handled between PE32 and PE32+ that could cause a function pointer to end up as 0x0?

**Deliverable:** Confirm relocation correctness for PE32 DLLs loaded by the 32-bit loader.

---

## Investigation 4: Check export parsing and GetProcAddress in 32-bit

**Files to read:**
- `src/loader/export_table.c` (parse_export_table)
- `src/loader/export_table.h`
- `src/msvcrt/kernel32_module.c` (GetProcAddress implementation)

**Question:** After LoadLibraryA succeeds and returns a module handle, does `GetProcAddress(hDll, "dll_add")` correctly look up the export? Is the export table parsing different for PE32 vs PE32+ DLLs? Could the export address table offsets be misinterpreted in 32-bit?

**Deliverable:** Trace from GetProcAddress to the actual address lookup. Identify if the crash at EIP=0x0 is a failed export lookup returning NULL that gets called directly.

---

## Investigation 5: Check the exportlib.dll binary itself

**Files to read:**
- Run `readelf -hW samples/dll_loader_32/exportlib.dll` or equivalent
- Run `objdump -p samples/dll_loader_32/exportlib.dll` to check exports
- Compare with `samples/dll_loader/exportlib.dll` (the 64-bit version)

**Question:** Is the `exportlib.dll` in `samples/dll_loader_32/` actually a PE32 binary? Does it have the expected exports (`dll_add`, `dll_greeting`, `dll_puts`)? Is it compiled as a DLL (IMAGE_FILE_DLL flag) or a regular EXE? Does it have relocations?

**Deliverable:** Binary analysis of the 32-bit exportlib.dll — PE type, exports, relocations, entry point.

---

## Investigation 6: GDB deep-dive with breakpoints

**Action:** Run the 32-bit sample under GDB with strategic breakpoints:
```
gdb -batch \
  -ex "break load_dll" \
  -ex "break map_image_at" \
  -ex "break resolve_import_pass1" \
  -ex "break parse_export_table" \
  -ex "run" \
  -ex "bt" \
  --args ./my_wine32 samples/dll_loader_32/dll_loader_32.exe
```

Also try:
```
gdb -batch \
  -ex "start" \
  -ex "break *0x19" \
  -ex "run" \
  -ex "bt" \
  -ex "info registers" \
  -ex "x/10i \$eip" \
  --args ./my_wine32 samples/dll_loader_32/dll_loader_32.exe
```

**Question:** At what exact point does execution go wrong? Is it during DLL mapping, relocation, import resolution, or when calling an exported function?

**Deliverable:** GDB output showing the exact crash point and surrounding instructions.

---

## Consolidation Instructions

After all investigations complete:

1. **Correlate findings** across all 6 investigations.
2. **Identify the root cause** — is it one specific bug, or a chain of issues?
3. **Propose a concrete fix** with the minimum number of file changes.
4. **Validate** by building and running `bash scripts/samples.sh run dll_loader_32`.

**Rule:** Each investigation is independent. Read only the files listed. Do not try to read the entire codebase. Focus on answering the specific question.
