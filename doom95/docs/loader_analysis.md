# my_wine Loader Architecture Analysis

## Stub Registration (import_table.c)

### Data Structure
```c
typedef struct {
    const char *dll_name;   // e.g., "ntdll.dll", "kernel32.dll"
    const char *name;       // e.g., "NtWriteFile", "GetStdHandle"
    void *address;          // pointer to stub/handler function (NULL = dynamic)
} import_entry_t;
```

The `import_table[]` is a **static, sentinel-terminated C array** (NULL/NULL/NULL terminator). It is the single source of truth for name→address resolution for all DLL functions my_wine supports.

### Registration Categories
The table has three sections:
1. **ntdll.dll handlers** — every entry points to a `handler_Nt*()` function (non-NULL, known at build time)
2. **kernel32.dll stubs** — C implementations like `GetStdHandle`, `WriteFile`, etc. (non-NULL)
3. **msvcrt.dll** — split into:
   - *Static* entries: CRT startup functions (`__getmainargs`, `__iob_func`, etc.) — addresses known at build time
   - *Dynamic* entries: libc-shim functions (`malloc`, `free`, `fprintf`, etc.) — address field is `NULL`, filled at runtime by `init_msvcrt_imports()` which calls `set_import("malloc", __msvcrt_malloc)`

### How New DLLs Get Added
To add a new DLL (e.g., `user32.dll`):
1. **Add entries to `import_table[]`** — each function needs `{ "user32.dll", "FunctionName", (void*)stub_FunctionName }`
2. **Define the stub function** — typically in a new `src/stubs/user32.c` (or in the `src/msvcrt/` directory)
3. **Add the header declaration** — in a new `include/user32.h` or extend an existing header
4. **Add to ordinal table** (`src/loader/ordinal_table.c`) — if the DLL supports ordinal imports, add `{ "user32.dll", ordinal, "FunctionName" }` entries
5. **Update build system** — the Makefile auto-discovers `.c` files via `find src/ -name '*.c'`, so new files are picked up automatically

### Binary Search Lookup
`init_import_table()` sorts the table by name using `qsort()`. Resolution uses a hand-rolled binary search (`resolve_import()`) that calls `import_cmp_by_name()` — no `bsearch()` dependency.

---

## Import Resolution (import_resolve.c)

### Three-Tier Resolver (`resolve_import()`)
```
Tier 1: import_table[] lookup (binary search by name, ignoring DLL name)
  └─ If found AND address != NULL → return address
  └─ If found AND address == NULL → fall through (dynamic entry not yet filled)
Tier 2: loaded_module_t export table lookup (for real PE DLLs)
  └─ find_module_by_name(dll_name) → lookup_export(mod, func_name)
Tier 3: Not found → return NULL (leaves IAT slot as 0)
```

### Pass 1: IAT Resolution
Walks the PE's `IMAGE_DIRECTORY_ENTRY_IMPORT` directory:
1. For each `IMAGE_IMPORT_DESCRIPTOR`, read the DLL name
2. For each thunk in `OriginalFirstThunk` (ILT):
   - If **ordinal import** (high bit set): look up name via `ordinal_lookup(dll_name, ordinal)`, then `resolve_import()`
   - If **name import**: read `IMAGE_IMPORT_BY_NAME->Name`, then `resolve_import()`
3. Write resolved address into `FirstThunk` (IAT)

### Pass 2: Thunk Patching
Some PE compilers place indirect jmp-thunks (`ff 25 disp32(%rip)`) in `.text` that point into the import directory rather than the canonical IAT. Pass 2:
1. **Scans `.text`** for `ff 25` instructions → collects unique IAT target addresses
2. **Builds a flat array** of all ILT+IAT pairs (DLL, funcName, ILT value, resolved address)
3. **Applies 4 strategies** (in order) to match each thunk target to a resolved import:
   - *Strategy 1 (resolved overlap)*: current value matches any resolved address → already correct
   - *Strategy 2 (ILT value match)*: current value matches an ILT entry's raw value → write resolved address
   - *Strategy 3 (ILT offset match)*: target falls within import directory range → compute slot index → write resolved address
   - *Strategy 4 (positional fallback)*: match by position in the flat array

