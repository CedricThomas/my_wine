# Architecture

Deep-dive into how my_wine loads and runs a PE binary on Linux.

---

## 1. High-Level Data Flow

```
  PE file (hello.exe)
       │
       ▼
  ┌──────────────────────────────────────────────────┐
  │  main() — src/main.c                             │
  │                                                   │
  │  1.  map_image(path)                              │
  │      open → mmap(file) → parse headers            │
  │      mmap(image_base) → copy sections → mprotect  │
  │                                                   │
  │  2.  init_msvcrt_imports() + init_import_table()  │
  │      Build and sort the import lookup table       │
  │                                                   │
  │  3.  patch_crt_refptrs()                          │
  │      .refptr → our stubs                          │
  │                                                   │
  │  4.  resolve_imports()                            │
  │      Pass 1: IAT from Import Descriptors           │
  │      Pass 2: scan .text for ff 25 thunks           │
  │                                                   │
  │  5.  setup_teb_peb()                              │
  │      Allocate TEB + PEB (does NOT set GS base)    │
  │                                                   │
  │  6.  setup_stack()                                │
  │      Allocate guest stack                         │
  │                                                   │
  │  7.  zero .data section                           │
  │                                                   │
  │  8.  seed_bss_vars()                              │
  │      Pre-seed argc/argv/envp in .bss              │
  │                                                   │
  │  9.  build guest argv/envp                        │
  │                                                   │
  │  10. run_guest_entry()                            │
  │      → setup_guest_and_run() in guest_setup.c:    │
  │        - setup_signal_handlers() (POSIX + altstack)│
  │        - setup_seh_and_thunks() (SEH + 23-byte thunks + unix stack)
  │        - parse_pe_headers() (re-parse from entry) │
  │        - apply_final_patches() (__acrt_iob, .bss mprotect)
  │        - finalize_guest_state() (GS base + TEB SEH)
  │        - jump_to_guest() → run_guest()            │
  │      → PE entry point runs in same process        │
  │                                                   │
  │  NT syscall flow:                                 │
  │    23-byte thunk                                  │
  │      push rdi                                     │
  │      mov rdi, NT_NR                               │
  │      mov rax, dispatcher_addr (absolute)          │
  │      call rax → __wine_dispatcher                 │
  │        → save guest regs to __wine_guest_regs     │
  │        → switch to UNIX stack                     │
  │        → c_dispatch_syscall(nr)                   │
  │        → restore guest regs, restore guest stack  │
  │        → ret → thunk's `pop rdi` → ret to caller  │
  └──────────────────────────────────────────────────┘
```

### 1.1 PE File → Mapped Image

`map_image()` in `src/loader/image_mapper.c`:

1. **Open file** — `open(path, O_RDONLY)`.
2. **Map file read-only** — `mmap(file_base, file_size, PROT_READ,
   MAP_PRIVATE, fd, 0)`.
3. **Parse headers** — validate DOS header (`MZ`), find PE signature,
   read `IMAGE_NT_HEADERS64`, read section table.
4. **Map image** — `mmap(image_base, SizeOfImage,
   PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,
   -1, 0)`. Falls back to `MAP_STACK` if preferred base is taken.
5. **Copy section data** — for each section with `SizeOfRawData > 0`,
   `memcpy(dest = base + VirtualAddress, src = file + PointerToRawData,
   SizeOfRawData)`.
6. **Copy headers** — the DOS header, PE signature, and section table
   are copied into the image. Section pointers are re-pointed into the
   live image.
7. **Apply base relocations** — `apply_relocations()` walks the
   `.reloc` directory and patches `DIR64` entries (add delta to
   64-bit pointers) when actual base ≠ preferred `ImageBase`. No-Op
   if loaded at preferred base. Fails if `RELOCS_STRIPPED` and
   base differs.
8. **Set per-section protections** — `mprotect` each section to match
   its `IMAGE_SCN_MEM_READ`/`WRITE`/`EXECUTE` characteristics.
9. **Unmap file** — the original file mapping is discarded.

### 1.2 Import Resolution → Stub Functions

The import resolver (`src/loader/import_resolve.c`) works in two passes:

- **Pass 1** — Walk the `IMAGE_IMPORT_DESCRIPTOR` chain. For each
  descriptor, resolve each `IMAGE_THUNK_DATA64` entry (by name or
  ordinal) using a three-tier resolver:
  1. Lookup in our stub `import_table` (binary search).
  2. Lookup in loaded module exports (`lookup_export`).
  3. Return `NULL` (unresolved).  Write
  the resolved function address into the descriptor's `FirstThunk`
  (the IAT).

