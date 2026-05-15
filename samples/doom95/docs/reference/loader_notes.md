# Loader Notes — What Needs to Change for New DLLs

> Status: historical Doom95 planning reference.
>
> This file is useful for imported API inventory, but some paths and limitations
> predate the current wrapper/backend PE32 runtime. Check `docs/architecture.md`,
> `docs/PE32.md`, and `audit/architecture-boundaries.md` before implementing
> from these notes.

## Adding a New DLL (e.g., user32, gdi32, ddraw, dsound, winmm, dplay)

### Required File Changes

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

**3. New stub file** — current stubs live under `src/msvcrt/`; historical
plans may mention `src/stubs/`, which no longer exists:
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
- Syscall definitions are driven from `include/nt_syscalls.def` and generated
  dispatcher code. Recheck current source before adding manual tables.

**9. `src/loader/import_resolve.c`** (if DLL can be dynamically loaded):
- Add to the "known stub library" check in `resolve_module_imports()`:
```c
if (dll_strcasecmp("user32.dll", dll_name) == 0 ||
    // ...)
```

### What Does NOT Need to Change

- **Build system** — Makefile auto-discovers `.c` files via `find`
- **Dispatcher** — only regenerated if new syscalls are added
- **PEB/LDR** — only the main PE is registered by default; new DLLs that aren't "real" PE files are handled via import table only

---

## Import Table Structure

The `import_table[]` is a **static, sentinel-terminated C array** (NULL/NULL/NULL terminator):

```c
typedef struct {
    const char *dll_name;   // e.g., "ntdll.dll", "kernel32.dll"
    const char *name;       // e.g., "NtWriteFile", "GetStdHandle"
    void *address;          // pointer to stub/handler function (NULL = dynamic)
} import_entry_t;
```

Three sections:
1. **ntdll.dll handlers** — `handler_Nt*()` functions (non-NULL, known at build time)
2. **kernel32.dll stubs** — C implementations (non-NULL)
3. **msvcrt.dll** — split into static (`__getmainargs`, `__iob_func`) and dynamic (`malloc`, `free` — NULL, filled at runtime)

`init_import_table()` sorts the table by name using `qsort()`. Resolution uses hand-rolled binary search (`resolve_import()` with `import_cmp_by_name()`).

---

## Import Resolution — Three-Tier Resolver

```
Tier 1: import_table[] lookup (binary search by name, ignoring DLL name)
  └─ If found AND address != NULL → return address
  └─ If found AND address == NULL → fall through (dynamic entry not yet filled)
Tier 2: loaded_module_t export table lookup (for real PE DLLs)
  └─ find_module_by_name(dll_name) → lookup_export(mod, func_name)
Tier 3: Not found → return NULL (leaves IAT slot as 0)
```

### Pass 1: IAT Resolution
Walks `IMAGE_DIRECTORY_ENTRY_IMPORT`:
1. For each `IMAGE_IMPORT_DESCRIPTOR`, read DLL name
2. For each thunk in `OriginalFirstThunk` (ILT):
   - **ordinal import** (high bit set): look up name via `ordinal_lookup()`, then `resolve_import()`
   - **name import**: read `IMAGE_IMPORT_BY_NAME->Name`, then `resolve_import()`
3. Write resolved address into `FirstThunk` (IAT)

### Pass 2: Thunk Patching
Scans `.text` for `ff 25` instructions → collects unique IAT targets. Applies 4 strategies to match each thunk target to a resolved import.

---

## ABI and Calling Convention Split

```c
#define WINE_STUB __attribute__((ms_abi, used))    // Microsoft x64 ABI
#define HANDLER __attribute__((used))               // System V ABI (called from C)
```

| Layer | ABI | Called By | Example |
|-------|-----|-----------|---------|
| **PE code** | Microsoft x64 (`ms_abi`) | Guest PE instructions | `kernel32.c` stubs, `msvcrt.c` stubs |
| **handler_*** | System V (default) | `dispatcher_generated.c` or kernel32 stubs | `ntdll_*.c` handlers |

**Path A: PE → ntdll.dll** → IAT thunk → `__wine_dispatcher` (asm) → `handler_Nt*()` (System V)
**Path B: PE → kernel32.dll** → IAT thunk → stub function (`ms_abi`) → calls `handler_Nt*()` directly (System V)
**Path C: PE → msvcrt.dll** → IAT thunk → `__msvcrt_*` function pointer → `wine_*` internal function

---

## Ordinal Import Support

**Yes, ordinal imports are supported.** Mechanism:
1. `ordinal_table.c` — static lookup table: `(dll_name, ordinal) → function_name`
2. During Pass 1, when high bit set: `ordinal_lookup(dll_name, ordinal)` → `resolve_import()`
3. If ordinal not in table → import left unresolved (IAT slot stays 0)

**DOOM95 DPLAY ordinal #1:** Needs entry `{ "dplay.dll", 1, "DPCreate" }` in both `ordinal_table.c` and `import_table.c`.

---

## Handle Management

Current handle table (ntdll_priv.h):

| Handle Value | Meaning |
|---|---|
| 0x7FFFFFFF | STDIN |
| 0x7FFFFFFE | STDOUT |
| 0x7FFFFFFD | STDERR |
| 0-2 | stdin/stdout/stderr file handles |
| 3+ | Dynamically allocated (files, events, mutexes, threads, sections) |

Object tables: `handle_table[256]`, `sections[64]`, `views[64]`, `events[64]`, `mutexes[64]`, `threads[32]`.

Current source has `include/handle_manager.h` and
`src/msvcrt/handle_manager.c`. Doom95-specific HWND/HDC/DDraw object modeling
is still not implemented by these historical notes.

### Missing Handle Types for DOOM95
- **HWND** — not implemented
- **HDC** — not implemented
- **HINSTANCE/HMODULE** — recheck current `LoadLibraryA`/module behavior
  against Doom95's needs
- **HBITMAP/HFONT/HPALETTE** — not implemented (needed for GDI/DDraw)
- **IDirectDraw/IDirectDrawSurface** — not implemented (needed for DDraw)

---

## Open Questions / Gaps

1. **PE32 breadth is incomplete** — `my_wine32` exists, but Doom95 is not a maintained passing sample
2. **No window creation** — `CreateWindowEx`, `ShowWindow`, `SetWindowLong` etc. not stubbed
3. **No DDraw/Direct3D stubs** — DDraw is the primary rendering API for DOOM95
4. **No DirectSound stubs** — DSOUND needed for audio
5. **No DPlay stubs** — DPlay ordinal #1 needs resolution
6. **Handle types incomplete** — HWND, HDC, HBITMAP, IDirectDraw, IDirectDrawSurface all need handle→object mapping tables
7. **Dynamic module support must be rechecked** — `LoadLibraryA`,
   `FreeLibraryA`, and `GetProcAddress` have current implementations, but
   Doom95's DLL/API needs may exceed them
8. **Module handle behavior must be rechecked** against current
   `kernel32_module.c`
9. **No registry access** — `Reg*` functions not stubbed
10. **MSVCRT is mingw-w64 specific** — refptr patches are hardcoded for mingw-w64 CRT layout