### Dynamic DLL Loading
`resolve_module_imports()` handles dynamically loaded DLLs:
1. First pass: loads all dependency DLLs (checks if stub library — kernel32/ntdll/msvcrt are skipped, they resolve via import table)
2. Uses `find_dll_path()` to locate real DLL files (searches cwd, app dir, `WINE_DLL_PATH`)
3. Calls `load_dll()` which maps, relocates, registers, and resolves imports recursively
4. Max depth: 8 (prevents circular import loops)

---

## ABI and Handler Macros (handler_abi.h)

```c
#define WINE_STUB __attribute__((ms_abi, used))    // Microsoft x64 ABI
#define HANDLER __attribute__((used))               // System V ABI (called from C)
```

### Calling Convention Split

| Layer | ABI | Called By | Example |
|-------|-----|-----------|---------|
| **PE code** | Microsoft x64 (`ms_abi`) | Guest PE instructions | `kernel32.c` stubs, `msvcrt.c` stubs |
| **handler_*** | System V (default) | `dispatcher_generated.c` or kernel32 stubs | `ntdll_*.c` handlers |

### The Two Code Paths

**Path A: PE → ntdll.dll function**
```
PE code → IAT thunk (23-byte generated) → __wine_dispatcher (asm entry)
  → c_dispatch_syscall() → dispatcher_core() [switch on syscall #]
  → handler_Nt*() [System V, reads guest regs via __wine_guest_regs]
```

**Path B: PE → kernel32.dll function**
```
PE code → IAT thunk → stub function [ms_abi] in kernel32.c
  → calls handler_Nt*() directly [System V, C calling convention]
```
Kernel32 stubs in `src/stubs/kernel32.c` call `handler_Nt*()` directly (not through the syscall dispatcher). They translate kernel32 args to ntdll args. Example: `WriteFile()` → `handler_NtWriteFile()` with zero-filled event/APC/context fields.

**Path C: PE → msvcrt.dll function**
```
PE code → IAT thunk → __msvcrt_* function pointer → wine_* internal function
```
msvcrt functions that would shadow host libc names use `wine_` prefixes internally and are exposed via `__msvcrt_*` function pointers.

### Thunk Generation (`thunk_gen.c`)
23-byte x86_64 machine code generated at runtime:
```
push rdi          (2 bytes)  — save original RDI
mov rdi, imm32    (7 bytes)  — set syscall number
mov rax, imm64    (10 bytes) — load dispatcher address (absolute, handles ASLR)
call rax          (2 bytes)  — enter dispatcher
pop rdi           (1 byte)   — restore RDI
ret               (1 byte)   — return to guest
```
Thunks are allocated in a single mmap'd RWX blob. Indexed by syscall number in `thunk_array[0x60]`.

### Dispatcher Generation
`include/nt_syscalls.def` → `scripts/gen_dispatcher.py` → `src/syscall/dispatcher_generated.c`

Each syscall entry in `.def` file declares:
- Syscall number (`0x05`)
- Handler function name (`handler_NtCallbackReturn`)
- Optional args: `ptr(wb)`, `ptr(wb32)`, `ptr(ro)`, `stack N`
- Call template with `STACK(N)` expansion

---

## PEB/LDR Setup (teb_peb.c)

### TEB (Thread Environment Block)
- Allocated via `mmap()` (1 page, RW)
- Self-referential pointer at offset `TEB_TEB_SELF_REF` (0x08)
- Thread pointer at offset `TEB_THREAD_PTR` (0x30) — fake, points to self
- PEB pointer at offset `TEB_PEB_PTR` (0x60)

**Critical**: GS base is NOT set in `setup_teb_peb()` — it's set later in `setup_guest_state()` right before jumping to guest code. Setting it earlier would corrupt glibc TLS access.

### PEB (Process Environment Block)
- Allocated via `mmap()` (1 page, RW)
- `PEB_IMAGE_BASE` (0x08) → points to mapped PE image
- `PEB_BEING_DEBUGGED` (0x02) → set to 0
- `PEB_PROCESS_HEAP` (0x30) → wine heap pointer
- `PEB_LDR` (0x18) → `PEB_LDR_DATA*` (linked lists of modules)

