# Wine vs my_wine: Architecture Differences

How Wine and my_wine approach the same problem (running Windows PE
executables on Linux) with fundamentally different strategies.

---

## 1. Scope

| Dimension | Wine | my_wine |
|-----------|------|---------|
| **Goal** | Full Windows compatibility layer | Minimal PE loader for research/education |
| **Size** | ~2 million lines of C | ~8,500 lines of C (~67 source files) |
| **CRT support** | Complete reimplementations of msvcrt, ucrtbase, vcruntime140, kernel32, ntdll, advapi32, user32, gdi32, … (100+ DLLs) | Minimal stubs for msvcrt, kernel32, ntdll — static import table (~60 entries) |
| **Target toolchains** | Any toolchain that produces valid PE (MSVC, mingw-w64, clang-mingw, Delphi, Borland, …) | mingw-w64 + GCC + msvcrt only |
| **Threading** | Full — pthreads mapped to Windows threads, per-thread TEB, TLS, APCs, jobs | Real threads via `clone()` syscall (CLONE_VM\|CLONE_FS\|CLONE_FILES), but shares parent's TEB (no per-thread TEB) |
| **Filesystem** | Full VFS with DOS device mapping, case-insensitive lookup, symlink translation, registry-backed paths | `NtOpenFile` with ASCII path conversion; handle table (256 entries, stdin/stdout/stderr pre-initialized at 0x7FFFFFFF/0x7FFFFFFE/0x7FFFFFFD) |
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
    /* ntdll — 16 NT syscall handlers */
    { "ntdll.dll", "NtWriteFile",         (void*)handler_NtWriteFile },
    { "ntdll.dll", "NtReadFile",          (void*)handler_NtReadFile },
    { "ntdll.dll", "NtClose",             (void*)handler_NtClose },
    { "ntdll.dll", "NtAllocateVirtualMemory", (void*)handler_NtAllocateVirtualMemory },
    { "ntdll.dll", "NtFreeVirtualMemory", (void*)handler_NtFreeVirtualMemory },
    { "ntdll.dll", "NtCreateSection",     (void*)handler_NtCreateSection },
    { "ntdll.dll", "NtMapViewOfSection",  (void*)handler_NtMapViewOfSection },
    { "ntdll.dll", "NtCreateThreadEx",    (void*)handler_NtCreateThreadEx },
    { "ntdll.dll", "NtCreateEvent",       (void*)handler_NtCreateEvent },
    { "ntdll.dll", "NtOpenFile",          (void*)handler_NtOpenFile },
    /* kernel32 — ~16 functions */
    { "kernel32.dll", "GetStdHandle",     (void*)GetStdHandle },
    { "kernel32.dll", "WriteFile",        (void*)WriteFile },
    { "kernel32.dll", "ExitProcess",      (void*)ExitProcess },
    /* msvcrt — startup + data + dynamic from host libc */
    { "msvcrt.dll", "__getmainargs",      (void*)__getmainargs },
    { "msvcrt.dll", "__iob_func",         (void*)__iob_func },
    { "msvcrt.dll", "fprintf",            NULL },  /* filled from host msvcrt.dll */
    { "msvcrt.dll", "malloc",             NULL },  /* filled from host msvcrt.dll */
    // ... ~60 entries total, all keyed to ntdll.dll / kernel32.dll / msvcrt.dll
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

**my_wine's approach (current):** Single `__wine_iob` union with 48-byte
`wine_FILE` entries (3 × 48 = 144 bytes total, stored as `char bytes[144]`).
Matches msvcrt.dll's FILE size exactly.

```c
// src/stubs/msvcrt_priv.h
typedef union {
    wine_FILE f[3];      // 3 × 48 bytes = 144 bytes
    char      bytes[144];
} iob_union;
```

If UCRT code reads beyond offset 48, it gets whatever happens to be in
memory.

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
layout (`.refptr`, `.bss` offsets, `__acrt_iob_func`
wrapper) to make this work. Wine doesn't — it just provides the DLL
the PE imports.

---

## 4. Syscall Interception

### 4.1 Wine: Dispatcher + wineserver IPC

Wine runs everything in a **single process**. Windows code executes on its
own stack, and when it needs kernel services, it calls through Wine's
dispatcher which either handles the call in-process or communicates with
`wineserver` (a separate process) for cross-process operations:

```
Windows app → Wine DLL → dispatcher → (in-process handler OR wineserver IPC) → Linux kernel
```

- A single process runs both the PE code and the Wine library code
- Each Windows thread has its own stack managed by Wine
- The dispatcher switches between Windows stacks and Wine stacks as needed
- `wineserver` (separate process) handles inter-process coordination via
  shared memory + signals
