# CRT Handling — Reference

C runtime support for MinGW-w64 and Watcom PE images. Handles detection,
`.refptr` patching, BSS seeding, startup function stubs, and FILE emulation.

---

## Overview

The CRT system intercepts the C runtime that the PE image expects at load time.
Without intervention, a MinGW or Watcom PE will call into its own CRT startup code,
which dereferences `.refptr` entries pointing to host libc data that doesn't exist
in our process — immediate SIGSEGV.

The CRT system has two layers:

| Layer | Location | Responsibility |
|-------|----------|----------------|
| **Module registry** | `src/crt/crt.c` | Pluggable module dispatch (MinGW / Watcom) |
| **MSVCRT stubs** | `src/msvcrt/crt_*.c` | Globals, stdio, stdlib, startup, refptr patching |

```
src/crt/
├── crt.c                    # Module registry, public API
├── crt_mingw.c              # MinGW-w64 module implementation
├── crt_watcom.c             # Watcom module implementation (DOOM95)
└── crt_priv.h               # struct crt_module (full vtable definition)

src/msvcrt/
├── crt_globals.c             # g_crt singleton + self-referential fixups
├── crt_refptrs.c             # .refptr patching orchestration (fallback + apply_refptr_patch)
├── crt_offset_discovery.c    # COFF symbol lookup + .text scanning for refptr targets
├── crt_startup.c             # __getmainargs, _initterm, _initterm_e, _m_* ABI wrappers
├── crt_stdio.c               # wine_vfprintf, wine_fprintf, wine_fwrite
├── crt_file.c                # __iob_func, __acrt_iob_func, __wine_iob_data
├── crt_misc.c                # ___lc_codepage_func, _errno, _lock/_unlock, fputc, localeconv
└── crt_stdlib.c              # wine_malloc/free/realloc, _amsg_exit, _cexit, wine_abort
```

### The g_crt Singleton

`wine_crt_state_t g_crt` (declared in `include/crt.h`, defined in
`src/msvcrt/crt_globals.c`) holds all CRT state in one struct:

- `crt_context_t crt_ctx` — image base, .bss VA, argc/argv/envp offsets
- `iob_union iob` — three `wine_FILE` structs (stdin/stdout/stderr, 48 bytes each)
- Constructor/destructor stubs (`ctor_list_stub`, `dtor_list_stub`, `xi_a_stub`, etc.)
- CRT globals (`app_type`, `commode`, `fmode`, `acmdln`, `initenv`, etc.)

All `src/msvcrt/crt_*.c` files include `src/msvcrt/msvcrt_priv.h` which declares
`g_crt` as extern and provides shared internal function declarations.

---

## CRT Detection

`crt_detect_type()` in `src/crt/crt.c` determines which CRT module to use.

### Detection Flow

1. Iterate all registered modules (`crt_module_mingw`, `crt_module_watcom`) and call
   their `detect` vtable function
2. If no module matches, use a heuristic fallback:
   - **PE32** → look for `D_DoomMain` / `_D_DoomMain` in COFF symbol table → Watcom
   - **PE32+** → assume MinGW
3. Default fallback: MinGW

### MinGW Detection (`src/crt/crt_mingw.c`)

Looks for MinGW-w64 marker symbols in the COFF symbol table:

| Marker | Meaning |
|--------|---------|
| `__CTOR_LIST__` | Constructor list array start |
| `__xi_a` | Initialization range start |
| `__xc_a` | Constructor range start |

If any marker is found via `find_symbol_rva_from_file()`, the image is MinGW-w64.

### Watcom Detection (`src/crt/crt_watcom.c`)

Strong indicators (any one is sufficient):

| Indicator | Description |
|-----------|-------------|
| `.mmh` section | Watcom-specific heap metadata section |
| `BEGTEXT` + `DGROUP` sections | Watcom code/data section naming convention |
| `D_DoomMain` / `_D_DoomMain` in COFF | DOOM95 entry point symbol |
| `_cstartup` / `_startup` in COFF | Watcom CRT entry symbols |

The detection also has DOOM95-specific checks (`is_doom95_image()` checks filename,
`looks_like_doom95_layout()` checks entry RVA == 0x444d8 and image size == 0x290000).

