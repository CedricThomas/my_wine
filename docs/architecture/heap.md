# Heap Management — Backend Abstraction and Wine Heap API

Reference for the heap subsystem in `src/heap/`. Provides the Windows Heap API
(kernel32.dll) to guest PE code through a backend-abstracted allocator, with
two compile-time backends: musl malloc for PE32+ and a minimal mmap allocator
for PE32.

---

## Overview

```
src/heap/
├── heap_backend.h                     # Backend abstraction (6 function declarations)
│
├── wine_heap.c                        # Wine Heap API implementation (KERNEL32_STUB exports)
├── wine_heap.h                        # Public header: init_process_heap(), g_process_heap
│
├── pe32plus_musl_malloc_backend.c     # PE32+ backend: musl libc malloc integration
│
├── pe32_mmap_heap_backend.c           # PE32 backend: mmap-based per-allocation allocator
│
├── musl_src/                          # Vendored musl malloc source (3 files)
│   ├── malloc.c
│   ├── aligned_alloc.c
│   └── malloc_usable_size.c
│
└── musl_stubs/                        # Musl environment stubs (6 files)
    ├── libc.h                         # libc struct, PAGE_SIZE, hidden, weak_alias
    ├── atomic.h                       # x86_64 atomic primitives + generic fallbacks
    ├── pthread_impl.h                 # __wait, __wake, __timedwait (no-ops)
    ├── dynlink.h                      # __malloc_replaced, __aligned_alloc_replaced
    ├── malloc_impl.h                  # struct chunk, struct bin, macros
    └── fork_impl.h                    # empty — malloc.c defines __malloc_atfork itself
```

**Design principle:** `wine_heap.c` implements the full Windows Heap API and
delegates all allocation to `heap_backend_*` functions defined in `heap_backend.h`.
The backend is selected at compile time via Makefile source groups — no runtime
dispatch, no vtable, no indirection.

---

## Wine Heap API

The Windows heap interface exported from `kernel32.dll`. Every function uses
`KERNEL32_STUB` (stdcall/ms_abi). Implemented in [`wine_heap.c`](../../src/heap/wine_heap.c).

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `HeapCreate` | `flOptions`, `dwInitialSize`, `dwMaximumSize` | `HHEAP` | Create a named heap. Allocates `wine_heap_t` via `mmap` (MAP_32BIT on PE32). Initializes mutex on PE32+. |
| `HeapAlloc` | `hHeap`, `dwFlags`, `dwBytes` | `LPVOID` | Allocate from heap. Calls `heap_backend_malloc()`. Supports `HEAP_ZERO_MEMORY` flag (0x00000008). |
| `HeapFree` | `hHeap`, `dwFlags`, `lpMem` | `BOOL` | Free allocation. Calls `heap_backend_free()`. `NULL` pointer is a valid no-op. |
| `HeapReAlloc` | `hHeap`, `dwFlags`, `lpMem`, `dwBytes` | `LPVOID` | Reallocate. Calls `heap_backend_realloc()`. With `lpMem==NULL`, acts as fresh alloc. `HEAP_ZERO_MEMORY` zeros only the new portion. |
| `HeapDestroy` | `hHeap` | `BOOL` | Invalidate and unmap heap handle. Clears `g_process_heap` if this was the process heap. |
| `HeapSize` | `hHeap`, `dwFlags`, `lpMem` | `LONG` | Returns `heap_backend_usable_size()`. Returns -1 on error. |
| `GetProcessHeap` | *(none)* | `HHEAP` | Returns `g_process_heap` (default process heap). |

### `wine_heap_t` Structure

```c
typedef struct wine_heap {
    pthread_mutex_t mutex;   // PE32+ only (guarded by #ifndef MY_WINE32)
    int is_valid;            // validation flag — set to 0 on HeapDestroy
} wine_heap_t;
```

### Handle Tracking

- `g_heap_handles[MAX_HEAP_HANDLES]` (64 slots) — open-address hash of valid heap pointers
- `heap_handle_find()`, `heap_handle_add()`, `heap_handle_remove()` — O(n) scan, sufficient for 64 max handles
- Every Heap* API call validates the handle against this table before proceeding
- Heap handles are allocated via `INLINE_SYSCALL_MMAP` with `MAP_32BIT` on PE32 (stays below 4GB)

### Process Heap

- `g_process_heap` — global pointer to the default process heap
- `init_process_heap()` — called early in loader; creates the process heap via `HeapCreate(0, 65536, 0)`
- `HeapDestroy(g_process_heap)` clears `g_process_heap` to NULL

