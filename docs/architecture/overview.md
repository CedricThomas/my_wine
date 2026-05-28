# my_wine — Architecture Overview

A userspace PE loader that runs Windows executables (PE32 and PE32+) on Linux.
The system has three binaries that share most code, plus a modular subsystem
for Windows API translation, heap management, and rendering.

---

## Three-Binary Layout

```
┌──────────────────────────────────────────────────────────────┐
│                     my_wine (wrapper)                        │
│  src/wrapper_main.c                                         │
│                                                              │
│  Reads PE headers → detects PE32 vs PE32+ → execvp()        │
│  Sibling binary selection via /proc/self/exe + strrchr()     │
└─────────┬──────────────────────────────┬─────────────────────┘
          │ PE32                         │ PE32+
          ▼                              ▼
┌────────────────────────┐    ┌─────────────────────────────────┐
│       my_wine32        │    │         my_wine64               │
│  src/loader/pe32_entry │    │  src/main.c                     │
│                        │    │                                 │
│  32-bit ELF binary     │    │  64-bit native process          │
│  (-m32, int $0x80)     │    │  (single-process stack switch)  │
│  FS → TEB via          │    │  GS → TEB via arch_prctl        │
│  set_thread_area       │    │                                 │
└────────────────────────┘    └─────────────────────────────────┘
```

### my_wine — Wrapper / Dispatcher

- **Source:** `src/wrapper_main.c`
- **Role:** Standalone PE-type detector. Opens the target binary, reads MZ signature,
  `e_lfanew`, PE signature, and the OptionalHeader Magic at `e_lfanew + 24`.
- **Decision:** Magic `0x10B` → `my_wine32`; Magic `0x20B` → `my_wine64`.
- **Mechanism:** Resolves its own directory via `readlink("/proc/self/exe")`, constructs
  the sibling backend path, and calls `execvp()`. Passes through all original argv.

### my_wine64 — PE32+ Loader

- **Source:** `src/main.c`
- **Role:** Full PE32+ loader in a single 64-bit process.
- **Architecture:** Maps the PE image into the host address space, resolves imports,
  sets up TEB/PEB, allocates a *separate* guest stack, installs the syscall dispatcher
  trampoline, and jumps to the entry point. After the jump, GS points at the guest TEB
  and the syscall dispatcher intercepts native syscalls.
- **Rejection:** If a PE32 image is passed, unmaps it and prints an error.

### my_wine32 — PE32 Loader

- **Source:** `src/loader/pe32_entry.c`
- **Role:** Full PE32 loader compiled as a 32-bit ELF binary (`-m32`).
- **Architecture:** Runs in a separate 32-bit process with glibc CRT. Uses `int $0x80`
  syscalls. Sets FS → TEB via `set_thread_area` (syscall 243) because
  `arch_prctl(ARCH_SET_FS)` returns `EINVAL` in 32-bit mode on a 64-bit kernel.
- **Path resolution:** Reads PE path from `argv[1]` with `WINE32_PE_PATH` as fallback.

---

## PE Loading Pipeline

Both `my_wine32` and `my_wine64` follow the same logical pipeline, though the
32-bit path uses separate submodules (prefixed `pe32_*`) and 32-bit ABI conventions:

```
  PE file on disk
        │
        ▼
  ┌──────────────┐
  │ map_image()   │  mmap(PROT_READ), parse DOS/NT headers, section table
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ copy sections │  mmap each section at preferred base + VirtualAddress
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │  mprotect    │  set correct permissions (RX, RW, etc.) per section characteristics
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ patch CRT    │  scan for .refptr relocations, resolve absolute addresses
  │  .refptr     │  (MinGW/Watcom-specific symbol patching)
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ resolve      │  walk Import Descriptor table, find DLL stubs, patch IAT
  │  imports     │  (kernel32, msvcrt, user32, ntdll, ddraw, dsound, ...)
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ setup TEB/   │  allocate guest TEB + PEB, wire self-references,
  │  PEB         │  image base, process params, LDR data
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ setup guest  │  allocate new stack (mmap + guard page)
  │  stack       │
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ install      │  write syscall dispatcher trampoline into guest address space
  │  dispatcher  │  (generated thunks for each syscall number)
  └──────┬───────┘
         ▼
  ┌──────────────┐
  │ jump to      │  switch to guest stack, set GS/FS → TEB,
  │  entry point  │  tail-call into PE entry (or user symbol like D_DoomMain)
  └──────────────┘
```

---

## Subsystem Overview

