# Wine vs my_wine: Architecture Differences

How Wine and my_wine approach the same problem (running Windows PE
executables on Linux) with fundamentally different strategies.

---

## 1. Scope

| Dimension | Wine | my_wine |
|-----------|------|---------|
| **Goal** | Full Windows compatibility layer | Minimal PE loader for research/education |
| **Size** | ~2 million lines of C | ~5,000 lines of C |
| **CRT support** | Complete reimplementations of msvcrt, ucrtbase, vcruntime140, kernel32, ntdll, advapi32, user32, gdi32, … (100+ DLLs) | Minimal stubs for msvcrt, kernel32, ntdll |
| **Target toolchains** | Any toolchain that produces valid PE (MSVC, mingw-w64, clang-mingw, Delphi, Borland, …) | mingw-w64 + GCC + msvcrt only |
| **Threading** | Full — pthreads mapped to Windows threads, with per-thread TEB/PEB, TLS, APCs, jobs | Stubbed — `NtCreateThreadEx` creates a pthread but shares the parent TEB |
| **Filesystem** | Full VFS with DOS device mapping, case-insensitive lookup, symlink translation, registry-backed paths | Console I/O only (`/dev/stdout`, `/dev/stderr`); minimal `NtOpenFile` with ASCII path conversion |
| **GUI** | Full Win32 GUI — X11/Wayland backend, DDraw, OpenGL, D3D | None — console-only |
| **License** | LGPL | Educational/research (no license) |

---

## 2. CRT Handling — The Biggest Difference

This is where the two approaches diverge most dramatically.

### 2.1 Wine: Full Per-DLL Reimplementation

Wine implements each Windows DLL as a **native Linux library** with its
own source tree:

```
dlls/msvcrt/          ← 120+ source files, full msvcrt.dll implementation
dlls/ucrtbase/        ← 80+ source files, full ucrtbase.dll implementation
dlls/vcruntime140/    ← 30+ source files, full vcruntime140.dll implementation
dlls/kernel32/        ← 100+ source files, full kernel32.dll implementation
dlls/ntdll/           ← 200+ source files, full ntdll.dll implementation
dlls/msvcp140/        ← C++ standard library
dlls/advapi32/        ← Registry, security, services
dlls/user32/          ← GUI, messages, windows
dlls/gdi32/           ← Graphics
... 100+ more
```

**When a PE imports `ucrtbase.dll!malloc`:** Wine's loader finds its own
`ucrtbase.dll` implementation and patches the IAT. The PE never knows the
difference.

**When a PE imports `msvcrt.dll!_iob`:** Same — Wine's `msvcrt.dll` has its
own `_iob` with Wine's `FILE` struct.

**Toolchain agnostic by design:** The PE's import table says what DLL it
needs; Wine implements that DLL. The toolchain (MSVC, mingw-w64, clang)
is irrelevant because **Wine implements every DLL the PE might reference**.

### 2.2 my_wine: Static Import Table with Minimal Stubs

my_wine maintains a **static C array** of known import names:

```c
// src/loader/import_table.c (simplified)
import_entry_t import_table[] = {
    { "ntdll.dll",    "NtWriteFile",         (void *)handler_NtWriteFile },
    { "ntdll.dll",    "NtTerminateProcess",  (void *)handler_NtTerminateProcess },
    { "kernel32.dll", "GetStdHandle",        (void *)GetStdHandle },
    { "kernel32.dll", "ExitProcess",         (void *)ExitProcess },
    { "msvcrt.dll",   "__getmainargs",       (void *)__getmainargs_stub },
    { "msvcrt.dll",   "__iob_func",          (void *)__iob_func_stub },
    { "msvcrt.dll",   "fprintf",             (void *)wine_fprintf },
    { "msvcrt.dll",   "malloc",              (void *)wine_malloc },
    // ... ~60 entries total, all keyed to msvcrt.dll / ntdll.dll / kernel32.dll
};
```

