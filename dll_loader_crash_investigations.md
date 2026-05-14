# DLL Loader 32 Crash Investigation Log

## Crash Description

`dll_loader_32` sample crashes with `EIP=0x00000000` (NULL function pointer call).

### Environment
- my_wine32: 32-bit standalone ELF binary (-m32)
- Sample: samples/dll_loader_32/dll_loader_32.exe (MinGW-w64 PE32)
- All other 32-bit samples pass (hello_world_32, dispatcher_regs_32, multi_import_32, multi_syscall_32, sync_test_32, null_deref_32, file_io_32, heap_test_32, time_test_32, virtual_mem_32)

### Crash Output
```
wine: dispatcher at 0x000000000804fbe6
=== DLL Loader Test ===
CRASH: SIGSEGV
 EIP=0x00000000  ESP=0x001xxxxx  EAX=0x00000001
```

- **EIP=0x00000000**: NULL function pointer call
- **EAX=0x00000001**: Likely sys_exit or return value 1
- **Return address on stack was 0x00000019** (25 bytes into PE header padding — not code)

### Crash Point
- Occurs immediately after "=== DLL Loader Test ===" banner is printed
- Banner uses GetStdHandle + WriteFile + lstrlenA (all kernel32 stubs)
- Next statement: `LoadLibraryA("exportlib.dll")`

## Tests Performed

### Test 1: Root Cause - Glibc PLT Calls
**Date:** 2026-05-14  
**Hypothesis:** `find_dll_path()` calls glibc PLT functions (strlen@plt, memcpy@plt, strchr@plt) which crash after GS→TEB switch (glibc accesses TLS via GS-relative addressing).

**Fix:** Created `loader_utils.h` with `dll_*` macros (manual loops, no __builtin_*) and replaced all glibc calls in `dll_path.c`.

**Commits:** 
- `9bd87c6` - Replace glibc PLT calls with dll_* variants
- `d8b15de` - Make dll_* truly inline with -fno-builtin

**Result:** ❌ Crash persisted with identical EIP=0x0, EAX=1

### Test 2: Verify -fno-builtin
**Date:** 2026-05-14  
**Hypothesis:** GCC might optimize manual loops back into PLT calls despite -fno-builtin.

**Verification:** 
```
objdump -d my_wine32 | sed -n '/<find_dll_path>/,/^$/p' | grep plt
```
No PLT calls in find_dll_path ✅

**Result:** find_dll_path is clean, but crash persists

### Test 3: Comprehensive PLT Elimination
**Date:** 2026-05-14  
**Hypothesis:** Other functions in the DLL loading call chain still have PLT calls.

**PLT calls found in DLL loading path (via objdump):**
```
find_module_by_name_safe: memset@plt
add_module: memset@plt, strncpy@plt, fprintf@plt
load_dll: memset@plt, munmap@plt
resolve_imports: memset@plt (2 calls)
parse_export_table: strncpy@plt, strcpy@plt, mprotect@plt, memset@plt
resolve_module_imports: strncpy@plt
```

**Fix:** 
- Commit `24a3af9` - Replace all PLT calls in dll_loader.c, export_table.c, import_resolve.c, loader_utils.h
- Added `dll_strcmp` macro
- Removed all DEBUG() calls that use fprintf

**Result:** 
- All 13 functions in DLL loading path: 0 PLT calls ✅
- Other 32-bit samples still pass ✅
- dll_loader_32 STILL crashes with EIP=0x0 ❌

### Test 4: import_table_count Check
**Finding:** `import_table_count = sizeof(import_table) / sizeof(import_entry_t) - 1`  
Worker noted import_table_count might be 0 at runtime causing resolve_import to skip all entries.

**Analysis:** This is a compile-time constant. `import_table[]` has ~100 entries. Not the issue.

### Test 5: GDB Debugging Attempt
```
gdb -batch -ex "break LoadLibraryA" -ex "run" ...
```
**Result:** GDB couldn't properly debug the 32-bit binary (architecture mismatch: "i386 is not compatible with i386:x86-64"). Crashed at EIP=0xf7fe1540 (glibc area) before reaching our breakpoints.

### Test 6: Other Samples
All 10 other 32-bit samples pass, confirming:
- Basic PE loading works ✅
- Syscall thunks work ✅
- Import resolution for kernel32/ntdll/msvcrt works ✅
- FS→TEB switch works ✅

Only `dll_loader_32` crashes — it's the ONLY sample that calls `LoadLibraryA`.

## Call Chain Analysis

```
dll_loader_32.exe (PE)
  → _main()
    → GetStdHandle(STD_OUTPUT_HANDLE)    ✅ (kernel32 stub)
    → WriteFile(...)                      ✅ (kernel32 stub via syscall)
    → lstrlenA(...)                       ✅ (kernel32 stub)
    → LoadLibraryA("exportlib.dll")       ❌ CRASH

my_wine32 LoadLibraryA() (src/msvcrt/kernel32_dll.c)
  → find_module_by_name_safe()            ✅ (0 PLT calls after fix)
  → find_dll_path()                       ✅ (0 PLT calls after fix)
  → load_dll()                            ✅ (0 PLT calls after fix)
    → map_image_at()                      ? (check PLT)
    → add_module()                        ✅ (0 PLT calls after fix)
    → ldr_add_module()                    ? (check PLT)
    → resolve_module_imports()            ✅ (0 PLT calls after fix)
```

## Key Observations

1. **EAX=1 at crash**: The signal handler shows EAX=1, which is the Linux `sys_exit` syscall number. This suggests the crash happens when trying to call function address 0x0, and EAX happened to be 1 (maybe from a previous syscall returning 1).

2. **Return address 0x19**: The value 0x19 (25) as return address is suspicious. In the PE image layout, 0x19 is within the DOS header. This suggests the crash is in the PE code or the thunk layer, not in our C code.

3. **Banner prints OK**: The banner "=== DLL Loader Test ===" prints successfully, meaning:
   - The PE is mapped correctly
   - Imports are resolved (GetStdHandle, WriteFile, lstrlenA all work)
   - FS→TEB is set (otherwise even these calls would fail)
   - The crash happens specifically at the LoadLibraryA call

4. **The LoadLibraryA function itself starts with a syscall**: The `int $0x80` for `load_dll: ENTER\n` debug output. This means even the entry into LoadLibraryA triggers a syscall.

## Files to Investigate

- `src/msvcrt/kernel32_dll.c` — LoadLibraryA implementation
- `src/loader/dll_loader.c` — load_dll()
- `src/loader/image_mapper.c` — map_image_at() (still has PLT calls!)
- `src/loader/export_table.c` — parse_export_table()
- `src/loader/import_resolve.c` — resolve_import() / resolve_imports()
- `src/syscall/thunk_gen.c` — thunk generation (32-bit specific)
- `src/loader/pe32_entry.c` — setup_fs_and_jump()
- `src/loader/crash_handlers.c` — crash handler
- `src/loader/peb_ldr.c` — LDR management

## Remaining PLT Calls (Not in DLL Loading Path)

After fix commit 24a3af9, these files still have glibc PLT calls that may NOT be hit yet but could be relevant:

- `src/loader/image_mapper.c`: `#include <stdio.h>`, `#include <string.h>`, `memcpy()` calls
- `src/loader/import_table.c`: `#include <string.h>`, `#include <search.h>`, `#include <stdio.h>`
- `src/syscall/thunk_gen.c`: `#include <stdio.h>`, `#include <stdlib.h>`, `#include <string.h>`

These are compiled with `-fno-builtin` but the includes and function calls remain.
