# Architecture

Deep-dive into how my_wine loads and runs a PE binary on Linux.

---

## 1. High-Level Data Flow

```
  PE file (hello.exe)
       │
       ▼
  ┌──────────────────────────────────────┐
  │  main() — src/main.c                  │
  │                                       │
  │  1. map_image(path)                   │
  │     open → mmap(file) →              │
  │     parse DOS/NT headers             │
  │     mmap(image_base)                 │
  │     copy sections                    │
  │     mprotect per-section             │
  │                                       │
  │  2. patch_crt_refptrs()              │
  │     .refptr → our stubs              │
  │                                       │
  │  3. resolve_imports()                │
  │     IAT → our functions              │
  │                                       │
  │  4. setup_unix_stack()               │
  │     mmap 128KB UNIX stack            │
  │                                       │
  │  5. setup_teb_peb()                  │
  │     TEB + PEB + GS base              │
  │                                       │
  │  6. setup_stack()                    │
  │     mmap guest stack                 │
  │                                       │
  │  7. setup_seh()                      │
  │     SEH chain in TEB                 │
  │                                       │
  │  8. generate_thunks()                │
  │     12-byte thunks → __wine_dispatch │
  │                                       │
  │  9. setup_signal_handlers()          │
  │     SIGSEGV/SIGBUS for crash handler │
  │                                       │
  │  10. run_guest() → entry point       │
  │      guest code runs in same process │
  │                                       │
  │  NT syscall flow:                    │
  │    thunk → __wine_dispatcher         │
  │      → stack switch to UNIX stack    │
  │      → c_dispatch_syscall() (C)     │
  │      → stack switch back to guest    │
  │      → ret to guest code            │
  └──────────────────────────────────────┘
```

### 1.1 PE File → Mapped Image

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
7. **Set per-section protections** — `mprotect` each section to match
   its `IMAGE_SCN_MEM_READ`/`WRITE`/`EXECUTE` characteristics.
8. **Unmap file** — the original file mapping is discarded.

### 1.2 Import Resolution → Stub Functions

The import resolver works in two passes:

- **Pass 1** — Walk the `IMAGE_IMPORT_DESCRIPTOR` chain. For each
  descriptor, resolve each `IMAGE_THUNK_DATA64` entry against our
  `import_table` (sorted for `bsearch`). Write the resolved function
  address into the descriptor's `FirstThunk` (the IAT).
- **Pass 2** — Scan `.text` for `ff 25` (`jmp *disp32(%rip)`)
  instructions. For each, dereference the IAT pointer to find the
  target. If the target doesn't match what Pass 1 set, patch it using
  four strategies: resolved-address overlap, ILT value match, ILT
  offset/slot match, and positional fallback.

### 1.3 TEB / PEB Setup

```
  GS:0 ──► TEB (Thread Environment Block)
  │
  ├── [TEB_SEH_CHAIN       (0x00)] SEH frame pointer  (set during setup_seh())
  ├── [TEB_TEB_SELF_REF    (0x08)] TEB self-reference
  ├── [TEB_THREAD_PTR      (0x30)] Thread pointer (→ TEB)
  └── [TEB_PEB_PTR         (0x60)] PEB pointer

  PEB (Process Environment Block)
  ├── [PEB_BEING_DEBUGGED  (0x002)] BeingDebugged = 0
  └── [PEB_IMAGE_BASE      (0x008)] ImageBaseAddress → mapped image
```

All TEB and PEB offsets are defined as named constants (`TEB_*`,
`PEB_*`) in `include/nt_constants.h`.

`arch_prctl(ARCH_SET_GS, teb)` makes the guest's `GS` segment
point to the TEB, matching Windows x86_64 expectations.

### 1.4 Guest Stack

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
                     │ map, resolve,   │
                     │ setup TEB/PEB   │
                     │ setup UNIX stack│
                     │ generate thunks │
                     │ setup signals   │
                     │ set GS base     │
                     │ patch __acrt_iob│
                     └────────┬────────┘
                              │
                        run_guest()
                              │
                              ▼
                       guest code runs
                       (same process)
                              │
                       NT syscalls:
                       thunk → __wine_dispatcher
                         → stack switch
                         → c_dispatch_syscall()
                         → stack switch back
                         → ret to guest
                              │
                       guest returns
                              │
                     cleanup_guest()
                     (munmap TEB, PEB, stack, thunks)
```

**Why single process?** The guest PE runs directly in the same
address space that loaded it. The `MAP_FIXED` image mapping is the
live mapping — no copy-on-write duplication. NT syscalls are
intercepted via dynamically generated thunks that call
`__wine_dispatcher` (not via seccomp/SIGSYS). The dispatcher switches
from the guest stack to a dedicated 128KB UNIX stack, calls the C
handler, switches back, and returns to guest code. This is closer to
how Wine itself works — no signal trampolining, no fork overhead.

If the guest crashes (e.g. SIGSEGV), a signal handler catches it and
returns an exit code from the same process.

---

## 3. Direct Syscall Dispatch

### 3.1 The 12-Byte Thunk

Each NT syscall has a dynamically generated **12-byte thunk**:

```
; generated at runtime by thunk_gen.c
; 12 bytes total — compact, no page per thunk