### Module Registration
```c
// In setup_teb_peb(), after PEB is set up:
loaded_module_t *mod = add_module(g_image_base, "main.exe", img_nt);
ldr_add_module(mod);
```
`add_module()` stores in `module_list[]` array (max `MAX_MODULES`), initializes `LDR_DATA_TABLE_ENTRY` with DllBase, EntryPoint, SizeOfImage, unicode FullDllName/BaseDllName.

`ldr_add_module()` inserts into three circular linked lists: `InLoadOrderModuleList`, `InMemoryOrderModuleModuleList`, `InInitializationOrderModuleList`.

---

## Overall Load Flow (main.c)

```
main()
  → init_loader()
    1. Parse MY_WINE_DEBUG from environ
    2. Cache WINE_DLL_PATH (before GS switch, so syscall-safe)
    3. map_image() — open PE, parse headers, copy sections, set protections
    4. get_image_sections()
    5. init_msvcrt_imports() — fill NULL entries in import_table
    6. init_import_table() — qsort for binary search
    7. patch_crt_refptrs() — fix .rdata refptr entries to point to our stubs
    8. resolve_imports() — Pass 1 (IAT) + Pass 2 (thunk patching)
    9. setup_teb_peb() — allocate TEB+PEB, init heap, register main module
    10. setup_stack() — allocate guest stack (min 512KB commit)
    11. Zero .data padding
    12. Pre-seed argc/argv/envp in .bss
    13. Build guest argv/envp from host args
    14. Look up main() symbol → use as entry point (or fallback to PE entry point)
  → run_guest_entry() — set GS base, switch to guest stack, jump to entry

```

---

## What Needs to Change for New DLLs

### Adding a New DLL (e.g., user32, gdi32, ddraw, dsound, winmm, dplay)

#### Required File Changes:

**1. `src/loader/import_table.c`** — Add entries:
```c
{ "user32.dll", "CreateWindowExA", (void*)CreateWindowExA },
{ "user32.dll", "ShowWindow", (void*)ShowWindow },
// ... etc
```
Also needs `#include "include/user32.h"` (or whatever header).

**2. `src/loader/ordinal_table.c`** — Add ordinal mappings (if the DLL uses them):
```c
{ "user32.dll", 1, "CreateWindowExA" },
// DOOM95 DPLAY uses ordinal #1 — this is how it's resolved
```

**3. New stub file** — e.g., `src/stubs/user32.c` or `src/msvcrt/user32.c`:
- Each function marked `__attribute__((ms_abi))`
- Must handle Windows calling convention args
- May call into `handler_Nt*()` or implement directly

**4. New header** — e.g., `include/user32.h`:
- Declare all stub functions with `__attribute__((ms_abi))`

**5. `include/nt_constants.h`** (if new syscalls needed):
- Add `#define NT_SYSCALL_NEW_CALL 0xXX`

**6. `include/nt_syscalls.def`** (if new syscalls needed):
- Add block with syscall number, handler, args, call template

**7. `src/msvcrt/ntdll_*.c`** (if new syscalls needed):
- Implement `handler_NtNewCall()` with `HANDLER` attribute
- Regenerate dispatcher: `python3 scripts/gen_dispatcher.py --generate`

**8. `src/syscall/thunk_gen.c`** (if new syscalls needed):
- Add to `nt_syscall_list[]` array

**9. `src/loader/import_resolve.c`** (if DLL can be dynamically loaded):
- Add to the "known stub library" check in `resolve_module_imports()`:
```c
if (dll_strcasecmp("user32.dll", dll_name) == 0 ||
    // ...)
```

#### What Does NOT Need to Change:
- **Build system** — Makefile auto-discovers `.c` files via `find`
- **Dispatcher** — only regenerated if new syscalls are added
- **PEB/LDR** — only the main PE is registered by default; new DLLs that aren't "real" PE files are handled via import table only

---

## Ordinal Import Support

**Yes, ordinal imports are supported.** The mechanism:
1. `ordinal_table.c` — static lookup table: `(dll_name, ordinal) → function_name`
2. During Pass 1 resolution, when `orig_thunks[i].AddressOfData & 0x8000000000000000ULL` (high bit = ordinal import):
   ```c
   uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
   const char *func_name = ordinal_lookup(dll_name, ordinal);
   if (func_name != NULL) {
       addr = resolve_import(dll_name, func_name);
   }
   ```