| Subsystem | Source | Purpose |
|-----------|--------|---------|
| **loader/** | `src/loader/` | Image mapping, section loading, import resolution, TEB/PEB setup, entry point resolution, crash handlers, guest stack setup. Shared code for both PE32 and PE32+. PE32-specific bootstrap in `pe32_*` files. See [Loader Architecture](./loader.md) for a deep dive. |
| **syscall/** | `src/syscall/` | Syscall dispatcher and thunk generator. Translates guest syscalls (int 0x80 / syscall) into host equivalents. Contains generated thunk table (`dispatcher_generated.c`), ABI wrappers, and the trampoline ASM entry point. See [Syscall Dispatcher](./syscall.md) for a deep dive. |
| **msvcrt/** | `src/msvcrt/` | Windows API stub implementations (~80 files). Covers kernel32 (file, process, memory, sync, string, console), user32 (windows, messages, dialogs, input), ntdll (objects, memory, time), ddraw, dsound, gdi32, winmm, and CRT functions (stdio, stdlib, file I/O). See [Windows API Stubs](./stubs.md) for a deep dive. |
| **heap/** | `src/heap/` | Heap allocation backends. `wine_heap.c` implements the Windows Heap API (`HeapCreate`, `HeapAlloc`, etc.). Two backends: musl malloc (PE32+) and a custom mmap-based allocator (PE32). See [Heap Management](./heap.md) for a deep dive. |
| **crt/** | `src/crt/` | C runtime detection and patching. Modules for MinGW (`crt_mingw.c`) and Watcom (`crt_watcom.c`). Handles .refptr patching, BSS offset discovery, entry symbol resolution, and `.bss` variable seeding (argc/argv/envp). See [CRT Handling](./crt.md) for a deep dive. |
| **backend/sdl2/** | `src/backend/sdl2/` | DOOM95 rendering backend. Implements DirectDraw and DirectSound surfaces, window management, input handling (keyboard/mouse), event queues, audio mixing, and palette support via SDL2. See [SDL2 Backend](./backend.md) for a deep dive. |

### Shared Source (root of `src/`)

| File | Role |
|------|------|
| `wrapper_main.c` | PE-type detector (my_wine) |
| `main.c` | PE32+ orchestrator (my_wine64) |
| `pe_headers.c` | PE header parsing utilities |
| `pe_imports.c` | Import descriptor parsing |
| `pe_symbols.c` | COFF symbol table parsing |
| `pe_rip_scan.c` | RIP-relative reference scanner for refptr patching |
| `run_guest.S` | Assembly entry for guest code handoff (PE32+) |
| `common.c` | Shared utility functions |
| `debug.c` | Debug logging infrastructure |

---

## Data Flow

```
PE .EXE file
    │
    ▼
┌─────────────────────────────────────────────────────┐
│                    my_wine (wrapper)                 │
│  read MZ → read e_lfanew → read PE sig              │
│  read OptionalHeader.Magic                          │
│                                                     │
│  0x10B (PE32) ──→ exec my_wine32                    │
│  0x20B (PE32+) ──→ exec my_wine64                   │
└─────────────────────────────────────────────────────┘

    │ PE32+ path          │ PE32 path
    ▼                     ▼
┌─────────────┐     ┌─────────────┐
│ my_wine64   │     │  my_wine32  │
│ (single     │     │ (32-bit     │
│  process)   │     │  ELF)       │
│             │     │             │
│ mmap PE     │     │ mmap PE     │
│ → sections  │     │ → sections  │
│ → mprotect  │     │ → mprotect  │
│ → patch CRT │     │ → patch CRT │
│ → imports   │     │ → imports   │
│ → TEB/PEB   │     │ → TEB/PEB   │
│ → guest     │     │ → guest     │
│   stack     │     │   stack     │
│ → dispatcher│     │ → dispatcher│
│ → GS→TEB    │     │ → FS→TEB    │
│ → jump      │     │ → jump      │
└──────┬──────┘     └──────┬──────┘
       │                   │
       ▼                   ▼
┌─────────────────────────────────────────────────────┐
│              Guest Code Execution                     │
│                                                      │
│  guest calls Win32 API ──→ msvcrt/ stubs             │
│  guest makes syscall  ──→ syscall/ dispatcher         │
│  guest allocs memory  ──→ heap/ backends              │
│  guest draws frame    ──→ backend/sdl2/               │
│                                                      │
│  All guest code runs in a single process (PE32+)     │
│  or a 32-bit child process (PE32)                    │
└─────────────────────────────────────────────────────┘
```

---

## PE32 vs PE32+ Distinctions

| Aspect | PE32+ (my_wine64) | PE32 (my_wine32) |
|--------|-------------------|------------------|
| Process | Single 64-bit host process | Separate 32-bit ELF child |
| ABI | 64-bit x86_64 | 32-bit i386 |
| TEB selector | GS (via `arch_prctl`) | FS (via `set_thread_area` syscall) |
| Syscall mechanism | `syscall` instruction | `int $0x80` |
| Stack | Separate guest stack (mmap) | Separate guest stack (mmap) |
| Heap backend | musl malloc | custom mmap allocator |
| CRT detection | auto-detect (MinGW/Watcom) | auto-detect (MinGW/Watcom) |
| Entry symbols | main, WinMain, D_DoomMain | main, WinMain, D_DoomMain |
| Signal safety | native sigaction | int 0x80 signal handlers |
| Threading | spinlocks | spinlocks (no pthreads) |

Both paths share the same import resolution logic, TEB/PEB layout, and msvcrt stub
implementations. The PE32-specific bootstrap modules (`pe32_bootstrap.c`, `pe32_process.c`,
`pe32_guest_launch.c`, `pe32_run_guest.S`) adapt the shared loader code for 32-bit ABI.
