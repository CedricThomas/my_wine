# Limitations Investigation

Every known limitation of the current my_wine implementation, with deep-dive
investigations on how each could be lifted.

Each section contains:
- **Current state** — what is blocked / incomplete
- **Investigation** — approaches, trade-offs, and implementation strategy
- **Complexity** — Low / Medium / High / Very High
- **Prerequisites** — what must be done first
- **Risks** — what could break or introduce new limitations

---

## Table of Contents

| # | Limitation | Complexity | Prerequisites |
|---|-----------|------------|---------------|
| [1](#1-no-relocation-support) | No Relocation Support | Medium | None |
| [2](#2-no-dynamic-loading) | No Dynamic Loading | High | None |
| [3](#3-no-tls-support) | No TLS Support | Medium | None |
| [4](#4-synchronization-primitives) | Synchronization Primitives | ✅ Implemented | Events (syscall) |
| [5](#5-only-mingw-w64-executables) | Only mingw-w64 Executables | High | Heap (§7) |
| [6](#6-limited-syscall-handlers) | Limited Syscall Handlers | High | None |
| [7](#7-heap-management) | Heap Management | ✅ Implemented | CriticalSection (§4) |
| [8](#8-limited-filesystem-io) | Limited Filesystem I/O | High | Syscall handlers (§6) |
| [9](#9-shared-teb-threading) | Shared-TEB Threading Model | High | Per-thread TEB (§4) |

### Current Capabilities Summary

The project currently has **25 NT syscall handlers**, **handle table with 256
entries** (stdin/stdout/stderr pre-initialized), **thread creation via
NtCreateThreadEx** (clone with shared address space), **real CRITICAL_SECTION**
(CAS fast-path + event slow-path), **heap management** (dlmalloc backend with
HeapCreate/Alloc/Free/ReAlloc/Destroy/Size, PEB.ProcessHeap wired), and **full
CRT startup chain** (mainCRTStartup → __getmainargs → _initterm → main).

**Implemented syscalls** (from `include/nt_constants.h`):

| Syscall | NT # | Status |
|---------|------|--------|
| `NtCallbackReturn` | `0x05` | ✅ Working |
| `NtQueryInformationProcess` | `0x07` | ✅ Working (ProcessBasicInformation, ProcessWorkingSetSize) |
| `NtClose` | `0x0F` | ✅ Working |
| `NtAllocateVirtualMemory` | `0x18` | ✅ Working (mmap-based) |
| `NtFreeVirtualMemory` | `0x19` | ✅ Working (munmap-based) |
| `NtGetContextThread` | `0x24` | ✅ Stub (zeros CONTEXT struct) |
| `NtSetContextThread` | `0x26` | ✅ Stub (no-op) |
| `NtMapViewOfSection` | `0x28` | ✅ Working (file-backed and anonymous) |
| `NtUnmapViewOfSection` | `0x29` | ✅ Working |
| `NtTerminateProcess` | `0x2A` | ✅ Working (INLINE_SYSCALL_EXIT) |
| `NtReadFile` | `0x3C` | ✅ Working (handle→FD, direct read) |
| `NtWriteFile` | `0x3D` | ✅ Working (handle→FD, direct write) |
| `NtCreateEvent` | `0x48` | ✅ Working (with NtSetEvent/NtResetEvent) |
| `NtCreateMutex` | `0x44` | ✅ Working (pthread_mutex) |
| `NtCreateSection` | `0x4A` | ✅ Working (file-backed and anonymous) |
| `NtCreateThreadEx` | `0x4E` | ✅ Working (clone() with CLONE_VM\|CLONE_FS\|CLONE_FILES\|CLONE_SIGHAND\|SIGCHLD) |
| `NtDelayExecution` | `0x1A` | ✅ Working (nanosleep) |
| `NtOpenFile` | `0x4F` | ✅ Working (UTF-16→UTF-8 ASCII conversion, handle table integration) |
| `NtQueryPerformanceCounter` | `0x55` | ✅ Working (clock_gettime CLOCK_MONOTONIC) |
| `NtQueryPerformanceFrequency` | `0x56` | ✅ Working (constant return) |
| `NtQuerySystemTime` | `0x09` | ✅ Working (clock_gettime CLOCK_REALTIME) |
| `NtReleaseMutex` | `0x1E` | ✅ Working (pthread_mutex_unlock) |
| `NtResetEvent` | `0x5E` | ✅ Working (clear signaled flag) |
| `NtSetEvent` | `0x5C` | ✅ Working (pthread_cond_signal) |
| `NtWaitForSingleObject` | `0x03` | ✅ Working (pthread_cond_wait) |

**Implemented kernel32 stubs:**
GetStdHandle, WriteFile, ReadFile, ExitProcess, Sleep, VirtualProtect,
VirtualQuery, lstrlenA, InitializeCriticalSection, EnterCriticalSection
(CAS fast-path + event slow-path), LeaveCriticalSection, DeleteCriticalSection,
GetLastError, TlsGetValue, GetStartupInfoA, SetUnhandledExceptionFilter,
__C_specific_handler, GetProcAddress, LoadLibraryA, GetModuleHandleA,
SetEvent, ResetEvent, WaitForSingleObject, CreateMutex, ReleaseMutex,
HeapCreate, HeapAlloc, HeapFree, HeapReAlloc, HeapDestroy, GetProcessHeap, HeapSize.

**Implemented msvcrt/CRT stubs:**
__getmainargs, __iob_func, fprintf, fwrite, malloc, calloc, free, memcpy,
strlen, strncmp, exit, _exit, abort, signal, _amsg_exit, _cexit, __set_app_type,
__initenv, _initterm, _initterm_e, _onexit, __p__commode, __p__fmode, _setargv,
__lconv_init, __setusermatherr.

### Recommended Implementation Order

By dependency graph (leaf nodes first; completed items marked ✅):

```
Phase 1:  §6 (P0-C/D: NtCreateFile, NtProtectVirtualMemory, NtTerminateThread, NtWaitForMultipleObjects) → §8 (full file I/O)
Phase 2:  §1
Phase 3:  §4 (real CriticalSection) ✅ + §7 (heap) ✅
Phase 4 (need §1): §2 (dynamic loading) → §5 (toolchain)
Phase 5 (need §3, §4, §6): §9 (per-thread TEB)
Phase 6:  §3 (TLS)
```

---

## 1. No Relocation Support

### Current State

The PE must load at its preferred image base. A `MAP_STACK` fallback exists
in `image_mapper.c`, but **relocations are never applied**. Any PE that
cannot get its preferred base is loaded with stale absolute addresses and
crashes. Blocks loading DLLs compiled with `/DYNAMICBASE` (ASLR).

### Investigation

**PE Relocation Format:** `DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]`
(index 3) — chain of `IMAGE_BASE_RELOCATION` blocks (VirtualAddress +
SizeOfBlock), each followed by 2-byte type+offset entries (12-bit offset,
4-bit type).

**For x86_64, only `IMAGE_REL_BASED_DIR64` (10) matters:**

```
delta = actual_base - preferred_base
for each DIR64 entry:
    target = actual_base + block->VirtualAddress + entry.offset
    *(uint64_t*)target += delta
```

**mprotect strategy:** Apply **before** per-section mprotect (between section
copy and protection lockdown in image_mapper.c). Image is still fully RWX —
no protection manipulation needed.

**IAT overlap:** Relocations may write to IAT entries. Order: relocations
first, then import resolution.

**Edge cases:**
- `delta == 0` (loaded at preferred base): no-op, skip entirely
- `IMAGE_FILE_RELOCS_STRIPPED` flag: no `.reloc` section — refuse non-preferred base
- Address collisions: `MAP_STACK` gets random address; full 64-bit delta is fine for DIR64

### Approach

New `apply_relocations()` in `src/loader/relocations.c`, called from
`image_mapper.c` after section copy, before mprotect. Only handle `DIR64` and
`ABSOLUTE` (no-op). Early-exit if `delta == 0`. Fail if `RELOCS_STRIPPED` and
`delta != 0`.

### Complexity: Medium

### Prerequisites

- Add `IMAGE_BASE_RELOCATION`, `IMAGE_RELOC_ENTRY` structs to `include/pe.h`
- Add `IMAGE_REL_BASED_DIR64`, `IMAGE_REL_BASED_ABSOLUTE`, `IMAGE_FILE_RELOCS_STRIPPED` constants

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Corrupting `.text` with wrong offset math | High | Validate target within `SizeOfImage` |
| mprotect ordering (SIGSEGV on read-only) | Medium | Apply **before** mprotect eliminates this |
| IAT order vs import resolution | Low | Natural order: relocate before resolve |

### Implementation Plan

1. Add structs/constants to `include/pe.h`
2. Create `src/loader/relocations.c` with `apply_relocations()`
3. Call from `image_mapper.c` between copy and mprotect
4. Add declaration to `src/loader/loader_priv.h`
5. Test: force `MAP_STACK` fallback, verify relocated code executes

---

## 2. No Dynamic Loading

### Current State

`LoadLibraryA` returns `NULL`. `GetProcAddress` and `GetModuleHandleA` are
also NULL-returning stubs (in `src/stubs/kernel32_module.c`). Runtime DLL
loading is not implemented.

### Investigation

**What `LoadLibraryA` must do (at guest runtime in single-process model):**

```
LoadLibraryA("some.dll")
 │
 ├─ 1. Resolve DLL path
 ├─ 2. Open & parse PE (reuse map_image logic)
 ├─ 3. Map image (mmap at ImageBase, MAP_STACK fallback)
 ├─ 4. Copy sections, set protections
 ├─ 5. Resolve imports (3-tier: stub table → module exports → recursive load)
 ├─ 6. Pass 2 thunk patching
 ├─ 7. Register in module list + PEB Ldr
 └─ 8. Return HMODULE = base address
```

**Key insight:** `mmap`, `open`, `fstat` are Linux syscalls < 0x400 — pass
through seccomp. `LoadLibraryA` can use libc directly.

**Import resolution refactoring:** Current `resolve_import()` does static
`import_table` lookup only. Must become **three-tier**:

```c
static void *resolve_import(const char *dll, const char *func) {
    void *addr = lookup_stub_import(dll, func);    // Tier 1: our stubs
    if (addr) return addr;
    loaded_module_t *mod = module_list_find(dll);  // Tier 2: loaded modules
    if (mod) return lookup_export(mod, func);
    return NULL;                                    // Tier 3: caller recurses
}
```

**Export table parsing:** `IMAGE_EXPORT_DIRECTORY` at `DataDirectory[0]`.
Binary search through `AddressOfNames`. Forwarder detection: if RVA falls
within the export directory itself, it's a forwarder string.

**PEB Ldr:** `PEB_LDR_DATA` at `PEB + 0x018` with three doubly-linked lists
of `LDR_DATA_TABLE_ENTRY` nodes (base address, size, DLL name).

**Interaction with syscall interception:** Existing `import_table` C handlers
use direct Linux syscalls (< 0xF000). They work for DLL imports too —
**no new thunk generation needed**. Only DLL-to-DLL exports need new code.

### Approach

Five-phase: (1) module list + PEB Ldr, (2) export parsing, (3) import
refactoring, (4) API stubs, (5) edge cases.

### Complexity: High (~730 lines)

### Prerequisites

None — self-contained.

### Risks

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Address space conflicts | Medium | MAP_STACK fallback |
| Circular dependencies | Medium | Track "currently loading" state |
| IAT corruption | Medium | Same pass-1+pass-2 as main PE |
| PEB Ldr corruption | Medium | Initialize before guest code |

---

## 3. No TLS Support

### Current State

`TlsGetValue` returns `NULL`; `__dyn_tls_init_callback` is stubbed.
`__declspec(thread)` variables are never initialized.

### Investigation

**Two TLS subsystems:**

**A. Implicit TLS (`__declspec(thread)`) — PE TLS Directory**

`DataDirectory[9]` (`IMAGE_DIRECTORY_ENTRY_TLS`) → `IMAGE_TLS_DIRECTORY64`:

```c
typedef struct {
    uint64_t StartAddressOfRawData;  // VA of raw TLS template
    uint64_t EndAddressOfRawData;    // VA past end
    uint64_t AddressOfIndex;         // Where loader writes TLS index
    uint64_t AddressOfCallBacks;     // NULL-terminated callback array
    uint32_t SizeOfZeroFill;         // Ignored
    uint32_t Characteristics;
} IMAGE_TLS_DIRECTORY64;
```

**Access on x86_64:** `gs:[0x58]` (TEB→ThreadLocalStoragePointer) → array
of per-image TLS pointers → per-module TLS data block.

**B. Explicit TLS (`TlsAlloc`/`TlsGetValue`/`TlsSetValue`/`TlsFree`)**

Win32 API. `TlsAlloc` returns index 1-64 (bitmap). Index →
`TEB→TlsSlots[64]` (via `ThreadLocalStoragePointer` at `TEB + 0x58`).

**Initial thread setup:**
1. Detect TLS directory (`DataDirectory[9].Size > 0`)
2. Allocate `ThreadLocalStoragePointer` array (64 × 8 = 512 bytes, zeroed)
3. Copy `.tls` template
4. Write TLS index into `AddressOfIndex`
5. Set `TEB[0x58]` to array, `TEB[0x48]` (ForwarderChain) to NULL
6. Call callbacks with `DLL_PROCESS_ATTACH` (sets `_CRT_MT = 2`)

### Approach

**Phase 1** (initial thread): TLS directory parsing in `teb_peb.c`.
**Phase 2** (explicit API): `TlsAlloc` (bitmap), `TlsGetValue`, `TlsSetValue`, `TlsFree`.
**Phase 3** (threading): Per-thread TLS when per-thread TEB is implemented.

### Complexity: Medium (~160 lines for Phase 1+2)

### Prerequisites

None. Independent.

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Template copy corrupts data | Medium | Validate raw addresses |
| Wrong TEB offset | Medium | Verify against Windows 10/11 layout |

---

## 4. Synchronization Primitives

### Current State

**✅ Implemented.** `CRITICAL_SECTION` uses the correct **40-byte layout** matching
Windows x64 (`src/stubs/kernel32_misc.c`).

- `InitializeCriticalSection` zeroes the struct, sets `LockCount = -1`
- `EnterCriticalSection` uses a **CAS fast-path**
  (`__atomic_compare_exchange_n` on `LockCount`) for uncontended entry;
  recursive entry by the owning thread is handled inline
- **Slow path** (contention): lazily creates a kernel EVENT via
  `NtCreateEvent`, resets it via `NtResetEvent`, waits via
  `NtWaitForSingleObject`, then retries CAS in a loop
- `LeaveCriticalSection` restores `LockCount = -1` and signals via
  `NtSetEvent` when recursion count reaches 0
- `DeleteCriticalSection` closes the semaphore handle via `NtClose`

`NtCreateEvent`, `NtSetEvent`, `NtResetEvent`, `NtWaitForSingleObject`,
`NtCreateMutex`, and `NtReleaseMutex` are all **implemented** (in
`src/stubs/ntdll_synchronization.c`). Events support auto-reset and
manual-reset modes with proper signaling via `pthread_cond_signal`.

**Header layout** (`include/kernel32.h`): `CRITICAL_SECTION` was previously
**28 bytes with misnamed fields**. **✅ Fixed to correct 40-byte layout:**

```c
// Previous (WRONG — 28 bytes, missing LockSemaphore):
typedef struct {
    void *DebugInfo;      // 0x00, 8 bytes
    int   LockCount;      // 0x08, 4 bytes
    int   RecursionCount; // 0x0C, 4 bytes
    void *OwningThread;   // 0x10, 8 bytes
    void *SpinCount;      // 0x18, 8 bytes ← WRONG: should be LockSemaphore then SpinCount
} CRITICAL_SECTION;       // total 28 bytes ← WRONG

// Current (CORRECT — 40 bytes):
typedef struct _RTL_CRITICAL_SECTION {
    void    *DebugInfo;         // 0x00, 8 bytes  — NULL
    LONG    LockCount;          // 0x08, 4 bytes  — -1=free, 0=owned, >0=contention
    LONG    RecursionCount;     // 0x0C, 4 bytes  — re-entry depth
    HANDLE  OwningThread;       // 0x10, 8 bytes  — thread ID
    HANDLE  LockSemaphore;      // 0x18, 8 bytes  — kernel handle (EVENT)
    ULONG_PTR SpinCount;        // 0x20, 8 bytes  — spin iterations
} RTL_CRITICAL_SECTION;         // total 40 bytes
```

**Fast path (user-mode only):**

```c
if (InterlockedIncrement(&cs->LockCount) != 0) {
    if (GetCurrentThreadId() == cs->OwningThread) { /* recursion */ }
    else { RtlpWaitForCriticalSection(cs); }  // slow path
}
```

**Slow path:** Lazily create EVENT in `cs->LockSemaphore`, wait via
`NtWaitForSingleObject`.

**Linux mapping:** `pthread_mutex_t` + `pthread_cond_t` (linked via `-lpthread`).
Signal-safe atomics (`__atomic_add_fetch`, `__atomic_compare_exchange_n`)
compile to `LOCK XADD` / `CMPXCHG` — no kernel transition.

**Extended `handle_entry_t`** ✅ implemented — supports non-FD objects (events, mutexes, etc.):

```c
typedef struct {
    union { int fd; void *object; } u;
    handle_type_t type;
    uint8_t used;
} handle_entry_t;
```

### Approach

**Phase A (Foundation):** ✅ Done — `CRITICAL_SECTION` header fixed to 40 bytes,
`handle_entry_t` extended, `pthread_mutex_t` + `pthread_cond_t` in `wine_event_t`,
`NtSetEvent` and `NtWaitForSingleObject` implemented.

**Phase B (Core):** ✅ Done — `InitializeCriticalSection`, `EnterCriticalSection`
(CAS fast-path + event slow-path), `LeaveCriticalSection`, `DeleteCriticalSection`.

**Phase C (Extended):** `TryEnterCriticalSection`, `InitializeCriticalSectionEx`.

**Phase D (More primitives):** MUTEX, SEMAPHORE, SRWLOCK.

### Complexity: Medium (~200 lines for Phase A+B)

### Prerequisites

All required syscalls (`NtCreateEvent`, `NtSetEvent`, `NtResetEvent`,
`NtWaitForSingleObject`, `NtCreateMutex`, `NtReleaseMutex`) are **implemented**.

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| `CRITICAL_SECTION` ABI mismatch | **Critical** | Fix header immediately |
| Deadlock (event never signaled) | High | Test with 2+ threads |
| Signal handler calling pthread | Critical | Fast path uses only atomics |

---

## 5. Only mingw-w64 Executables

### Current State

Three mingw-w64-specific assumptions: (1) `.refptr` section, (2) import DLL is
`msvcrt.dll`, (3) `__acrt_iob_func` wrapper patch in `.text`. MSVC-compiled
binaries or other toolchains may not work.

### Investigation

**MSVC UCRT startup is radically different:**

```
// mingw-w64 (current)
mainCRTStartup → __getmainargs → _initterm → main

// MSVC UCRT
mainCRTStartup → __scrt_common_main_sealed → __scrt_initialize_crt →
                 __scrt_acquire_global_lock → initterm → __scrt_execute_handlers → main
```

**MSVC does NOT use `.refptr`.** Uses `__local_base` / `__local_base_ucrt`
sections with direct references.

**DLL import split for MSVC UCRT:**

```
ucrtbase.dll:      fprintf, malloc, free, memcpy, strlen, signal, exit, abort, ...
vcruntime140.dll:  __scrt_initialize_crt, __scrt_common_main_sealed, initterm, terminate, ...
KERNEL32.dll:      GetCommandLineW, GetEnvironmentStringsW, GetStartupInfoW, ...
```

**iob differences:** `__acrt_iob_func` is imported from `ucrtbase.dll` (direct
import, no wrapper to patch). Simply resolving to `__wine_iob_data` works.

**`__acrt_iob_func` patching:** The current 15-byte `.text` patch (in
`src/loader/guest_setup.c`) is a workaround for a **mingw-w64 import library
bug**. MSVC doesn't have this wrapper — no patching needed.

### Approach

**Phase 1 (Low risk):** Extend import table with `ucrtbase.dll` +
`vcruntime140.dll` entries reusing existing `wine_*` stubs. Enables mingw-w64
+ UCRT (MSYS2 UCRT64).

**Phase 2 (Medium):** CRT type detection — scan import descriptors,
conditionalize patching.

**Phase 3 (Higher):** Full MSVC — `__scrt_*` stubs, `__local_base*` globals,
`GetCommandLineW`/`GetEnvironmentStringsW` stubs, skip CRT via `main()` jump.

**Best path for MSVC:** Skip CRT entirely (jump to `main()` directly — already
works for mingw-w64), only resolve imports for DLLs that `main()` calls.

### Complexity: High (~3-5 days for Phase 3)

### Prerequisites

Heap (§7) — `malloc`/`calloc`/`free` are imported from `ucrtbase.dll`.

### Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Breaking mingw-w64 support | High | High | New paths are additive |
| UCRT FILE struct differs (64-96 vs 48) | Medium | Medium | Zero-pad `__wine_iob` to 96 bytes |

### Files to Change

| File | Changes |
|------|---------|
| `src/loader/import_table.c` | Add `ucrtbase.dll`, `vcruntime140.dll` entries |
| `src/stubs/crt_msvc.c` (new) | `__scrt_*` stubs, `__local_base*` globals |
| `src/stubs/crt_stdio.c` | `__wine_iob` padded to 96 bytes |
| `src/stubs/kernel32_misc.c` | `GetCommandLineW`, `GetEnvironmentStringsW` |
| `src/stubs/crt_offset_discovery.c` | Skip for non-mingw CRTs |
| `src/loader/guest_setup.c` | Conditional `patch_acrt_iob` |

---

## 6. Limited Syscall Handlers

### Current State

**25 NT syscalls** are implemented. Unsupported syscalls cause
`INLINE_SYSCALL_KILL(getpid(), SIGSEGV)` (the default case in `dispatcher.c`).

All 25 are listed in the [Current Capabilities Summary](#current-capabilities-summary).

### P0 Missing Syscalls (blocks most real applications)

**Correct syscall numbers verified against Windows 10/11 x86_64:**

| Syscall | NT # | Linux Mapping |
|---------|------|---------------|
| `NtTerminateThread` | `0x1D` | pthread_exit / kill |
| `NtProtectVirtualMemory` | `0x4D` | mprotect() |
| `NtWaitForMultipleObjects` | `0x58` | ppoll() |
| `NtCreateFile` | `0x59` | openat() + path parsing |
| `NtQueryAttributesFile` | `0x3B` | stat() |

**NOTE:** Earlier versions of this document incorrectly listed
`NtQuerySystemTime` as `0x58` (conflicting with `NtWaitForMultipleObjects`)
and `NtProtectVirtualMemory` as `0x4E` (conflicting with
`NtCreateThreadEx`). The corrected values above have been verified against
the Windows x86_64 syscall table.

### Priority Subsets

**P0-A (Quick wins — ✅ implemented):**

| Syscall | NT # | Implementation |
|---------|------|---------------|
| `NtQuerySystemTime` | `0x09` | clock_gettime(CLOCK_REALTIME) |
| `NtQueryPerformanceCounter` | `0x55` | clock_gettime(CLOCK_MONOTONIC) |
| `NtQueryPerformanceFrequency` | `0x56` | Return constant |
| `NtDelayExecution` | `0x1A` | nanosleep() (already used by Sleep stub) |

**P0-B (Sync infrastructure — ✅ implemented, enables CriticalSection + heap):**

| Syscall | NT # | Implementation |
|---------|------|---------------|
| `NtWaitForSingleObject` | `0x03` | futex / pthread_cond_wait |
| `NtSetEvent` | `0x5C` | pthread_cond_signal |
| `NtResetEvent` | `0x5E` | Clear flag |
| `NtCreateMutex` | `0x44` | pthread_mutex_init |
| `NtReleaseMutex` | `0x1E` | pthread_mutex_unlock |

**P0-C (File I/O expansion):**

| Syscall | NT # | Implementation |
|---------|------|---------------|
| `NtCreateFile` | `0x59` | openat() + path parsing |
| `NtQueryAttributesFile` | `0x3B` | stat() |

**P0-D (Process/memory):**

| Syscall | NT # | Implementation |
|---------|------|---------------|
| `NtProtectVirtualMemory` | `0x4D` | mprotect() |
| `NtTerminateThread` | `0x1D` | pthread_exit / kill |

### Auto-Generation

Current: **four manual edits across three files** per syscall (dispatcher.c
case, handler in ntdll_*.c, thunk in thunk_gen.c, header declaration in
ntdll.h).

Recommended: `include/nt_syscalls.def` + Python script generates
`nt_constants_gen.h`, `dispatcher_gen.c`, `thunk_list_gen.c`.

**ptr_flags encoding:** Bit-field where each bit marks an output pointer arg.

### Default Handler

Change `default` case from `INLINE_SYSCALL_KILL(getpid(), SIGSEGV)` to return
`STATUS_NOT_IMPLEMENTED`. Many Windows apps check NTSTATUS and fall back
gracefully.

### NT Status Codes

Add: `STATUS_NOT_IMPLEMENTED` (0xC00000B7), `STATUS_FILE_NOT_FOUND`,
`STATUS_END_OF_FILE`, `STATUS_NO_MORE_ENTRIES`, `STATUS_PENDING`, etc.

### Approach

1. **Safety net** — `STATUS_NOT_IMPLEMENTED`, add NTSTATUS codes
2. **P0-A quick wins** — ✅ Done: 4 trivial syscalls
3. **P0-B sync infra** — ✅ Done: enables real CriticalSection (§4)
4. **Auto-generation** — `.def` file + Python generator
5. **P0-C/D** — follow as needed

### Complexity: High

### Prerequisites

None.

---

## 7. Heap Management

### Current State

**✅ Implemented.** Heap management backed by [dlmalloc](https://github.com/WebAssembly/wasi-libc/blob/main/dlmalloc/src/malloc.c)
(public domain, ~12KB) in `src/heap/dlmalloc.c`. The `MMAP`/`MMUNMAP` hooks
are overridden to use `sysv_mmap`/`INLINE_SYSCALL_MUNMAP` — the same low-level
mmap calls used elsewhere in the project.

**Implemented API surface** (in `src/heap/wine_heap.c`):

| Function | Status | Notes |
|----------|--------|-------|
| `HeapCreate` | ✅ | Creates `wine_heap_t` via mmap, initializes `pthread_mutex_t` |
| `HeapAlloc` | ✅ | Lock → `dlmalloc` → unlock; supports `HEAP_ZERO_MEMORY` |
| `HeapFree` | ✅ | Lock → `dlfree` → unlock; `NULL` pointer is valid no-op |
| `HeapReAlloc` | ✅ | `dlrealloc`; zeros new portion when `HEAP_ZERO_MEMORY` set |
| `HeapDestroy` | ✅ | Invalidates heap, destroys mutex, unmaps struct via `INLINE_SYSCALL_MUNMAP` |
| `GetProcessHeap` | ✅ | Returns `g_process_heap` (created in `init_process_heap()`) |
| `HeapSize` | ✅ | Returns `dlmalloc_usable_size` |

**PEB.ProcessHeap** wired up in `src/loader/teb_peb.c`: `PEB_PROCESS_HEAP`
(`0x030`) is set to the process heap handle returned by `init_process_heap()`.

**Key:** Heap functions are **library functions** (not NT syscalls). All
implemented as `WINE_STUB` (ms_abi) in `src/heap/wine_heap.c`. Registered in
`src/loader/import_table.c` and `src/loader/ordinal_table.c`.

### Implementation

**Option B chosen and implemented:** dlmalloc dropped into `src/heap/dlmalloc.c`,
`MMAP`/`MMUNMAP` overridden to use `sysv_mmap`/`INLINE_SYSCALL_MUNMAP`.
Wrapped in `wine_heap_t` with `pthread_mutex_t` for thread safety.

```
HeapAlloc(heap, flags, size)  ← WINE_STUB
    → wine_heap.c (Windows API Shim)
        → dlmalloc (src/heap/dlmalloc.c)
            → sysv_mmap / INLINE_SYSCALL_MUNMAP (existing)
```

### Phased Implementation

**Phase 1:** ✅ Done — `PEB_PROCESS_HEAP` constant, dlmalloc, `wine_heap.c` shim,
`HeapCreate`/`HeapAlloc`/`HeapFree`, process heap init.

**Phase 2:** ✅ Done — `HeapReAlloc`, `HeapDestroy`, `GetProcessHeap`,
`HeapSize`.

**Phase 3:** `HeapWalk`, `HeapSetInformation`, `HeapValidate`,
`HeapLock`/`HeapUnlock`.

### Complexity: Medium

### Prerequisites

✅ `CRITICAL_SECTION` is implemented with real locking (CAS fast-path + event slow-path).

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| dlmalloc MMAP conflicts with PE mapping | Low | dlmalloc uses anonymous mmap |
| Thread safety | Low | ✅ Mitigated — uses `pthread_mutex_t` directly |

### Files

| File | Action |
|------|--------|
| `src/heap/dlmalloc.c` | New — public domain dlmalloc |
| `src/heap/wine_heap.c` | New — `Heap*` stubs |
| `src/heap/wine_heap.h` | New — `wine_heap_t` struct |
| `include/nt_constants.h` | Add `PEB_PROCESS_HEAP 0x030` |
| `include/kernel32.h` | Add `Heap*` declarations, `HEAP_*` flags |
| `src/loader/teb_peb.c` | Create process heap, set `PEB->ProcessHeap` |
| `Makefile` | Add `src/heap/*.c` |

---

## 8. Limited Filesystem I/O

### Current State

`NtOpenFile` **is implemented** (in `src/stubs/ntdll_io.c`): extracts path
from `OBJECT_ATTRIBUTES→ObjectName→UNICODE_STRING`, performs UTF-16→UTF-8
ASCII conversion, calls `openat()`, stores result in handle table. Supports
`GENERIC_READ` and `GENERIC_WRITE` access modes.

`NtReadFile` and `NtWriteFile` are implemented and work through the handle
table (stdin/stdout/stderr pre-mapped, plus any files opened via `NtOpenFile`).

**`NtCreateFile` is NOT implemented** — this is the syscall most Windows
programs use for file operations (it combines open + create with rich
semantics). `NtQueryAttributesFile`, `NtSetInformationFile`, and
`NtQueryDirectoryFile` are also not implemented.

### Investigation

**What `NtCreateFile` must handle:** create disposition mapping
(`FILE_SUPERSEDE` → `O_WRONLY|O_CREAT|O_TRUNC`, etc.), path translation
(`C:\...` → `/home/...`), share mode tracking, file attribute storage.

**Create disposition mapping:**

| Windows | Linux |
|---------|-------|
| `FILE_SUPERSEDE` | `O_WRONLY\|O_CREAT\|O_TRUNC` |
| `FILE_OPEN` | `O_RDONLY` |
| `FILE_CREATE` | `O_WRONLY\|O_CREAT\|O_EXCL` |
| `FILE_OPEN_IF` | `O_WRONLY\|O_CREAT` |
| `FILE_OVERWRITE_IF` | `O_WRONLY\|O_CREAT\|O_TRUNC` |

**Other needed handlers:** `NtQueryAttributesFile` (stat),
`NtSetInformationFile` (ftruncate/fcntl), `NtQueryDirectoryFile` (getdents).

**Current NtOpenFile limitations:**
- ASCII-only path conversion (non-ASCII characters become `?`)
- No share mode enforcement
- No FILE_CREATE semantics (always opens existing or falls back to `/dev/null`)
- No `ShareAccess` parameter handling

### Approach

**Option A (Full):** Path translation + `NtCreateFile` + attributes + directory.
3-4 new files.

**Option B (Minimal):** `NtCreateFile` only, ASCII paths, no share modes.
1-2 files.

### Complexity: High

### Prerequisites

Benefits from ordinal import support. Needs dispatcher registration (§6).

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Path translation edge cases | Medium | Handle common cases first |
| Share mode conflicts | High | Track per handle; enforce at open |
| Handle table overflow (256) | Low | Expand or dynamic |
| UTF-16 → UTF-8 for non-ASCII paths | Medium | Full Unicode conversion |

---

## 9. Shared-TEB Threading Model

### Current State

`NtCreateThreadEx` **is implemented** and creates real Linux threads via
`clone()` with `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD`
(in `src/stubs/ntdll_objects.c`). The thread uses a wrapper function that
extracts the start routine and argument from a temporary mmap'd page, then
calls the entry point directly.

**Limitation:** All threads share the **same TEB** and the **same GS base**.
The clone'd thread inherits the parent's address space (CLONE_VM) and the
same GS base register. This means:

- SEH chain is shared — `__try/__except` cannot be per-thread
- TEB self-reference (`gs:[0x08]` and `gs:[0x30]`) points to the same
  TEB for all threads
- No per-thread stack isolation — each thread uses kernel-allocated default
  stack (the `stack_size` parameter is acknowledged but not yet implemented)
- `NtGetContextThread` and `NtSetContextThread` are stubs (zero out / no-op)
- `NtTerminateThread` is not implemented — threads exit via `INLINE_SYSCALL_EXIT(0)`
  which terminates the **entire process**

Thread tracking uses `wine_thread_t` with `tid` (clone PID) and `suspended`
flag. Threads are stored in `threads[MAX_THREADS]` array (MAX_THREADS=32).

### Investigation

**Option A (Per-thread TEB):** Each thread gets its own TEB with its own
SEH chain. `wine_thread_t` includes `teb` and `seh_frame` pointers.
`NtCreateThreadEx` allocates TEB, sets GS base via `arch_prctl(ARCH_SET_GS)`,
creates pthread with custom stack.

**Option B (Global SEH + thread-local extensions):** Keep global frame as
tail; guest `__try/__except` prepends frames on stack. Requires proper
`__C_specific_handler` implementation.

**Option C (Defer):** "Works for single-threaded programs only."

### Approach

**Option A** is the most authentic. Requires:
- `NtCreateThreadEx` to allocate per-thread TEB via `mmap()`
- GS base switching via `arch_prctl(ARCH_SET_GS)` (thread-local)
- Custom stack allocation (using `stack_size` parameter)
- Signal handler must identify which thread's TEB to use
- `NtTerminateThread` to kill only the target thread
- `NtGetContextThread` / `NtSetContextThread` to work with per-thread state
- `__C_specific_handler` to walk per-thread SEH chain

### Complexity: High

### Prerequisites

- `NtSetEvent` / `NtWaitForSingleObject` for thread synchronization — syscall handlers (§6)
- Real `CRITICAL_SECTION` (§4) for thread-safe data structures
- `NtTerminateThread` for per-thread termination
- TLS (§3) for per-thread state

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Multi-threading race conditions | High | Careful locking; thorough testing |
| Signal handler thread identification | Medium | `arch_prctl(ARCH_GET_GS)` to recover TEB |
| Windows x64 table-based exception handling | High | `__C_specific_handler` must parse `.xdata` tables |
| GS base switch corrupts glibc TLS | Critical | Switch back to Linux TLS for any libc call |

---

## Appendix: Cross-Reference Matrix

| Limitation | Enables | Blocked By |
|-----------|---------|------------|
| §1 Relocation | §2 Dynamic Loading | None |
| §2 Dynamic Loading | §5 Toolchain (DLL loading) | §1 (optional) |
| §3 TLS | §9 Threading | None |
| §4 Sync | ✅ Done — enables §7 Heap, §9 Threading | ✅ Syscall handlers (§6 P0-A/B) |
| §5 Toolchain | — | §7 Heap |
| §6 Syscall | §4 Sync (✅ P0-A/B), §8 File I/O | None |
| §7 Heap | ✅ Done — enables §5 Toolchain | ✅ §4 Sync |
| §8 File I/O | Real applications | §6 Syscall |
| §9 Threading | Exception safety, real apps | §3, §4, §6 |

---

*Updated: 2026-05-05.
Source files: `src/stubs/ntdll_io.c`, `src/stubs/ntdll_memory.c`,
`src/stubs/ntdll_objects.c`, `src/stubs/ntdll_process.c`,
`src/stubs/ntdll_handle.c`, `src/stubs/kernel32_*.c`,
`src/stubs/crt_*.c`, `src/syscall/dispatcher.c`,
`src/syscall/thunk_gen.c`, `src/loader/teb_peb.c`,
`src/loader/guest_setup.c`, `include/nt_constants.h`,
`include/ntdll.h`, `include/kernel32.h`.*