3. If ordinal is not in the table → import left unresolved (IAT slot stays 0)

**DOOM95 DPLAY ordinal #1**: Needs an entry like:
```c
{ "dplay.dll", 1, "DPCreate" },  // example — actual name depends on DOOM95
```
Both in `ordinal_table.c` AND `import_table.c`.

---

## Handle Management

### Handle Table (ntdll_priv.h)
```c
typedef struct {
    int        fd;        // -1 for non-file handles (events, mutexes, threads)
    uint8_t    used;
} handle_entry_t;

handle_entry_t handle_table[HANDLE_TABLE_SIZE]; // 256 entries
```

### Handle Types and Conventions
| Handle Value | Meaning |
|---|---|
| 0x7FFFFFFF | STDIN |
| 0x7FFFFFFE | STDOUT |
| 0x7FFFFFFD | STDERR |
| 0-2 | stdin/stdout/stderr file handles |
| 3+ | Dynamically allocated (files, events, mutexes, threads, sections) |

### Object Tables (all in ntdll_priv.h)
| Object | Array | Max | fd sentinel |
|---|---|---|---|
| Handles | `handle_table[256]` | 256 | fd >= 0 = file, fd == -1 = event/thread/mutex/section |
| Sections | `sections[64]` | 64 | handle = index+3 |
| Views | `views[64]` | 64 | found by base address scan |
| Events | `events[64]` | 64 | linked to handle slot |
| Mutexes | `mutexes[64]` | 64 | linked to handle slot |
| Threads | `threads[32]` | 32 | linked to handle slot |

**No type-safe handle management** — HANDLE is just `uint64_t`. The fd field in `handle_entry_t` acts as type indicator: `fd >= 0` = file, `fd == -1` = non-file object (event/mutex/thread). Object tables are indexed by `(handle - 3)`.

**Not thread-safe** — all tables have `SINGLE-THREAD ONLY` warnings in comments.

### Missing Handle Types for DOOM95
- **HWND** — not implemented (no window manager)
- **HDC** — not implemented (no GDI)
- **HINSTANCE/HMODULE** — `LoadLibraryA` returns NULL (stub, not implemented)
- **HBITMAP/HDC/HBITMAP** — not implemented (needed for GDI/Ddraw)

---

## Open Questions / Gaps

1. **No PE32 (32-bit) support** — all loader code assumes PE32+ (IMAGE_NT_HEADERS64). DOOM95 needs to be confirmed as PE32+ (x64). If it's PE32, the entire import table type (IMAGE_THUNK_DATA32 vs 64), PEB offsets, and thunk generation need adaptation.

2. **No window creation** — `CreateWindowEx`, `ShowWindow`, `SetWindowLong` etc. not stubbed. DOOM95 likely needs at least a minimal window handle for DDraw surface creation.

3. **No DDraw/Direct3D stubs** — DDraw is the primary rendering API for DOOM95. Requires either:
   - A full DDraw stub layer that maps to SDL2
   - Or a GDI fallback (if DOOM95 has one)

4. **No DirectSound stubs** — DSOUND needed for audio. Requires mapping to SDL2 audio.

5. **No DPlay stubs** — DPlay ordinal #1 needs resolution. If DOOM95 uses DPlay for networking, this needs implementation.

6. **Handle types incomplete** — HWND, HDC, HBITMAP, IDirectDraw, IDirectDrawSurface all need handle→object mapping tables.

7. **No real `LoadLibraryA` / `GetProcAddress`** — both return NULL. If DOOM95 dynamically loads any DLL at runtime (beyond the import table), this will crash.

8. **No `GetModuleHandleA` implementation** — returns NULL. Some games use this to check if a module is loaded.

9. **No registry access** — `Reg*` functions not stubbed. Some games check registry for config paths.

10. **MSVCRT is mingw-w64 specific** — the refptr patches are hardcoded for the specific mingw-w64 CRT layout. If DOOM95 is compiled with MSVC, the CRT initialization path is completely different.