- **Dynamic loading** — `LoadLibraryA` (in `kernel32_module.c`) calls
  `load_dll()` (in `import_resolve.c`) which: maps the DLL via
  `map_image_at()` at a controlled base below 4 GB, applies
  relocations, registers in the module list + PEB LDR, resolves
  imports recursively (up to `MAX_IMPORT_DEPTH`), and parses the
  export table. `FreeLibraryA` decrements the load count and unmaps
  the image when it reaches zero. `GetProcAddress` uses
  `lookup_export()` with binary search on the export cache.

- **Pass 2** — Scan `.text` for `ff 25` (`jmp *disp32(%rip)`)
  instructions. Collect and deduplicate unique IAT target addresses.
  Build a flat import array from all resolved entries. Patch any
  mismatched IAT targets using four strategies, tried in order:
  1. **Resolved-address overlap** — does the current IAT value match
     any resolved function address in the flat array?
  2. **ILT value match** — does the current IAT value match any
     OriginalFirstThunk value in the flat array?
  3. **ILT offset/slot match** — is the current IAT pointer within
     the import directory range and does the value match an ILT entry?
  4. **Positional fallback** — match by position in the collected
     thunk-target array against the flat import array.

### 1.3 TEB / PEB Setup

`setup_teb_peb()` in `src/loader/teb_peb.c`:

```
  GS:0 ──► TEB (Thread Environment Block)
  │
  ├── [TEB_SEH_CHAIN       (0x00)] SEH frame pointer  (set in finalize_guest_state)
  ├── [TEB_TEB_SELF_REF    (0x08)] TEB self-reference
  ├── [TEB_THREAD_PTR      (0x30)] Thread pointer (→ TEB)
  └── [TEB_PEB_PTR         (0x60)] PEB pointer

  PEB (Process Environment Block)
  ├── [PEB_BEING_DEBUGGED  (0x002)] BeingDebugged = 0
  ├── [PEB_IMAGE_BASE      (0x008)] ImageBaseAddress → mapped image
  ├── [PEB_LDR             (0x018)] → PEB_LDR_DATA
  │   └── Three doubly-linked lists (load, memory, initialization order)
  └── [PEB_PROCESS_HEAP    (0x030)] → default process heap (musl malloc)
```

All TEB and PEB offsets are defined as named constants (`TEB_*`,
`PEB_*`) in `include/nt_constants.h`.

**GS base is NOT set in `setup_teb_peb()`**. The GS segment must
continue pointing to Linux TLS for glibc calls during setup. GS base
is set to the TEB in `finalize_guest_state()` (inside
`setup_guest_and_run()` in `guest_setup.c`), right before jumping to
guest code. The `set_gs_base()` function (`src/loader/gs_base.c`) has
a fallback strategy: try `arch_prctl(ARCH_SET_GS)`, verify with
`arch_prctl(ARCH_GET_GS)`, and if the value doesn't match, fall back
to the `wrgsbase` instruction (verified with `rdgsbase`).

### 1.4 Guest Stack

`setup_stack()` in `src/loader/teb_peb.c`:

The stack is allocated from the PE's `SizeOfStackReserve`/`SizeOfStackCommit`
values (with a minimum commit of 512 KB for CRT startup). It grows
downward; the top-of-stack pointer is 16-byte-aligned as required by
the x86_64 ABI (`rsp % 16 == 8` before `call`).

---

## 2. Single-Process Model

`main()` runs entirely in a **single process**. There is no `fork()` —
the guest PE loads and executes in the same process that orchestrated
the loading.

```
                     ┌──── main() ────┐
                     │ map_image       │
                     │ init imports    │
                     │ patch refptrs   │
                     │ resolve imports │
                     │ setup_teb_peb   │
                     │ setup_stack     │
                     │ zero .data      │
                     │ seed_bss_vars   │
                     │ build argv/envp │
                     │ find entry point│
                     └────────┬────────┘
                              │
                     run_guest_entry()
                              │
                     ┌────────▼──────────┐
                     │ setup_guest_and_run│
                     │ (guest_setup.c)   │
                     │                   │
                     │ setup_signal_handlers()
                     │ setup_seh_and_thunks()
                     │ finalize_guest_state()
                     │ apply_final_patches()
                     │                   │
                     │ run_guest()       │
                     └────────┬──────────┘
                              │
                              ▼
                       guest code runs
                       (same process)
                              │
                       NT syscalls:
                       23-byte thunk
                         → __wine_dispatcher
                           → save regs to global
                           → switch to UNIX stack
                           → c_dispatch_syscall(nr)
                           → restore regs & stack
                           → ret → pop rdi → ret
                              │
                       guest returns
                       (→ ExitProcess → NtTerminateProcess → exit)
                              │
                     cleanup_guest()
                     (munmap TEB, PEB, stack, thunks, unix stack)
```