mov  rdi, imm32(NT_NR)   ; 48 C7 C7 XX XX XX XX  (7 bytes)
call __wine_dispatcher   ; E8 XX XX XX XX         (5 bytes)
```

`NT_NR` is the raw NT syscall number (e.g. 0x3D for `NtWriteFile`),
**without** any offset. The thunk loads the syscall number into `RDI`
and jumps directly to `__wine_dispatcher` via a relative `call`.

The IAT entries in the PE point to these thunks so that when guest
code calls `NtWriteFile`, it jumps to the thunk, which loads the NT
syscall number and transfers to the dispatcher.

### 3.2 `__wine_dispatcher` Assembly Trampoline

`__wine_dispatcher` is a small assembly function (in
`src/syscall/dispatcher.S` or equivalent) that bridges the guest
code running on the guest stack to our C handler running on the
UNIX stack:

```
__wine_dispatcher:
    ; 1. Save guest registers (RAX, RCX, RDX, R8, R9, R10, R11, RDI)
    ;    and guest RSP onto the guest stack frame
    ;
    ; 2. Switch to UNIX stack:
    ;    mov  rsp, unix_stack_top
    ;
    ; 3. Call C dispatcher:
    ;    mov  rdi, NT_NR           (already in RDI from thunk)
    ;    mov  rsi, &saved_regs     (pointer to saved guest state)
    ;    call c_dispatch_syscall
    ;
    ; 4. Result is in RAX (set by C handler)
    ;
    ; 5. Switch back to guest stack:
    ;    mov  rsp, saved_rsp
    ;
    ; 6. Restore guest registers
    ;
    ; 7. ret — returns to the instruction after the thunk's `call`
```

The dispatcher is position-independent and called via relative
`call`, so no absolute addresses are baked into the thunk.

### 3.3 `c_dispatch_syscall` C Handler

```
  Guest code: call NtWriteFile
        │
        ▼
  Thunk: mov rdi, 0x3D; call __wine_dispatcher
        │
        ▼
  __wine_dispatcher: save regs, switch to UNIX stack
        │
        ▼
  c_dispatch_syscall(nt_nr, saved_regs)
    │
    ├── read args from RCX/RDX/R8/R9 (Windows x64 ABI)
    ├── switch(nt_nr):
    │     case 0x3D → handler_NtWriteFile(...)
    │     case 0x2A → handler_NtTerminateProcess(...)
    │     case 0x18 → handler_NtAllocateVirtualMemory(...)
    │     ...
    └── write result to RAX
        │
        ▼
  __wine_dispatcher: switch back to guest stack, restore regs, ret
  → guest code continues with result in RAX
```

### 3.4 Handler Implementation

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

File handles are pseudo-handles: `STD_OUTPUT_HANDLE` (0x7FFFFFFE)
maps to Linux fd 1, `STD_ERROR_HANDLE` (0x7FFFFFFD) to fd 2.

---

## 4. Stack Switching

The guest PE runs on its own stack (allocated from the PE's
`SizeOfStackReserve`/`SizeOfStackCommit`). The C dispatcher handlers
run on a separate **128KB UNIX stack**.

### UNIX Stack

- **Size**: 128 KB, allocated via `mmap` with `MAP_STACK`.
- **Alignment**: top-of-stack is 16-byte-aligned (`rsp % 16 == 0`),
  satisfying the System V ABI requirement.
- **Ownership**: allocated during `setup_unix_stack()` in `main()`,
  pointed to by a global `unix_stack_top` pointer.
- **Lifetime**: munmap'd during `cleanup_guest()` after the guest exits.

### Switching Mechanism

When `__wine_dispatcher` is called from a thunk:

1. **Save state** — guest RSP, RAX, RCX, RDX, R8, R9, R10, R11,
   and RDI are saved onto the **guest stack** (the RSP at the time
   of the thunk call).
2. **Switch to UNIX stack** — `rsp` is set to `unix_stack_top`.
3. **Call C handler** — `c_dispatch_syscall(nt_nr, &saved_regs)`
   executes on the UNIX stack with full System V ABI semantics.
4. **Restore guest RSP** — `rsp` is restored from the saved value.
5. **Restore guest regs** — RAX, RCX, RDX, R8, R9, R10, R11, RDI
   are restored from the saved state.
6. **Return** — `ret` returns to the instruction after the thunk's
   `call`, resuming guest code on the guest stack.

### Why Two Stacks?

Guest code runs with the Windows x64 ABI. C handlers use the System V
ABI with assumptions about stack layout, red zone (when applicable),
and frame pointers. Using a separate UNIX stack:

- Prevents C handlers from corrupting guest stack data
- Ensures the UNIX stack is large enough for deep C call chains
- Avoids alignment mismatches (guest stack follows Windows convention;
  UNIX stack follows System V)
- Keeps the switch atomic — `rsp` is the only thing that changes
  between guest and UNIX contexts during the switch

### Saved Register Layout

```
struct saved_regs {
    uint64_t rsp;   // guest RSP at time of thunk entry
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t rdi;
};
```

`c_dispatch_syscall` reads the NT syscall number from `RDI` (loaded by
the thunk) and the Windows ABI arguments from `RCX`, `RDX`, `R8`, `R9`
in the saved registers. The result is written back to `RAX` in the
saved struct, which the assembly trampoline copies to the real `RAX`
before returning to guest code.

---

## 5. force_align_arg_pointer and WINE_STUB

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

### WINE_STUB vs WINE_STUB_STATIC

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