---

## PE32+ Backend — Musl Malloc

[`pe32plus_musl_malloc_backend.c`](../../src/heap/pe32plus_musl_malloc_backend.c) integrates a vendored copy
of [musl libc](https://musl.libc.org/) `malloc` as the heap backend. Provides a proper
arena-based allocator with bin management, coalescing, and aligned allocation — suitable
for the high-allocation workload of 64-bit PE executables.

### Integration Strategy

Musl `malloc.c` is compiled **inline** (via `#include`) directly into the loader binary.
The approach has five layers:

1. **Stubs** (`musl_stubs/`) — define the libc environment musl expects: `libc` struct,
   `atomic_*` primitives, `pthread_impl` no-ops, `dynlink` globals, and `malloc_impl`
   macros (`struct chunk`, `struct bin`, size/padding calculations)
2. **Externals** — `__malloc_replaced` and `__aligned_alloc_replaced` (from `dynlink.h`)
   defined as globals
3. **Syscall wrappers** — `__mmap`, `__munmap`, `__madvise`, `__mremap`, `__syscall`
   mapped to `INLINE_SYSCALL_*` macros
4. **Source inclusion** — `#include "musl_src/malloc.c"`, `aligned_alloc.c`, `malloc_usable_size.c`
   with suppressed GCC warnings (parentheses, sign-compare, unused-but-set-variable, array-bounds)
5. **Export layer** — `heap_backend_*` functions call the musl internals
   (`__libc_malloc_impl`, `__libc_realloc`, `__libc_free`, `aligned_alloc`, `malloc_usable_size`)

### Syscall Mapping

| Musl Symbol | Implementation | Notes |
|-------------|---------------|-------|
| `__mmap` | `INLINE_SYSCALL_MMAP` | Direct syscall |
| `__munmap` | `INLINE_SYSCALL_MUNMAP` | Direct syscall |
| `__madvise` | no-op (returns 0) | `MADV_DONTNEED` is optional optimization |
| `__mremap` | alloc-new + memcpy + free-old | Fallback — doesn't use `MREMAP_MAYMOVE` |
| `__syscall` | always returns -1 | Forces musl to fall through to mmap path (brk is unavailable) |

### Build Configuration

The Makefile passes special include paths and flags:

```make
CFLAGS_pe32plus_musl_malloc_backend.o = $(SPECIAL_CFLAGS) -Isrc/heap/musl_stubs -Isrc/heap/musl_src
```

The `-Isrc/heap/musl_stubs` flag makes `#include "libc.h"`, `#include "atomic.h"`, etc.
resolve to our stub files instead of any system headers. The musl source files themselves
contain internal `#include` directives that resolve to the stub directory.

### Musl Atomics

The `musl_stubs/atomic.h` provides x86_64-specific atomic primitives using inline
assembly (cmpxchg16b, lock xchg, etc.) plus generic C fallbacks. These atomics require
64-bit instructions — which is why musl cannot be used on PE32 (i386).

---

## PE32 Backend — Mmap Allocator

[`pe32_mmap_heap_backend.c`](../../src/heap/pe32_mmap_heap_backend.c) is a minimal allocator for
32-bit builds. **Every allocation is its own mmap region.** No pooling, no bin management,
no coalescing.

### Why Not Musl?

**Musl atomics are x86_64-only.** The musl `malloc.c` uses 128-bit compare-and-swap
(`cmpxchg16b`) for lock-free arena management. This instruction doesn't exist on i386.
The PE32 backend exists solely because the 32-bit child process cannot use musl.

### Design

- Each allocation: one `mmap(MAP_32BIT)` call, guaranteed below 4GB
- 4-byte size header prepended to user pointer (`*(uint32_t *)base = alloc_size`)
- Minimum allocation: 4096 bytes (one page) — even tiny allocs get a full page
- `realloc`: if growth needed, alloc-new + memcpy + free-old; if no growth, return existing pointer

### Trade-offs

| Aspect | musl (PE32+) | mmap (PE32) |
|--------|-------------|-------------|
| Memory overhead | ~16-24 bytes per alloc header | 4KB minimum per allocation + 4-byte header |
| Fragmentation | Bin-based coalescing | Each alloc is independent — no external fragmentation |
| Performance | Fast for small allocs (arena-based) | Slow — every alloc is a syscall |
| Address space | Efficient (arena pools) | Wasteful for small frequent allocs |
| Complexity | ~2000 lines of musl + stubs | ~80 lines of self-contained code |

The mmap approach is acceptable because PE32 child processes allocate very few objects
(the child is a thin bootstrap layer that loads the PE, sets up the entry point, and
transitions to the 64-bit loader).

### `#if defined(__i386__)` Guard

The entire file is wrapped in `#if defined(__i386__)` / `#endif` so it compiles cleanly
on all architectures but only produces symbols when building for i386.

---

## Backend Abstraction

[`heap_backend.h`](../../src/heap/heap_backend.h) defines six pure-C functions. Both backends
must implement all six:

```c
void *heap_backend_malloc(size_t size);
void *heap_backend_calloc(size_t count, size_t size);
void *heap_backend_realloc(void *ptr, size_t size);
void heap_backend_free(void *ptr);
void *heap_backend_aligned_alloc(size_t align, size_t len);
size_t heap_backend_usable_size(void *ptr);
```

All use standard `size_t` parameters — no Windows-specific types. The `wine_heap.c`
layer translates Windows parameters (`uintptr_t`, `DWORD`) to `size_t` when calling
the backend.

### Compile-Time Selection

The Makefile controls which backend is compiled:

```make
# PE32+ (default): all .c files in src/heap/ (includes musl backend)
HEAP_SRC = $(sort $(shell find src/heap -maxdepth 1 -name '*.c'))

# PE32 (32-bit child): explicitly list only these two files
MY_WINE32_HEAP_SRC = src/heap/wine_heap.c src/heap/pe32_mmap_heap_backend.c
```

PE32+ builds get both backends compiled but `pe32_mmap_heap_backend.c` has the
`#if defined(__i386__)` guard so it produces no symbols on x86_64. PE32 builds
get only `wine_heap.c` and `pe32_mmap_heap_backend.c` — the musl backend is not
even compiled.

---

## Call Graph

```
Guest PE calls Kernel32 HeapAlloc/HeapFree/...
    │
    ▼
wine_heap.c (KERNEL32_STUB exports)
    ├── heap_handle_find()     — validate handle
    ├── pthread_mutex_lock()   — PE32+ only (skip on MY_WINE32)
    │
    ├── heap_backend_malloc()  ──────┐
    ├── heap_backend_free()    ──────┤
    ├── heap_backend_realloc() ──────┤ backend selection at compile time
    ├── heap_backend_usable_size() ──┤
    │                                │
    └── pthread_mutex_unlock()  ─────┘
                                       │
                    ┌───────────────────┴───────────────────┐
                    │                                       │
    PE32+ (x86_64)  │                                       │  PE32 (i386)
    musl backend    │                                       │  mmap backend
                    │                                       │
    __libc_malloc   │                                       │  INLINE_SYSCALL_MMAP
    __libc_free     │                                       │  INLINE_SYSCALL_MUNMAP
    __libc_realloc  │                                       │  (per-alloc mmap regions)
    aligned_alloc   │                                       │
    malloc_usable   │                                       │
                    │                                       │
                    ▼                                       ▼
            INLINE_SYSCALL_MMAP                    INLINE_SYSCALL_MMAP
            INLINE_SYSCALL_MUNMAP                   (MAP_32BIT for PE32)
            INLINE_SYSCALL_MADVISE
            INLINE_SYSCALL_MREMAP
```

---

## Cross-Cutting Dependencies

| From | Depends On |
|------|------------|
| `wine_heap.c` | `heap_backend.h`, `syscall/syscalls_inline.h`, `include/kernel32.h`, `loader/image_mapper.h` (`g_is_32bit_get`) |
| `pe32plus_musl_malloc_backend.c` | `musl_stubs/*`, `musl_src/*`, `syscall/syscalls_inline.h` |
| `pe32_mmap_heap_backend.c` | `syscall/syscalls_inline.h` |
| `kernel32_memory.c` | `HeapAlloc`/`HeapFree`/`HeapReAlloc`/`HeapSize` (calls through `wine_heap.c`) |
| `ntdll_memory.c` | `NtAllocateVirtualMemory` (separate — uses `mmap` directly, not heap) |
| `ddraw_*`, `dsound_*`, `user32_*` | Use `HeapAlloc`/`HeapFree` (PE32) or `malloc`/`free` (PE32+) via private helpers |

**Note:** Virtual memory (`VirtualAlloc`, `NtAllocateVirtualMemory`) and heap memory
are separate subsystems. `VirtualAlloc` maps directly to `mmap`/`munmap`/`mprotect`
via `ntdll_memory.c`. Heap operations (`HeapAlloc`, etc.) go through the backend
allocator in `src/heap/`.