**Why single process?** The guest PE runs directly in the same
address space that loaded it. The `MAP_FIXED` image mapping is the
live mapping — no copy-on-write duplication. NT syscalls are
intercepted via dynamically generated 23-byte thunks that call
`__wine_dispatcher` (not via seccomp/SIGSYS). The dispatcher saves
guest registers into the global `__wine_guest_regs`, switches from
the guest stack to a dedicated 128KB UNIX stack, calls the C handler,
switches back, restores registers, and returns to guest code. This is
closer to how Wine itself works — no signal trampolining, no fork
overhead.

If the guest crashes (e.g. SIGSEGV), a signal handler catches it and
exits from the same process.

---

## 3. PE32 Dual-Process Model

> For full details, see [PE32.md](PE32.md).

### Why Dual-Process?

Linux blocks `ljmp`/`lcall` to a 32-bit code segment at CPL=3 in a 64-bit process. A 32-bit PE **cannot** execute inside a 64-bit ELF — it must run in a native 32-bit process. The in-process mode-switch approach was tried and found fundamentally unfixed.

### Architecture

`my_wine` (64-bit) detects PE32 → forks + execs `my_wine_32` (a 32-bit static ELF built with `-static -nostartfiles`). Communication is via `WINE32_PE_PATH` env var. The parent `waitpid()`s and returns the child's exit code. No IPC, no shared memory.

### `my_wine_32` Entry Point

`_start` (`pe32_entry.S`) → `wine32_main()` (`pe32_entry.c`), which independently:
- Maps PE from disk, allocates TEB32/PEB32 at fixed 32-bit addresses
- Generates 15-byte thunks, resolves imports, seeds BSS vars
- Sets up **FS → TEB via `set_thread_area`** (syscall 243, LDT-based)
- Jumps to PE entry via `pe32_run_guest.S`

### Key Differences From Single-Process (PE32+)

| | PE32+ (single) | PE32 (dual) |
|---|---|---|
| Thunk size | 23 bytes | 15 bytes: `push rdi; mov rdi,nr; mov eax,dispatcher; call eax; pop rdi; ret` |
| Syscall | `syscall` (RAX) | `int $0x80` (EAX) |
| mmap | `mmap` (syscall 9) | `mmap2` (syscall 192) |
| TEB base | GS via `arch_prctl` / `wrgsbase` | FS via `set_thread_area` (LDT, syscall 243) |
| Calling ABI | Microsoft x64 (RCX/RDX/R8/R9) | cdecl (stack-based) |
| libc calls | glibc available | No — all replaced with `INLINE_SYSCALL_*` macros (no TLS in `-nostartfiles`) |

### Process Flow

```
my_wine (64-bit)                      my_wine_32 (32-bit static ELF)
 ──────────────────                      ──────────────────────────────────
  detect PE32                              _start (pe32_entry.S)
  fork() ─── exec("my_wine_32") ─────►     wine32_main() (pe32_entry.c)
  setenv(WINE32_PE_PATH)                         ├─ map_image()
  waitpid()                                      ├─ setup_teb_peb()
  │                                              ├─ set_thread_area(FS → TEB)
  │                                              ├─ resolve_imports()
  │                                              ├─ generate_all_thunks() (15-byte)
  │                                              ├─ seed_bss_vars()
  │                                              └─ pe32_run_guest()
  │                                                    │
  │                                              guest code runs (32-bit)
  │                                              syscalls via int $0x80
  │                                                    │
  ◄─── exit(child_code) ───────────────────────────────┘
  return child exit code
```

---

## 4. Direct Syscall Dispatch

### 4.1 The 23-Byte Thunk

Each NT syscall has a dynamically generated **23-byte thunk**
(`src/syscall/thunk_gen.c`):

```
; generated at runtime by thunk_gen.c
; 23 bytes total

push rdi                   ; 41 57        (2 bytes)  — save original RDI
mov rdi, imm32(NT_NR)      ; 48 C7 C7 XX (7 bytes)  — load syscall number
mov rax, imm64(dispatcher) ; 48 B8 XX..XX(10 bytes) — load dispatcher address (absolute)
call rax                   ; FF D0        (2 bytes)  — indirect call to dispatcher
pop rdi                    ; 5F           (1 byte)   — restore original RDI
ret                        ; C3           (1 byte)   — return to guest caller
```

