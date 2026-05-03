# Architecture

Deep-dive into how my_wine loads and runs a PE binary on Linux.

---

## 1. High-Level Data Flow

```
  PE file (hello.exe)
       │
       ▼
  ┌─────────────────────────────┐
  │  main() — src/main.c        │
  │                             │
  │  1. map_image(path)         │
  │     open → mmap(file) →     │
  │     parse DOS/NT headers    │
  │     mmap(image_base)        │
  │     copy sections           │
  │     mprotect per-section    │
  │                             │
  │  2. patch_crt_refptrs()     │
  │     .refptr → our stubs     │
  │                             │
  │  3. resolve_imports()       │
  │     IAT → our functions     │
  │                             │
  │  4. setup_teb_peb()         │
  │     TEB + PEB + GS base     │
  │                             │
  │  5. setup_stack()           │
  │     mmap guest stack        │
  │                             │
  │  6. jump_to_entry()         │
  │     fork()                  │
  │                             │
  │     parent ──► waitpid()    │
  │     child  ──► run_guest()  │
  └─────────────────────────────┘
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
  ├── [0x00] SEH frame pointer  (set by child in entry.c)
  ├── [0x08] TEB self-reference
  ├── [0x30] Thread pointer (→ TEB)
  └── [0x60] PEB pointer

  PEB (Process Environment Block)
  ├── [0x002] BeingDebugged = 0
  └── [0x008] ImageBaseAddress → mapped image
```

`arch_prctl(ARCH_SET_GS, teb)` makes the guest's `GS` segment
point to the TEB, matching Windows x86_64 expectations.

### 1.4 Guest Stack

The stack is allocated from the PE's `SizeOfStackReserve`/`SizeOfStackCommit`
values (with a minimum commit of 512 KB for CRT startup). It grows
downward; the top-of-stack pointer is 16-byte-aligned as required by
the x86_64 ABI (`rsp % 16 == 8` before `call`).

---

## 2. The fork() Model

`main()` runs entirely in the **parent** process. The heavy lifting
(mapping, import resolution, TEB/PEB) happens before `fork()`.

```
                     ┌──── main() ────┐
                     │ map, resolve,   │
                     │ setup TEB/PEB   │
                     └────────┬────────┘
                              │
                          fork()
                         ┌────┴────┐
                         │         │
                     parent    child
                         │         │
                waitpid() │         │ install signals
                         │         │ generate thunks
                         │         │ setup sigsys
                         │         │ set GS base (child)
                         │         │ patch __acrt_iob
                         │         │ run_guest() → entry
                         │         │   │
                         │         │   ▼
                         │     guest code runs
                         │     (syscalls intercepted)
                         │         │
                         │    _exit(code)
                         │         │
                     child         │
                     exits         │
                         │         │
                 WEXITSTATUS() ────┘
                         │
                     cleanup_guest()
                     (munmap TEB, PEB, stack, thunks)
```

**Why fork?** The parent needs a clean Linux environment to manage
the guest. The child runs the guest PE with intercepted syscalls.
If the child crashes, the parent survives and returns the exit code.
The `MAP_FIXED` image mapping is inherited by the child via
`fork()` (the child gets a private copy via copy-on-write).

---

## 3. Syscall Interception Chain

### 3.1 The 0xF000 Offset Scheme

Wine uses syscall numbers in the range `0xF000+` for NT syscalls.
Linux syscall numbers are all `< 0x400`. A seccomp-BPF filter
distinguishes them:

```
BPF: LOAD syscall_number
BPF: JGE 0xF000 → TRAP (send SIGSYS)
BPF: ALLOW (native Linux syscall, pass through)
```

This means:
- **syscall < 0xF000** → executed directly by the Linux kernel
- **syscall >= 0xF000** → trapped by seccomp, delivers `SIGSYS` to
  our handler

### 3.2 Thunk Generation

Each NT syscall has a dynamically generated thunk (11 bytes):

```asm
; generated at runtime by thunk_gen.c
mov  r10, rcx          ; 41 89 CF  (Windows: arg1 in RCX; Linux: in R10)
mov  eax, NR + 0xF000  ; B8 XX XX XX XX
syscall                ; 0F 05
ret                    ; C3
```

Each thunk occupies one page (4096 bytes, `PROT_READ|PROT_WRITE|PROT_EXEC`).
The IAT entries in the PE point to these thunks so that when guest code
calls `NtWriteFile`, it jumps to the thunk, which executes `syscall`
with a `0xF000+` number, triggering `SIGSYS`.

### 3.3 SIGSYS Handler → Dispatcher → Handler

```
  Guest code: call NtWriteFile
        │
        ▼
  Thunk: syscall 0xF03D
        │
        ▼
  seccomp filter: syscall >= 0xF000 → TRAP
        │
        ▼
  SIGSYS delivered
        │
        ▼
  sigsys_handler() (src/syscall/signal_handler.c)
    │
    ├── validate: call_addr is within ±4096 of a registered thunk
    │     │
    │     └── if not: raise(SIGSEGV) — fatal
    │
    └── g_dispatcher(syscall_num, ucontext)
            │
            ▼
    handle_syscall() (src/syscall/dispatcher.c)
      │
      ├── nt_nr = syscall_num - 0xF000
      ├── read args from RCX/RDX/R8/R9 (Windows x64 ABI)
      ├── switch(nt_nr):
      │     case 0x3D → handler_NtWriteFile(...)
      │     case 0x2A → handler_NtTerminateProcess(...)
      │     case 0x18 → handler_NtAllocateVirtualMemory(...)
      │     ...
      └── write result to RAX
            │
            ▼
  Advance RIP past `syscall` (2 bytes: 0x0F 0x05)
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

## 4. force_align_arg_pointer and WINE_STUB

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