- This gives Wine full control over file handles, processes, memory, etc.

### 4.2 my_wine: Direct Dispatch via `__wine_dispatcher`

my_wine uses a **single process** with **no wineserver**: the PE
runs inline, and NT syscalls are intercepted through a direct C dispatcher:

```
Windows app → thunk → __wine_dispatcher (assembly) → c_dispatch_syscall (C) → handler → Linux syscall
```

- The PE runs in the same process — no fork, no separate child
- Each Windows thread has its own stack, managed by the loader
- NT syscalls from the PE land in thunks (generated at load time), which
  call `__wine_dispatcher()` (assembly, stack-switching entry point)
- `__wine_dispatcher` saves guest registers to the global `__wine_guest_regs`
  struct, switches to a pre-allocated UNIX stack, and calls
  `c_dispatch_syscall()` (C, reads from `__wine_guest_regs`)
- `c_dispatch_syscall` decodes the syscall number and dispatches to C
  handlers, which call Linux syscalls directly via `INLINE_SYSCALL_*` macros
- Linux syscalls pass through unchanged (not intercepted)
- No seccomp, no SIGSYS, no kernel module — just function calls

**Similarities to Wine:** single process, stack switching, direct call to
dispatcher.

**Differences from Wine:** my_wine has no `wineserver` (no separate IPC
process), no SUD (Syscall User Dispatch), and no full syscall table — only
the ~16 NT syscalls we explicitly implement. No wineserver means no
cross-process coordination; everything stays in-process.

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
- **Per-thread TEB** (Thread Environment Block) with its own SEH chain
- Per-thread TLS (Thread Local Storage) with proper initialization
- `arch_prctl(ARCH_SET_GS)` sets GS base per-thread
- APCs (Asynchronous Procedure Calls) implemented via signals

### 6.2 my_wine

Real threads with shared TEB:

- `NtCreateThreadEx` creates a **real thread** using `clone()` syscall
  with `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD` flags
  (via `INLINE_SYSCALL_CLONE` macro). This creates a Linux thread that
  shares the address space, file descriptors, and signal handlers with
  the parent.
- A `thread_wrapper()` function serves as the clone entry point — it
  unwraps the target function and argument, calls the thread function,
  then exits with `INLINE_SYSCALL_EXIT(0)`.
- **Shares the parent's TEB** — no per-thread TEB. All threads see the
  same GS base and TEB contents.
- Thread tracking: `wine_thread_t { tid, suspended }` with `MAX_THREADS = 32`
  and `threads[]` array in `src/stubs/ntdll_objects.c`.
- No per-thread SEH, no per-thread TLS
- GS base is set once (for the main thread only)

---

## 7. Current NT Syscall Coverage

my_wine implements **16 unique NT syscall handlers**:

| Category | Handlers |
|----------|----------|
| **Process** | `NtTerminateProcess`, `NtCallbackReturn`, `NtQueryInformationProcess` |
| **File I/O** | `NtWriteFile`, `NtReadFile`, `NtOpenFile`, `NtClose` |
| **Memory** | `NtAllocateVirtualMemory`, `NtFreeVirtualMemory` |
| **Sections/Views** | `NtCreateSection`, `NtMapViewOfSection`, `NtUnmapViewOfSection` |
| **Threading** | `NtCreateThreadEx`, `NtGetContextThread`, `NtSetContextThread` |
| **Events** | `NtCreateEvent` |

---

## 8. Summary: Design Philosophy

| | Wine | my_wine |
|---|------|---------|
| **Philosophy** | "Implement everything the PE might need" | "Implement the minimum to make this PE run" |
| **Process model** | Single process + wineserver (IPC) | Single process (no IPC) |
| **Dispatch** | Dispatcher → in-process handler or wineserver IPC | Thunk → `__wine_dispatcher` (asm) → `c_dispatch_syscall` (C) → handler → direct Linux syscall |
| **Approach** | Full API reimplementation per DLL | Static stub table (~60 entries) + direct dispatch |
| **Syscall table** | Full — every NT syscall has a handler | ~16 NT syscalls explicitly implemented |
| **Thread model** | Per-thread TEB, full TLS | Shared TEB (all threads share parent's TEB via CLONE_VM) |
| **Toolchain support** | Automatic (implements the DLL, not the toolchain) | Manual (add DLL names to the table) |
| **Handle table** | Full handle management via wineserver | 256-entry array, stdin/stdout/stderr at fixed special values |
| **GUI** | Full Win32 GUI | Console-only |
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