`NT_NR` is the raw NT syscall number (e.g. 0x3D for `NtWriteFile`),
**without** any offset. The thunk loads the syscall number into `RDI`
and jumps to `__wine_dispatcher` via an **absolute indirect call**
(`mov rax, imm64; call rax`) — not a relative `E8` displacement. This
avoids overflow when the thunk blob and dispatcher are more than 2 GB
apart due to ASLR.

The thunk saves/restores `RDI` with `push`/`pop` around the dispatcher
call. This means the dispatcher's `ret` lands on the thunk's `pop rdi`
instruction, which restores `RDI` before the final `ret` back to the
guest caller.

The IAT entries in the PE point to these thunks so that when guest
code calls `NtWriteFile`, it jumps to the thunk, which loads the NT
syscall number, calls the dispatcher, and returns.

### 4.2 `__wine_dispatcher` Assembly Trampoline

`__wine_dispatcher` (in `src/syscall/dispatcher_entry_asm.S`) bridges
guest code on the guest stack to our C handler on the UNIX stack:

```
; On entry: RDI = syscall number, RSP = guest stack
;           [RSP] = return address (pushed by thunk's `call`)
;           [RSP+8] = original RDI (pushed by thunk's `push rdi`)
;           RCX/RDX/R8/R9 = guest args

__wine_dispatcher:
    ; 1. Save guest state to __wine_guest_regs (RIP-relative global)
    mov (%rsp), %rax
    mov %rax, __wine_guest_regs+RET_ADDR_OFF(%rip)  ; return address

    mov %rsp, __wine_guest_regs+RSP_OFF(%rip)       ; guest RSP

    mov %rcx, __wine_guest_regs+RCX_OFF(%rip)       ; arg 1
    mov %rdx, __wine_guest_regs+RDX_OFF(%rip)       ; arg 2
    mov %r8,  __wine_guest_regs+R8_OFF(%rip)        ; arg 3
    mov %r9,  __wine_guest_regs+R9_OFF(%rip)        ; arg 4

    mov %rsi, __wine_guest_regs+RSI_OFF(%rip)       ; callee-saved

    ; RDI is NOT saved here — the thunk handles it with push/pop

    ; 2. Switch to UNIX stack (RIP-relative global, subtract 8 for ABI)
    mov unix_stack_ptr_val(%rip), %rsp
    sub $8, %rsp

    ; 3. Call C dispatcher (RDI already = syscall_nr from thunk)
    call c_dispatch_syscall

    ; 4. Save result to __wine_guest_regs.rax
    mov %rax, __wine_guest_regs+RAX_OFF(%rip)

    ; 5. Switch back to guest stack
    mov __wine_guest_regs+RSP_OFF(%rip), %rsp

    ; 6. Restore guest registers
    mov __wine_guest_regs+RCX_OFF(%rip), %rcx
    mov __wine_guest_regs+RDX_OFF(%rip), %rdx
    mov __wine_guest_regs+R8_OFF(%rip), %r8
    mov __wine_guest_regs+R9_OFF(%rip), %r9
    ; RDI NOT restored — thunk's `pop rdi` handles it
    mov __wine_guest_regs+RSI_OFF(%rip), %rsi
    mov __wine_guest_regs+RAX_OFF(%rip), %rax

    ; 7. ret → lands on thunk's `pop rdi`, then thunk's `ret` → guest
    ret
```

The dispatcher is position-independent (all accesses use `%rip`-relative
offsets) and does not use the red zone.

### 4.3 `c_dispatch_syscall` C Handler

`c_dispatch_syscall()` in `src/syscall/dispatcher.c`:

```
  Guest code: call NtWriteFile
        │
        ▼
  23-byte thunk: push rdi; mov rdi, 0x3D; mov rax, dispatcher; call rax
        │
        ▼
  __wine_dispatcher: save regs to __wine_guest_regs, switch to UNIX stack
        │
        ▼
  c_dispatch_syscall(nr)  ← reads from __wine_guest_regs global

    ; Arguments from global __wine_guest_regs:
    arg1 = __wine_guest_regs.rcx
    arg2 = __wine_guest_regs.rdx
    arg3 = __wine_guest_regs.r8
    arg4 = __wine_guest_regs.r9
    arg5+ = read_guest_stack(n)  ← uses __wine_guest_regs.rsp

    switch(nt_nr):
      case NtWriteFile   → handler_NtWriteFile(...)
      case NtTerminateProcess → handler_NtTerminateProcess(...)
      case NtAllocateVirtualMemory → handler_NtAllocateVirtualMemory(...)
      ...

    ; Result written back to __wine_guest_regs.rax
    __wine_guest_regs.rax = result

        │
        ▼
  __wine_dispatcher: switch back to guest stack, restore regs, ret
  → thunk's `pop rdi` (restores RDI), `ret` (returns to guest caller)
  → guest code continues with result in RAX
```