**When a PE imports `ucrtbase.dll!malloc`:** The lookup fails — there is
no `ucrtbase.dll` entry. The IAT stays at 0. Crash.

**Toolchain tied by design:** If the PE's imports reference a DLL name that
isn't in the table, the import is unresolved. **Adding support for a new
toolchain means adding new DLL entries to this table.**

### 2.3 The CRT FILE Struct Problem

Both Wine and my_wine must provide a `FILE` struct that matches what the
PE expects. But the size differs across CRTs:

| CRT | FILE Size | Notes |
|-----|-----------|-------|
| msvcrt.dll | 48 bytes | my_wine uses `WINE_FILE_SIZE = 48` — matches |
| ucrtbase.dll | 64-96 bytes (varies by version) | my_wine's 48-byte `FILE` is too small |

**Wine's solution:** Each Wine DLL implements its own `FILE` struct
matching the expected size. `msvcrt.dll` uses 48 bytes; `ucrtbase.dll`
uses the correct larger size.

**my_wine's approach (current):** Single `__wine_iob` array with 48-byte
entries. If UCRT code reads beyond offset 48, it gets whatever happens to
be in memory.

**Quick fix for UCRT:** Zero-pad each `__wine_iob` entry to 96 bytes.
Most UCRT code only reads within the first 48 bytes, and the rest being 0
is safe for the common case.

---

## 3. CRT Startup Sequence

### 3.1 Wine

Wine intercepts the PE at the loader level. When the PE's entry point is
`mainCRTStartup`, Wine's CRT DLL **handles the entire startup**:

```
mainCRTStartup (in Wine's msvcrt.dll or ucrtbase.dll)
  └─ calls Wine's __getmainargs or __scrt_initialize_crt
     └─ sets up argc/argv/envp, locale, iob
     └─ calls user's main()
```

The PE's CRT code is largely bypassed — Wine's CRT DLL is what actually
runs.

### 3.2 my_wine

my_wine **runs the PE's own CRT code** but intercepts its imports:

```
mainCRTStartup (in the PE itself)
  └─ calls __getmainargs (import → our stub)
     └─ our stub seeds .bss with argc/argv/envp pointers
     └─ calls _initterm (import → our stub)
        └─ our stub runs __CTOR_LIST__
        └─ calls user's main()
```

**Critical implication:** my_wine must understand the CRT's internal data
layout (`.refptr`, `.bss` offsets, `__acrt_iob_func` wrapper) to make
this work. Wine doesn't — it just provides the DLL the PE imports.

---

## 4. Syscall Interception

### 4.1 Wine: Single Process with Stack Switching

Wine runs everything in a **single process**. Windows code executes on its
own stack, and when it needs kernel services, it calls through Wine's
dispatcher which either handles the call in-process or communicates with
`wineserver` (a separate process) for cross-process operations:

```
Windows app → Wine DLL → dispatcher → (in-process handler or wineserver IPC) → Linux kernel
```

- A single process runs both the PE code and the Wine library code
- Each Windows thread has its own stack managed by Wine
- The dispatcher switches between Windows stacks and Wine stacks as needed
- `wineserver` (separate process) handles inter-process coordination via
  shared memory + signals
- This gives Wine full control over file handles, processes, memory, etc.

### 4.2 my_wine: Direct Dispatch via `__wine_dispatcher`

my_wine uses a **single process** approach similar to Wine: the PE runs
inline, and NT syscalls are intercepted through a direct C dispatcher
rather than kernel-level traps:

```
Windows app → __wine_dispatcher() → C handler → Linux kernel
Linux syscall → passes through directly → Linux kernel
```

- The PE runs in the same process — no fork, no separate child
- Each Windows thread has its own stack, managed by the loader
- NT syscalls from the PE land in `__wine_dispatcher()` via the IAT or
  inline redirection, which dispatches directly to C handlers
- Linux syscalls pass through unchanged (not intercepted)
- No seccomp, no SIGSYS, no kernel module — just function calls

**Similarities to Wine:** single process, stack switching, direct call to
dispatcher. The control flow is now much closer to Wine's model.