### Module Registry

`src/crt/crt.c` maintains a static array `modules[]` and the global `active_crt` pointer.
The registry provides accessor functions (`crt_get_module()`, `crt_get_active()`,
`crt_entry_symbols()`, `crt_get_refptr_mappings()`) that route through the vtable.

---

## .refptr Patching

### Background

MinGW-w64 uses `.refptr` — a linker feature that creates a data entry whose value
is the *address of a symbol* (not the symbol's value). The PE loads these as GOT-like
entries. When our loader maps the PE into a Linux process, these entries still point
to addresses inside the PE's own image or to host libc symbols that don't exist.

Without patching, the MinGW CRT startup code dereferences these entries and crashes.

### Architecture

```
                    patch_crt_refptrs()
                          │
          ┌───────────────┼─────────────────┐
          ▼               ▼                  ▼
   CRT module?    CRT module with        No module
   with vtable    fallback table?        → return
          │               │
          ▼               ▼
   mod->patch_refptrs   Inline fallback
   (module-specific)     in crt_refptrs.c
```

`patch_crt_refptrs()` in `src/msvcrt/crt_refptrs.c` is the entry point called from
`src/main.c`. If the active CRT module has a `patch_refptrs` vtable entry, it delegates
entirely to the module. Otherwise, it uses the inline fallback logic (the original
pre-module implementation).

### Patching Phases

Both the MinGW module (`src/crt/crt_mingw.c`) and the inline fallback
(`src/msvcrt/crt_refptrs.c`) follow the same three-phase approach:

#### Phase 1: COFF Symbol Table

For each entry in `mingw_refptr_mappings[]` / `refptr_mappings[]`, look up the
symbol in the PE's COFF symbol table via `find_symbol_rva_from_file()`.
If found, call `apply_refptr_patch()` to overwrite the refptr.

Matching strategies in `find_matching_symbol()` (`src/msvcrt/crt_offset_discovery.c`):

| Strategy | Pattern |
|----------|---------|
| `.rdata$.refptr.<name>` | Full section-qualified name |
| `.refptr.<name>` | Short refptr prefix |
| Exact match | Symbol name equals target name |
| Substring fallback | Target name appears anywhere in symbol name |

Section-bound symbols are preferred over absolute (section 0) symbols, since
MinGW-w64 sometimes has garbage absolute symbols with wrong values.

#### Phase 2: Data Section Scan (Fallback)

For MinGW builds that don't include certain CRT symbols in the COFF table
(only in DWARF), scan `.rdata` and `.data` sections for 8-byte values that
point into `.bss`. When found, match against the next unpatched mapping
(skip `__CTOR`/`__DTOR`/`__xi_`/`__xc_` entries which belong in `.CRT`).

#### Phase 3: .text Scanning

`scan_text_for_refptrs()` in `src/msvcrt/crt_offset_discovery.c` scans the
code section for the x86-64 instruction pattern:

```
  48 8B 05 disp32    ; mov rax, [rip+disp32]  — two-level indirection
  48 8B 00           ; mov rax, [rax]          — dereference
  89 00 or C7 00     ; write to [rax]          — store result
```

This pattern is the signature of `__imp___initenv` usage (deref + write).
The function distinguishes it from callback patterns (deref + test + call).

### apply_refptr_patch()

`apply_refptr_patch()` in `src/msvcrt/crt_refptrs.c` performs a single patch:

1. Validates the target RVA is within the image
2. Computes the page boundary and determines the correct `restore_prot` from
   the containing section's characteristics (critical for `.idata` which is R+W)
3. Calls `with_mprotect_rw()` to temporarily make the page writable
4. Overwrites the 8-byte refptr with the target address
5. Restores the original protection (read from section characteristics, not hardcoded)

The protection restoration is important: `.idata` must be restored to `PROT_READ|PROT_WRITE`
so that `resolve_imports()` can later write IAT entries. Hardcoding `PROT_READ` would
leave `.idata` read-only and crash import resolution.

### Refptr Mapping Table

`src/msvcrt/crt_refptrs.c` defines the shared `refptr_mappings[]` table (also
duplicated in `src/crt/crt_mingw.c` as `mingw_refptr_mappings[]`):

| Symbol | Target in g_crt |
|--------|-----------------|
| `__CTOR_LIST__` | `ctor_list_stub[0]` |
| `__DTOR_LIST__` | `dtor_list_stub[0]` |
| `__xi_a` / `__xi_z` | `xi_a_stub` / `xi_z_stub` |
| `__xc_a` / `__xc_z` | `xc_a_stub` / `xc_z_stub` |
| `__image_base__` | `crt_ctx.image_base` |
| `__imp___initenv` | `imp_initenv_stub` |
| `__imp__acmdln` | `acmdln` |
| `_commode`, `_fmode`, `_newmode` | `commode` / `fmode` / `newmode` |
| `mingw_app_type` | `app_type` |
| `__native_startup_lock/state` | `native_startup_lock` / `native_startup_state` |
| `_dowildcard` | `dowildcard` |
| `__dyn_tls_init_callback` | `dyn_tls_callback_stub` |
| `__mingw_oldexcpt_handler` | `mingw_excpt_handler_stub` |

All targets are zero-initialized stubs or g_crt fields that the loader controls.

### Initialized Flag

After patching, `initialized` at `.bss + 0x30` is set to 1. This skips
`__do_global_ctors` during CRT startup — without it, `__main()` calls
`__do_global_ctors()` which iterates `__DTOR_LIST__` and can crash on
unpatched refptrs.

---

## CRT Globals

### Initialization (`src/msvcrt/crt_globals.c`)

`g_crt` is defined with inline initializers:

- `iob.f[0]` (stdin): `_fd=0`, `_flag=IOREAD|IONBF`
- `iob.f[1]` (stdout): `_fd=1`, `_flag=IOWRT|IONBF`
- `iob.f[2]` (stderr): `_fd=2`, `_flag=IOWRT|IONBF`

A `__attribute__((constructor))` function `crt_init_self_refs()` runs before
`main()` to fix self-referential pointers: `g_crt.acmdln` and `g_crt.p_acmdln`
must point to `g_crt.cmdline_storage`, which can't be expressed in a compile-time
initializer.

### Function-wrapper globals

`__p__acmdln_func()`, `__initenv_func()`, `__p__fmode_func()`,
`__p__commode_func()` return pointers to g_crt fields as *functions* (not data),
because MinGW CRT imports them via JMP thunks. If the IAT contained a data address,
the CPU would execute it as instructions → SIGSEGV. These are excluded under
`MY_WINE32` because `crt_32_stub.c` provides its own implementations.

### CRT Offset Discovery (`src/msvcrt/crt_offset_discovery.c`)

`discover_crt_offsets()` finds the .bss offsets of `_argc`/`_argv`/`_environ`
via COFF symbol table lookup (trying both decorated and undecorated names:
`_argc`/`__argc`, `_argv`/`__argv`, `_environ`/`__envp`). Falls back to the
CRT module's hardcoded offsets, then to hardcoded fallbacks:

| Variable | Offset from .bss base |
|----------|----------------------|
| `_argc` | `0x028` |
| `_argv` | `0x020` |
| `_environ` | `0x018` |
| `_acmdln` | `0x030` |

### BSS Seeding

Both modules (`src/crt/crt_mingw.c` and `src/crt/crt_watcom.c`) implement
`seed_bss()` which writes `argc=1`, `argv=pe32_argv_ptr()` / `envp=pe32_envp_ptr()`
(or NULL for PE32+) into the PE's `.bss` section at the discovered offsets.
Requires `mprotect()` to make `.bss` writable first.

`src/main.c` calls `crt_seed_bss()` via the module vtable (or falls back to
`seed_bss_vars()` inline) after `patch_crt_refptrs()` has populated `g_crt.crt_ctx`.

---

## CRT I/O

### `__iob_func` and `__acrt_iob_func` (`src/msvcrt/crt_file.c`)

Both return `g_crt.iob.bytes` — the base of the three contiguously packed
`wine_FILE` structs (48 bytes each, 144 bytes total).

PE32 `__acrt_iob_func` has a known broken implementation (garbage `rcx` upper
bits after index manipulation). The entry point patching in `src/loader/entry.c`
replaces the broken math with a direct return of `__wine_iob_data()`.

### Stdio Functions (`src/msvcrt/crt_stdio.c`)

| Function | Implementation | Notes |
|----------|----------------|-------|
| `wine_vfprintf` | Writes format string directly (no `vsnprintf` — crashes on garbage va_list) | Uses `INLINE_SYSCALL_WRITE` |
| `wine_fprintf` | Calls `wine_vfprintf` via va_list | |
| `wine_fwrite` | `INLINE_SYSCALL_WRITE(fd, ptr, total)` | Avoids callee-save SSE spills |

Stream validation: the FILE pointer is checked against `g_crt.iob.bytes` to
ensure it's one of our three structs (stdin/stdout/stderr, fd 0-2).

### Stdio in `crt_misc.c`

`wine_fputc()` in `src/msvcrt/crt_misc.c` writes a single character via
`INLINE_SYSCALL_WRITE(fd, &c, 1)`. Also exports `__msvcrt_fputc`.

---

## CRT Stdlib (`src/msvcrt/crt_stdlib.c`)

| Function | Implementation | Notes |
|----------|----------------|-------|
| `wine_malloc` | `sysv_malloc(size)` | System V ABI wrapper around libc malloc |
| `wine_free` | `sysv_free(ptr)` | |
| `wine_realloc` | Allocates new, copies, frees old | Old size unknown — conservatively copies new size |
| `wine_memcpy` | `sysv_memcpy` | |
| `wine_exit` | `INLINE_SYSCALL_EXIT(code)` | Direct syscall |
| `wine__exit` | Same as `wine_exit` | |
| `wine_abort` | Dumps RSP/RBP/return address to stderr via syscall, then `INLINE_SYSCALL_EXIT(SIGABRT)` | |
| `wine_signal` | Returns `SIG_ERR` | Signal handling not needed post-GS switch |

`_amsg_exit()` and `_cexit()` are called from CRT startup error paths.
`_amsg_exit()` formats the error message inline (no sprintf — avoids libc)
and calls `wine__exit(1)`. `_cexit()` calls `wine__exit(0)`.

---

## CRT Startup Functions (`src/msvcrt/crt_startup.c`)

### `__getmainargs`

Writes `argc=1`, `argv`, `envp` to the PE's `.bss` section at the offsets
discovered during refptr patching. Writes pointer sizes matching the PE type:
4 bytes for PE32, 8 bytes for PE32+. Also writes to `g_crt.guest_argv` /
`g_crt.guest_envp` for other stubs to read.

### `_initterm` and `_initterm_e`

No-op stubs. `_initterm_e` returns `NULL` via `FORCE_PTR_RETURN(NULL)`
(never return a bare NULL pointer — that would crash as code if called via JMP).

### `_m_*` Wrappers

`_m_malloc`, `_m_free`, `_m_calloc`, `_m_realloc`, `_m_memcpy`, `_m_memset`,
`_m_strlen`, `_m_strcmp`, `_m_strncmp`, `_m_memcmp`, `_m_abort`, `_m_exit`,
`_m_signal`, `_m_wcslen`, `_m_localeconv`, `_m_strerror`, `_m_fprintf`,
`_m_fwrite`, `_m_vfprintf`, `_m_fputc` — all declared with `__attribute__((ms_abi))`
to match the Microsoft x64 calling convention. They use `__builtin_*` or direct
libc calls (system V ABI is fine on the *caller* side for these wrappers).

### Other Startup Stubs

| Function | Behavior |
|----------|----------|
| `__set_app_type` | Sets `g_crt.app_type` |
| `_onexit` | No-op, returns NULL via `FORCE_PTR_RETURN` |
| `__p__commode` / `__p__fmode` | Returns `&g_crt.commode` / `&g_crt.fmode` |
| `_setargv` | Returns NULL via `FORCE_PTR_RETURN` |
| `__lconv_init` | No-op |
| `__setusermatherr` | No-op |
| `_errno` | Returns `&__my_wine_errno` (static local in crt_startup.c) |

---

## CRT Misc (`src/msvcrt/crt_misc.c`)

| Function | Return | Notes |
|----------|--------|-------|
| `___lc_codepage_func` | `65001` (UTF-8) | Internal MSVCRT code page query |
| `___mb_cur_max_func` | `6` | Max bytes per multibyte char for UTF-8 |
| `_errno_func` | `&g_msvcrt_errno` | Thread-local errno storage |
| `_lock` / `_unlock` | No-op | CRT internal locking |
| `wine_localeconv` | `&g_msvcrt_lconv` | US/English locale defaults |
| `wine_strerror` | Static string table | Avoids glibc `strerror` which accesses vDSO via GS |
| `_m_atoi` | Pure-C atoi | `__attribute__((ms_abi))` |
| `_m_strchr` | Pure-C strchr | |
| `_m_setlocale` | Returns `"C"` | |
| `wine_wcslen` | Pure-C wcslen | |

---

## Watcom CRT

### Differences from MinGW

The Watcom CRT (`src/crt/crt_watcom.c`) has a much simpler patching model:

- **`watcom_refptr_mappings[]` is empty** — no refptr entries to patch
- **Detection uses section names** (`.mmh`, `BEGTEXT`, `DGROUP`) and entry symbols
  (`D_DoomMain`, `_cstartup`)
- **Offset discovery** uses the same COFF lookup + hardcoded fallback pattern as MinGW

### DOOM95 Runtime Seeding

`watcom_seed_doom95_runtime()` in `src/crt/crt_watcom.c` sets up DOOM95-specific
runtime slots that the Watcom CRT expects:

| RVA | Purpose |
|-----|---------|
| `0x218358` | Standard handle count (set to 3) |
| `0x21835c` | Pointer to handle table (STDIN=0, STDOUT=1, STDERR=2) |
| `0x077d84` | Trap flag (set to 0) |

The handle table is allocated via `mmap(MAP_ANONYMOUS)` and populated with
`STDIN_HANDLE`, `STDOUT_HANDLE`, `STDERR_HANDLE` values.

### Watcom Entry Symbols

`watcom_entry_symbols[]`: `D_DoomMain`, `_D_DoomMain`, `main`.
CRT startup symbols like `_cstartup`/`_startup` are detection markers only —
jumping to them would re-enter CRT code with a user-entry stack frame.

---

## PE32 vs PE32+ CRT Differences

| Aspect | PE32 (my_wine32) | PE32+ (my_wine64) |
|--------|------------------|-------------------|
| CRT stub source | `src/msvcrt/crt_32_stub.c` | `src/msvcrt/crt_*.c` |
| Pointer size in .bss | 32-bit (`uint32_t`) | 64-bit (`uint64_t`) |
| `__p__acmdln_func` etc. | Provided by `crt_32_stub.c` | Provided by `crt_globals.c` (excluded via `#ifndef MY_WINE32`) |
| Calling convention | `__attribute__((used))` (no ms_abi on 32-bit) | `__attribute__((ms_abi))` for import-table functions |
| BSS seeding for argv/envp | `pe32_argv_ptr()` / `pe32_envp_ptr()` | Set to 0 |

`crt_32_stub.c` is used only in the PE32 child binary (`my_wine32`). The 64-bit
build uses the full `src/msvcrt/crt_*.c` module set. The distinction exists
because musl's ifunc resolution causes issues in the 32-bit standalone build.

---

## Entry Point Flow (`src/main.c`)

```
1. crt_detect_type(argv[1], &nt)        → detects MinGW or Watcom
2. crt_set_active(mod)                   → sets global module
3. init_import_table()                   → populates IAT function pointers
4. patch_crt_refptrs(argv[1], base, ...) → patches .refptr entries, discovers offsets
5. resolve_imports(base, &nt)            → resolves DLL imports
6. crt_seed_bss(active, base, ...)       → writes argc/argv/envp into .bss
```

Steps 1-3 happen before the TEB/PEB setup and stack switch. Steps 4-6
complete the CRT preparation before jumping to the PE entry point.