### 4.4 `__wine_guest_regs` — Global Saved State

`__wine_guest_regs` is a **global struct** (defined in
`src/syscall/dispatcher_entry.c`, declared in
`include/syscall/dispatcher_entry.h`), not a parameter:

```c
struct guest_regs {
    uint64_t rcx;       // guest input arg 1
    uint64_t rdx;       // guest input arg 2
    uint64_t r8;        // guest input arg 3
    uint64_t r9;        // guest input arg 4
    uint64_t rdi;       // guest callee-saved (clobbered by thunk)
    uint64_t rsi;       // guest callee-saved (clobbered by dispatcher)
    uint64_t rsp;       // original guest RSP at dispatch time
    uint64_t ret_addr;  // return address pushed by the thunk call
    uint64_t rax;       // output: result for guest
};

struct guest_regs __wine_guest_regs = {0};
```

`c_dispatch_syscall(nr)` reads arguments from this global struct
directly — the syscall number comes from `RDI` (the C function's
`rdi` parameter per System V ABI, which is already set by the thunk).

### 4.5 Handler Implementation

NT handlers (`src/stubs/ntdll_*.c`) implement Windows syscalls using
Linux primitives:

| NT Handler | Linux Implementation |
|---|---|
| `NtWriteFile` | `write(fd, buf, len)` via syscall |
| `NtReadFile` | `read(fd, buf, len)` via syscall |
| `NtTerminateProcess` | `exit(code)` via syscall |
| `NtAllocateVirtualMemory` | `mmap()` |
| `NtFreeVirtualMemory` | `munmap()` |
| `NtCreateSection` | `mmap(MAP_ANONYMOUS)` |
| `NtMapViewOfSection` | `mmap()` from section |
| `NtUnmapViewOfSection` | `munmap()` |
| `NtQueryInformationProcess` | synthetic response |
| `NtCallbackReturn` | no-op |
| `NtClose` | no-op (handles are pseudo) |
| `NtGetContextThread` | synthetic context dump |
| `NtSetContextThread` | context restore |
| `NtCreateEvent` | pseudo handle allocation |
| `NtCreateThreadEx` | `clone()` real threads |
| `NtOpenFile` | `open()` with attribute parsing |
| `NtQuerySystemTime` | `clock_gettime(CLOCK_REALTIME)` → FILETIME |
| `NtQueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` |
| `NtQueryPerformanceFrequency` | constant `10^7` |
| `NtDelayExecution` | `nanosleep()` |
| `NtCreateMutex` | `pthread_mutex_init()` |
| `NtSetEvent` | `pthread_cond_broadcast()` |
| `NtResetEvent` | clear signaled flag |
| `NtWaitForSingleObject` | spin-sleep loop with timeout |
| `NtReleaseMutex` | `pthread_mutex_unlock()` |

File handles are pseudo-handles: `STD_OUTPUT_HANDLE` (0x7FFFFFFE)
maps to Linux fd 1, `STD_ERROR_HANDLE` (0x7FFFFFFD) to fd 2.

---

## 5. Stack Switching

The guest PE runs on its own stack (allocated from the PE's
`SizeOfStackReserve`/`SizeOfStackCommit`). The C dispatcher handlers
run on a separate **128KB UNIX stack**.

### UNIX Stack

- **Size**: 128 KB, allocated via `mmap` with `MAP_PRIVATE | MAP_ANONYMOUS`.
- **Alignment**: top-of-stack is 16-byte-aligned (`rsp % 16 == 0`),
  satisfying the System V ABI requirement. The dispatcher subtracts 8
  before calling C code so that `rsp % 16 == 8` on entry to the callee.
- **Ownership**: allocated by `setup_unix_stack()` in
  `src/syscall/dispatcher_entry.c` (called from `setup_seh_and_thunks()`
  in `guest_setup.c`), pointed to by the global `unix_stack_ptr_val`.
- **Lifetime**: munmap'd during `cleanup_guest()` after the guest exits.

### Switching Mechanism

When `__wine_dispatcher` is called from a thunk:

1. **Save state** — RSP, RAX, RCX, RDX, R8, R9, RSI, and the return
   address are saved into the global `__wine_guest_regs` struct (via
   RIP-relative stores). RDI is NOT saved by the dispatcher — the
   thunk's `push rdi`/`pop rdi` wrapping handles it.