**Differences from Wine:** my_wine has no `wineserver` (no separate IPC
process), no SUD (Syscall User Dispatch), and no full syscall table — only
the handful of NT syscalls we explicitly implement.

---

## 5. Module Loading

### 5.1 Wine

Wine has a full module loader with DLL caching, preloading, and
Wine-specific metadata. Each DLL can be:

- **Native** — implemented in C (most DLLs)
- **Builtin** — compiled into the Wine binary
- **Preloaded** — loaded before the PE runs
- **PE DLL** — actual Windows DLL loaded via the same PE loader

### 5.2 my_wine

my_wine currently loads **one PE only** (the main executable). No dynamic
DLL loading (`LoadLibraryA` returns NULL). No module list. No PEB Ldr.

**Path to dynamic loading** (see `limitations_investigations.md §2`):

1. Module list (`loaded_module_t` with DLL name, base, exports)
2. Export table parsing (`IMAGE_EXPORT_DIRECTORY`)
3. Three-tier import resolution (stub table → module exports → recursive load)
4. PEB Ldr population

---

## 6. Thread Model

### 6.1 Wine

Full thread implementation:

- Each Windows thread → Linux pthread
- Per-thread TEB (Thread Environment Block) with its own SEH chain
- Per-thread TLS (Thread Local Storage) with proper initialization
- `arch_prctl(ARCH_SET_GS)` sets GS base per-thread
- APCs (Asynchronous Procedure Calls) implemented via signals

### 6.2 my_wine

Stubbed threading:

- `NtCreateThreadEx` creates a pthread but shares the **parent's TEB**
- No per-thread SEH, no per-thread TLS
- GS base is set once (for the main thread only)
- Signal handler can't distinguish which thread's context it's in

---

## 7. Summary: Design Philosophy

| | Wine | my_wine |
|---|------|---------|
| **Philosophy** | "Implement everything the PE might need" | "Implement the minimum to make this PE run" |
| **Process model** | Single process + wineserver (IPC) | Single process (no separate IPC process) |
| **Dispatch** | Stack switching + dispatcher + wineserver | Stack switching + direct dispatcher (no wineserver) |
| **Approach** | Full API reimplementation per DLL | Static stub table + direct dispatch |
| **Syscall table** | Full — every NT syscall has a handler | Minimal — only the NT syscalls we implement |
| **Toolchain support** | Automatic (implements the DLL, not the toolchain) | Manual (add DLL names to the table) |
| **Code per feature** | Thousands of lines per DLL | Dozens of lines per stub |
| **Maintenance burden** | High (many features, many edge cases) | Low (small surface area) |
| **When to use** | Production, real applications | Research, education, experimentation |

### The Key Insight

**Wine abstracts away the toolchain by implementing every DLL.** The PE
imports `ucrtbase.dll`, Wine has `ucrtbase.dll`, done.

**my_wine ties itself to the toolchain by stubbing specific imports.** The
PE imports `ucrtbase.dll`, my_wine's table has no entry, failure.

To lift this limitation in my_wine, you don't need to reimplement every
DLL — you just need to:

1. **Add DLL names** to the import table (`ucrtbase.dll`, `vcruntime140.dll`)
2. **Reuse existing stubs** (most UCRT functions are the same as msvcrt)
3. **Handle CRT startup differences** (`__scrt_*` vs `__getmainargs`)
4. **Skip `.refptr` patching** for MSVC binaries (they use `__local_base` instead)

This is what the toolchain compatibility investigation (`limitations_investigations.md §5`)
recommends.

---

## Related Documents

- [Architecture](architecture.md) — How my_wine loads and runs PE binaries
- [Rationale](rationale.md) — Why my_wine makes its architectural choices
- [Limitations Investigation](limitations_investigations.md) — How to lift each limitation
- [CRT refptr Patching](refptr.md) — .refptr deep-dive
- [PE Format Primer](pe_format.md) — PE structure basics
- [Onboarding](onboarding.md) — Getting started guide
