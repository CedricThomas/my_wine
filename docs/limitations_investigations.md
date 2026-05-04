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
| [4](#4-stubbed-synchronization-primitives) | Stubbed Synchronization | Medium | Events (syscall) |
| [5](#5-only-mingw-w64-executables) | Only mingw-w64 Executables | High | Heap (§7) |
| [6](#6-limited-syscall-handlers) | Limited Syscall Handlers | High | None |
| [7](#7-no-heap-management) | No Heap Management | Medium | CriticalSection (§4) |
| [8](#8-no-filesystem-io) | No Filesystem I/O | High | Syscall handlers (§6) |
| [9](#9-hardcoded-crt-fallback-offsets) | ✅ Hardcoded CRT Fallback Offsets — **RESOLVED** | Low | None |
| [10](#10-no-ordinal-imports) | ✅ No Ordinal Imports — **RESOLVED** | Low | None |
| [11](#11-60s-watchdog) | ✅ 60s Watchdog — **RESOLVED** | Low | None |
| [12](#12-single-thread-seh) | Single-thread SEH | High | Threading (§3, §4) |

### Recommended Implementation Order

By dependency graph (leaf nodes first):

```
✅ Done:  §11, §10, §9
Phase 1 (remaining):  §1
Phase 2 (need §1 or nothing): §6 → §3
Phase 3 (need §6):  §4 → §8
Phase 4 (need §4 + §7): §5
Phase 5 (need §3 + §4): §12
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
| mpostect ordering (SIGSEGV on read-only) | Medium | Apply **before** mprotect eliminates this |
| IAT order vs import resolution | Low | Natural order in main.c: relocate before resolve |

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
also NULL-returning stubs. Runtime DLL loading is not implemented.

### Investigation

**What `LoadLibraryA` must do (at guest runtime in child process):**

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
through seccomp. `LoadLibraryA` can use libc directly in the child.

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
**Phase 3** (threading): Per-thread TLS when `NtCreateThreadEx` is implemented.

### Complexity: Medium (~160 lines for Phase 1+2)

### Prerequisites

None. Independent.

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Template copy corrupts data | Medium | Validate raw addresses |
| Wrong TEB offset | Medium | Verify against Windows 10/11 layout |

---

## 4. Stubbed Synchronization Primitives

### Current State

`InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`,
`DeleteCriticalSection` are no-ops. No real mutual exclusion.

**Critical header bug** (`include/kernel32.h`): `CRITICAL_SECTION` is 32
bytes with misnamed fields. **Correct layout is 40 bytes:**

```c
typedef struct _RTL_CRITICAL_SECTION {
    void    *DebugInfo;         // 0x00, 8 bytes  — NULL or -1
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

**Extended `handle_entry_t`** needed for non-FD objects (events, mutexes, etc.):

```c
typedef struct {
    union { int fd; void *object; } u;
    handle_type_t type;
    uint8_t used;
} handle_entry_t;
```

### Approach

**Phase A (Foundation):** Fix `CRITICAL_SECTION` header (40 bytes), extend
`handle_entry_t`, add `pthread_mutex_t` + `pthread_cond_t` to `wine_event_t`,
implement `NtSetEvent` and `NtWaitForSingleObject`.

**Phase B (Core):** `InitializeCriticalSection`, `EnterCriticalSection`,
`LeaveCriticalSection`, `DeleteCriticalSection`.

**Phase C (Extended):** `TryEnterCriticalSection`, `InitializeCriticalSectionEx`.

**Phase D (More primitives):** MUTEX, SEMAPHORE, SRWLOCK.

### Complexity: Medium (~200 lines for Phase A+B)

### Prerequisites

`NtCreateEvent` exists. Need `NtSetEvent` and `NtWaitForSingleObject`.

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

**`__acrt_iob_func` patching:** The current 15-byte `.text` patch is a
workaround for a **mingw-w64 import library bug**. MSVC doesn't have this
wrapper — no patching needed.

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
| Breaking mingw-w664 support | High | High | New paths are additive |
| UCRT FILE struct differs (64-96 vs 48) | Medium | Medium | Zero-pad `__wine_iob` to 96 bytes |

### Files to Change

| File | Changes |
|------|---------|
| `src/loader/import_table.c` | Add `ucrtbase.dll`, `vcruntime140.dll` entries |
| `src/stubs/crt_msvc.c` (new) | `__scrt_*` stubs, `__local_base*` globals |
| `src/stubs/crt_stdio.c` | `__wine_iob` padded to 96 bytes |
| `src/stubs/kernel32_misc.c` | `GetCommandLineW`, `GetEnvironmentStringsW` |
| `src/stubs/crt_offset_discovery.c` | Skip for non-mingw CRTs |
| `src/loader/child_setup.c` | Conditional `patch_acrt_iob` |

---

## 6. Limited Syscall Handlers

### Current State

Only **16 NT syscalls** are implemented. Unsupported syscalls cause
`STATUS_NOT_IMPLEMENTED` (or `raise(SIGSEGV)` in some paths).

**Currently implemented:** `NtCallbackReturn`, `NtQueryInformationProcess`,
`NtClose`, `NtAllocateVirtualMemory`, `NtFreeVirtualMemory`,
`NtGetContextThread`, `NtSetContextThread`, `NtMapViewOfSection`,
`NtUnmapViewOfSection`, `NtTerminateProcess`, `NtReadFile`, `NtWriteFile`,
`NtCreateEvent`, `NtCreateSection`, `NtCreateThreadEx`, `NtOpenFile`.

### P0 Missing Syscalls (blocks most real applications)

| Syscall | NT # | Linux Mapping |
|---------|------|---------------|
| `NtWaitForSingleObject` | `0x00` | futex / pthread_cond_wait |
| `NtWaitForMultipleObjects` | `0x58` | ppoll() |
| `NtSetEvent` | `0x5C` | pthread_cond_signal |
| `NtResetEvent` | `0x5E` | Clear flag |
| `NtCreateMutex` | `0x44` | pthread_mutex_init |
| `NtReleaseMutex` | `0x1E` | pthread_mutex_unlock |
| `NtQuerySystemTime` | `0x58` | clock_gettime(CLOCK_REALTIME) |
| `NtQueryPerformanceCounter` | `0x55` | clock_gettime(CLOCK_MONOTONIC) |
| `NtQueryPerformanceFrequency` | `0x56` | Return constant |
| `NtDelayExecution` | `0x1A` | nanosleep() |
| `NtTerminateThread` | `0x1D` | pthread_exit / kill |
| `NtCreateFile` | `0x59` | openat() + path parsing |
| `NtQueryAttributesFile` | `0x3B` | stat() |
| `NtProtectVirtualMemory` | `0x4E` | mprotect() |

### Auto-Generation

Current: **four manual edits across three files** per syscall.
Recommended: `include/nt_syscalls.def` + Python script generates
`nt_constants_gen.h`, `dispatcher_gen.c`, `thunk_list_gen.c`.

**ptr_flags encoding:** Bit-field where each bit marks an output pointer arg.

### Default Handler

Change `default` case from `raise(SIGSEGV)` to return `STATUS_NOT_IMPLEMENTED`.
Many Windows apps check NTSTATUS and fall back gracefully.

### NT Status Codes

Add: `STATUS_NOT_IMPLEMENTED` (0xC00000B7), `STATUS_FILE_NOT_FOUND`,
`STATUS_END_OF_FILE`, `STATUS_NO_MORE_ENTRIES`, `STATUS_PENDING`, etc.

### Approach

1. **Safety net** — `STATUS_NOT_IMPLEMENTED`, add NTSTATUS codes
2. **Auto-generation** — `.def` file + Python generator
3. **P0 syscalls** — implement in priority order
4. **P1 syscalls** — follow as needed

### Complexity: High

### Prerequisites

None.

---

## 7. No Heap Management

### Current State

No `HeapAlloc`/`HeapFree`. `PEB->ProcessHeap` is zeroed. CRT crashes on any
dynamic allocation.

**Key:** Heap functions are **library functions in ntdll.dll**, not NT syscalls.
Must be `WINE_STUB` (ms_abi), not dispatcher handlers.

### PEB ProcessHeap

`PEB->ProcessHeap` at offset `0x030` (Windows 10+ x64). Must add:

```c
#define PEB_PROCESS_HEAP  0x030
```

### Three Options

| Option | Approach | Lines | Risk | Verdict |
|--------|----------|-------|------|---------|
| A: mmap-per-allocation | `mmap` per `HeapAlloc` | ~20 | High — catastrophic fragmentation | ❌ |
| **B: dlmalloc + mmap hooks** | Public domain, configurable | ~200 | **Low** | **✅ Recommended** |
| C: Custom page-based allocator | Free-list, block merge | 500-800 | Medium — allocator bugs | Future |

**Recommendation: Option B.** Drop [dlmalloc.c](https://github.com/WebAssembly/wasi-libc/blob/main/dlmalloc/src/malloc.c)
(public domain, ~12KB) into `src/heap/`. Override `MMAP`/`MMUNMAP` to use our
`sysv_mmap`/`INLINE_SYSCALL_MUNMAP`. Wrap in `wine_heap_t` with mutex.

```
HeapAlloc(heap, flags, size)  ← WINE_STUB
    → wine_heap.c (Windows API Shim)
        → dlmalloc (src/heap/dlmalloc.c)
            → sysv_mmap / INLINE_SYSCALL_MUNMAP (existing)
```

### Phased Implementation

**Phase 1** (~4h): `PEB_PROCESS_HEAP` constant, dlmalloc, `wine_heap.c` shim,
`HeapCreate`/`HeapAlloc`/`HeapFree`, process heap init.

**Phase 2** (~2h): `HeapReAlloc`, `HeapDestroy`, `GetProcessHeap`,
`HeapSize`, `HeapLock`/`HeapUnlock`.

**Phase 3** (~2h): `HeapWalk`, `HeapSetInformation`, `HeapValidate`,
real `EnterCriticalSection`/`LeaveCriticalSection`.

### Complexity: Medium

### Prerequisites

`CRITICAL_SECTION` must be upgraded from no-op to real `pthread_mutex`
(already linked via `-lpthread`).

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| dlmalloc MMAP conflicts with PE mapping | Low | dlmalloc uses anonymous mmap |
| Thread safety before real CriticalSection | Medium | Use `pthread_mutex_t` directly |

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

## 8. No Filesystem I/O

### Current State

Only console I/O. `NtOpenFile` is minimal (ASCII-only, no create). **No `NtCreateFile`**
— the syscall most Windows programs use for file operations.

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

### Approach

**Option A (Full):** Path translation + `NtCreateFile` + attributes + directory.
3-4 new files.

**Option B (Minimal):** `NtCreateFile` only, ASCII paths, no share modes.
1-2 files.

### Complexity: High

### Prerequisites

Benefits from ordinal import support (§10). Needs dispatcher registration (§6).

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Path translation edge cases | Medium | Handle common cases first |
| Share mode conflicts | High | Track per handle; enforce at open |
| Handle table overflow (256) | Low | Expand or dynamic |
| UTF-16 → UTF-8 for non-ASCII paths | Medium | Full Unicode conversion |

---

## 9. Hardcoded CRT Fallback Offsets

### Current State

Hardcoded offsets (`0x018`, `0x020`, `0x028`) when COFF symbol lookup fails.
Vary by mingw-w64 version and linker flags.

**Discovery hierarchy:** (1) COFF symbol table, (2) data section scan,
(3) `.text` instruction scan, (4) **hardcoded fallback** (fragile).

### Approach

**Option A (Recommended): Build-time offset measurement.**
Shell script compiles test PE, extracts actual offsets, generates
`include/crt_offsets_generated.h`.

**Option B: Extended runtime probing.** Scan `.bss` for patterns.

**Option C: Remove fallback entirely.** Hard error.

### Complexity

- Option A: Low (one script + header)
- Option B: Medium (~50 lines)
- Option C: Very Low (5 lines)

### Prerequisites

None. Option A requires mingw-w64 (already needed).

### ✅ RESOLVED

Implemented via `scripts/gen_crt_offsets.sh` — compiles a test PE with mingw-w64,
extracts actual `.refptr` offsets from COFF symbols, generates `include/crt_offsets_generated.h`.
When present, `crt_offset_discovery.c` uses generated offsets instead of hardcoded values.
Run `make gen-crt-offsets` to generate (requires Docker). Falls back to hardcoded values gracefully.

---

## 10. No Ordinal Imports

### Current State

Ordinal imports (high bit set) are skipped with a warning. IAT entry left at
0 — crashes when called.

### Approach

**Static ordinal database:**

```c
// src/loader/ordinal_table.c
typedef struct { const char *dll; uint16_t ordinal; const char *name; } ordinal_mapping_t;

static const ordinal_mapping_t ordinal_table[] = {
    { "ntdll.dll", 0x0005, "NtCallbackReturn" },
    { "ntdll.dll", 0x000F, "NtClose" },
    { "ntdll.dll", 0x0018, "NtAllocateVirtualMemory" },
    // ... ~50 entries for ntdll/kernel32/msvcrt
    { NULL, 0, NULL }
};
```

Modify `resolve_import_pass1()` to look up ordinal → name, then resolve through
existing name-based path.

### Complexity: Low (~60 lines total)

### Prerequisites

None.

### Risks

Ordinal numbers change between Windows versions. Acceptable since we only
support mingw-w64.

### ✅ RESOLVED

Implemented via `src/loader/ordinal_table.c` — static lookup table (~160 entries for
ntdll/kernel32/msvcrt ordinals). `resolve_import_pass1()` now looks up ordinal→name and
resolves through the existing name-based path. Pass 2 thunk patching also uses the lookup.

---

## 11. 60s Watchdog

### Current State

Hardcoded 60-second timeout in `child_setup.c`. Arbitrary — CRT init on slow
machines can approach this limit; too long for debugging hangs.

### Approach

**Option A (Recommended): Configurable timeout.**
`MY_WINE_WATCHDOG` env var or `--watchdog=N` CLI arg. Validate range
(1-3600s, default 60).

**Option B: Self-disabling after entry.** Cancel watchdog after guest `main()`
begins (covers only CRT init phase, ~5s).

**Option C: Heartbeat.** Timer reset on each syscall dispatch.

### Complexity: Low (5-10 lines for Option A)

### Prerequisites

None.

### ✅ RESOLVED

Timeout is now configurable via `MY_WINE_WATCHDOG` environment variable or `--watchdog=N`
CLI argument (range 1-3600 seconds, default 60). `MY_WINE_WATCHDOG` takes precedence.

---

## 12. Single-thread SEH

### Current State

One static SEH frame for the entire process. `NtCreateThreadEx` creates a
pthread but shares the same TEB (and same SEH chain). Guest `__try/__except`
handlers cannot be per-thread.

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
- `NtCreateThreadEx` to actually create threads (not just stub)
- Per-thread TEB allocation via `mmap()`
- GS base switching via `arch_prctl(ARCH_SET_GS)` (thread-local)
- Signal handler must identify which thread's TEB to use
- `__C_specific_handler` to walk per-thread SEH chain

### Complexity: High

### Prerequisites

- `NtCreateThreadEx` must work (exists but is stubbed)
- `NtGetContextThread` / `NtSetContextThread` must work with multiple threads
- TLS (§3) for per-thread state
- CriticalSection (§4) for thread synchronization

### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Multi-threading race conditions | High | Careful locking; thorough testing |
| Signal handler thread identification | Medium | `arch_prctl(ARCH_GET_GS)` to recover TEB |
| Windows x64 table-based exception handling | High | `__C_specific_handler` must parse `.xdata` tables |

---

## Appendix: Cross-Reference Matrix

| Limitation | Enables | Blocked By |
|-----------|---------|------------|
| §1 Relocation | §2 Dynamic Loading | None |
| §2 Dynamic Loading | §5 Toolchain (DLL loading) | §1 (optional) |
| §3 TLS | §12 SEH | None |
| §4 Sync | §7 Heap, §8 File I/O | Syscall handlers |
| §5 Toolchain | — | §7 Heap |
| §6 Syscall | §4 Sync, §8 File I/O, §11 Watchdog | None |
| §7 Heap | §5 Toolchain | §4 Sync |
| §8 File I/O | Real applications | §6 Syscall |
| §9 CRT Offsets | Robustness | None |
| §10 Ordinal Imports | More PEs | None |
| §11 Watchdog | DX | None |
| §12 SEH | Exception safety | §3, §4 |

---

*Generated by deep-thinker investigation on 2026-05-04.
Source files per section: see individual investigation documents.*