2. **Switch to UNIX stack** — `rsp` is set to `unix_stack_ptr_val - 8`
   (the `-8` ensures `rsp % 16 == 8` on entry to `c_dispatch_syscall`).
3. **Call C handler** — `c_dispatch_syscall(nr)` executes on the UNIX
   stack with full System V ABI semantics. It reads arguments from the
   global `__wine_guest_regs` struct.
4. **Save result** — return value stored in `__wine_guest_regs.rax`.
5. **Restore guest RSP** — `rsp` is restored from `__wine_guest_regs.rsp`.
6. **Restore guest regs** — RCX, RDX, R8, R9, RSI, RAX restored from
   `__wine_guest_regs`. RDI is NOT restored here — the thunk's `pop rdi`
   after `ret` handles it.
7. **Return** — `ret` returns to the thunk's `pop rdi`, which restores
   the original RDI, then the thunk's `ret` returns to the guest caller.

### Why Two Stacks?

Guest code runs with the Windows x64 ABI. C handlers use the System V
ABI with assumptions about stack layout, red zone, and frame pointers.
Using a separate UNIX stack:

- Prevents C handlers from corrupting guest stack data
- Ensures the UNIX stack is large enough for deep C call chains
- Avoids alignment mismatches (guest stack follows Windows convention;
  UNIX stack follows System V)
- Keeps the switch atomic — `rsp` is the only thing that changes
  between guest and UNIX contexts during the switch

---

## 6. Crash Handlers

`src/loader/crash_handlers.c` installs both Windows-style SEH handlers
and POSIX signal handlers.

### SEH Handler

`seh_crash_handler()` is a function with `ms_abi` that receives
`(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext)`
per the Microsoft x64 ABI. It dumps a message via inline syscall and
exits with the exception code. It is wired into a static SEH frame
during `setup_seh_and_thunks()` and linked to the TEB at
`TEB_SEH_CHAIN` in `finalize_guest_state()`.

### POSIX Signal Handlers

`setup_signal_handlers()` installs `crash_handler` for:
`SIGSEGV`, `SIGILL`, `SIGABRT`, `SIGFPE`, `SIGBUS`, and `SIGTRAP`.

It uses `SA_SIGINFO` with a `siginfo_t`/`ucontext_t` handler that
dumps register state (RIP, RSP, RAX) via inline syscalls and exits
with code 139.

An **alternate signal stack** is set up via `mmap` + `sigaltstack`
(64KB) before handlers are installed. This ensures that even if a
crash occurs during handler installation itself (e.g., glibc TLS access
with wrong GS base), the handler runs on a safe stack.

The crash handler uses `INLINE_SYSCALL_EXIT` (direct `exit` syscall)
instead of `_exit()` because after GS base points to the TEB, glibc's
TLS access via GS-relative offsets will crash.

---

## 7. Jumping to Guest Code

### `run_guest_entry()` in `src/loader/entry.c`

This is the final step from `main()`. It simply calls
`setup_guest_and_run()` (defined in `guest_setup.c`) and never returns.

### `setup_guest_and_run()` in `src/loader/guest_setup.c`

The orchestrator for the final guest transition:

1. **`setup_signal_handlers()`** — install POSIX signal handlers +
   alternate signal stack.

2. **`setup_seh_and_thunks()`** — create static SEH frame, call
   `generate_all_thunks()` to create 23-byte thunks in a single
   mmap'd executable blob, call `setup_unix_stack()` to allocate the
   128KB UNIX stack.

3. **`parse_pe_headers()`** — re-parse PE headers from the entry
   point address (to get section info for patches).

4. **`apply_final_patches()`** — patch `__acrt_iob_func` thunk to
   return `__wine_iob_data` directly (15-byte patch: `movabs rax, imm64;
   ret; 4x NOP`), then ensure all writable sections have `PROT_WRITE`
   via `mprotect`.

5. **`finalize_guest_state()`** — set GS base to TEB (with
   arch_prctl → wrgsbase fallback), wire SEH frame into TEB at
   `TEB_SEH_CHAIN`.

6. **`jump_to_guest()`** — look up `ExitProcess` from the import table,
   call `run_guest()`.

### `run_guest()` in `src/run_guest.S`

A naked assembly trampoline that performs **System V → MS ABI register
remapping** and jumps to the PE entry point:

```
; SysV input: rdi=entry, rsi=stack_top, rdx=peb, rcx=guest_argv,
;             r8=guest_envp, r9=exit_process_fn

run_guest:
    mov %rsi, %rsp           ; switch to guest stack
    mov %r9, %r15            ; save ExitProcess in r15 (callee-saved)
    mov %rcx, %r9            ; save guest_argv (rcx will be overwritten)
    mov $1, %rcx             ; argc = 1 (MS ABI arg1 = rcx)
    mov %r9, %rdx            ; guest_argv → rdx (MS ABI arg2)
    ; guest_envp already in r8 (MS ABI arg3)
    call *%rdi               ; call entry()
    mov %rax, %rcx           ; uExitCode = main()'s return value
    call *%r15               ; ExitProcess(uExitCode)
    hlt                      ; should never reach here
```

Key behaviors:
- **Stack switch**: immediately switches `rsp` to the guest stack.
- **ABI remapping**: converts System V calling convention (rdi, rsi, rdx, rcx)
  to Microsoft x64 (rcx=argc, rdx=argv, r8=envp).
- **ExitProcess in r15**: the `ExitProcess` function pointer is saved in
  callee-saved `r15` so it survives across the entry call.
- **Post-entry**: when `entry()` returns (i.e., `main()` returns), the
  return value is passed to `ExitProcess` to terminate cleanly.

---

## 8. Guest Setup Flow (Detailed)

The complete flow inside `setup_guest_and_run()`:

```
setup_guest_and_run(entry_abs, stack_top, teb, guest_argv, guest_envp)
│
├── setup_signal_handlers()
│   ├── mmap(64KB) → alternate signal stack
│   ├── sigaltstack()
│   └── sigaction(SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP)
│
├── setup_seh_and_thunks()
│   ├── guest_seh_frame[0] = 0 (end of chain)
│   ├── guest_seh_frame[1] = &seh_crash_handler
│   ├── generate_all_thunks()
│   │   ├── mmap(executable blob)
│   │   ├── for each NT syscall: write 23-byte thunk
│   │   │   └── push rdi; mov rdi,nr; mov rax,dispatcher; call rax; pop rdi; ret
│   │   └── store thunk pointers indexed by syscall number
│   └── setup_unix_stack()
│       └── mmap(128KB) → unix_stack_ptr_val = base + 128KB
│   └── return seh_frame
│
├── parse_pe_headers(entry_abs) → nt, sections
│
├── apply_final_patches(base, nt, sections)
│   ├── patch_acrt_iob()
│   │   ├── find_text_thunk(base, nt, sections, __iob_func)
│   │   │   └── scan .text for ff 25 thunks, deref IAT, match __iob_func
│   │   ├── validate opcode (must be ff 25)
│   │   ├── bounds check (15 bytes within .text)
│   │   └── mprotect(RW) → write patch (movabs rax, iob_addr; ret; NOP×4)
│   │                     → mprotect(REX)
│   └── mprotect all writable sections to ensure PROT_WRITE
│
├── finalize_guest_state(teb, seh_frame)
│   ├── set_gs_base(teb)
│   │   ├── arch_prctl(ARCH_SET_GS, teb)
│   │   ├── arch_prctl(ARCH_GET_GS) → verify
│   │   └── if mismatch: wrgsbase (verify with rdgsbase)
│   └── *(void**)(teb + TEB_SEH_CHAIN) = seh_frame
│
└── jump_to_guest(entry_abs, stack_top, guest_argv, guest_envp)
    ├── find ExitProcess in import_table
    └── run_guest(entry, stack_top, NULL, guest_argv, guest_envp, exit_fn)
        (in run_guest.S — stack switch, ABI remap, call entry,
         call ExitProcess on return, hlt if ExitProcess returns)
```

---

## 9. Cleanup

`cleanup_guest()` in `src/loader/guest_setup.c` is called from
`NtTerminateProcess` to reclaim all guest resources:

1. **Unmap PEB** (separate page from TEB, read from `teb + TEB_PEB_PTR`)
2. **Unmap TEB**
3. **Unmap guest stack** (if `g_stack_base` set)
4. **Unmap thunk pages** (via `cleanup_thunk_pages()` in `thunk_gen.c`)

Note: the UNIX stack is cleaned up by `cleanup_unix_stack()` in
`dispatcher_entry.c`.

---

## 10. Key File References

| File | Purpose |
|---|---|
| `src/main.c` | Orchestrator — 10-step pipeline |
| `src/loader/image_mapper.c` | `map_image()` — open, mmap, parse, copy sections, mprotect |
| `src/loader/teb_peb.c` | `setup_teb_peb()` + `setup_stack()` — allocate TEB, PEB, guest stack |
| `src/loader/guest_setup.c` | `setup_guest_and_run()` — signal handlers, SEH, thunks, __acrt_iob, GS base, jump to guest |
| `src/loader/entry.c` | `run_guest_entry()` — wrapper that calls `setup_guest_and_run()` |
| `src/run_guest.S` | Naked assembly trampoline — stack switch + ABI remap + ExitProcess |
| `src/syscall/dispatcher_entry_asm.S` | `__wine_dispatcher` — save regs to `__wine_guest_regs`, stack switch, call C |
| `src/syscall/dispatcher_entry.c` | `__wine_guest_regs` global, `unix_stack_ptr_val`, `setup_unix_stack()` |
| `src/syscall/dispatcher.c` | `c_dispatch_syscall(nr)` — read from `__wine_guest_regs`, dispatch to handlers |
| `src/syscall/thunk_gen.c` | 23-byte thunk generation with absolute indirect call |
| `src/loader/crash_handlers.c` | SEH + POSIX signal handlers (SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP) |
| `src/loader/gs_base.c` | `set_gs_base()` — arch_prctl → wrgsbase fallback |
| `src/loader/import_resolve.c` | `resolve_imports()` — pass 1 (ILT/IAT walk) + pass 2 (`.text` scan with 4 strategies); `load_dll()` + `find_dll_path()` + `resolve_module_imports()` |
| `src/loader/relocations.c` | `apply_relocations()` — DIR64 base relocation patches |
| `src/loader/module_list.c` | Module registry — `add_module`, `find_module_by_name/addr`, `remove_module` |
| `src/loader/export_table.c` | `parse_export_table()`, `lookup_export()` (binary search), `lookup_export_by_ordinal()` |
| `src/loader/peb_ldr.c` | PEB_LDR_DATA management — `ldr_add_module`, `ldr_remove_module`, three linked lists |
| `src/loader/ordinal_table.c` | Ordinal import name lookup (ntdll/kernel32/msvcrt) |
| `src/heap/wine_heap.c` | `HeapCreate/Alloc/Free/ReAlloc/Destroy/Size/GetProcessHeap` (musl malloc backend) |

---

## 11. `force_align_arg_pointer` and `WINE_STUB`

### What `force_align_arg_pointer` Does

The GCC attribute `force_align_arg_pointer` tells the compiler to
ensure that the stack pointer (`rsp`) is aligned to 16 bytes at the
beginning of the function body. This is done by inserting a `sub
$8, %rsp` (or equivalent) at function entry and `add $8, %rsp`
at every exit path.

### Why It's Needed for `ms_abi` Functions

The [Windows x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
requires the stack to be **16-byte aligned before a `call`
instruction**. Specifically, on entry to any function, `rsp % 16 ==
8` (the 8-byte return address is on the stack, making `rsp` nominally
misaligned until the callee adjusts it).

Our stub functions use `__attribute__((ms_abi))` to follow the
Windows x64 calling convention (arguments in `RCX`, `RDX`, `R8`,
`R9`). However, they are *called from Linux code* that uses the
System V ABI (arguments in `RDI`, `RSI`, `RDX`, `RCX`). The System V
ABI only guarantees 16-byte alignment before `call`, but doesn't
guarantee the specific `rsp % 16 == 8` invariant that Windows code
expects.

`force_align_arg_pointer` fixes this by adding a prologue/epilogue
adjustment that enforces the 16-byte alignment regardless of how the
function was called.

### `WINE_STUB` vs `WINE_STUB_STATIC`

Defined in `include/wine_abi.h`:

```c
#define WINE_STUB        __attribute__((ms_abi, force_align_arg_pointer))
#define WINE_STUB_STATIC static __attribute__((ms_abi, force_align_arg_pointer))
```

- **`WINE_STUB`** — for non-static functions that appear in the
  import table and are called directly from guest PE code
  (e.g., `GetStdHandle`, `WriteFile`, `__iob_func`).
- **`WINE_STUB_STATIC`** — for internal helper functions within stub
  files that are called from other stubs but never directly from the
  PE (e.g., `wine_vfprintf`, `wine_malloc`).

Both use `ms_abi` to ensure correct argument passing when called from
guest code. The `force_align_arg_pointer` ensures the stack is
properly aligned regardless of the caller's ABI.

### Key Detail: `-mno-red-zone`

Stub files are compiled with `-mno-red-zone`. The System V ABI allows
leaf functions to use 128 bytes below `rsp` (the "red zone") without
adjusting `rsp`. The Windows x64 ABI has no red zone. Compiling with
`-mno-red-zone` prevents the compiler from using this space, avoiding
conflicts when stub code runs in the guest context where the stack
layout is controlled by the PE.

---

## Related Documents

- [Onboarding](onboarding.md) — Getting started guide and reading order
- [PE Format Primer](pe_format.md) — PE structure basics
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [CRT refptr Patching](refptr.md) — .refptr deep-dive
