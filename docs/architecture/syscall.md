# NT Syscall Dispatcher

The syscall subsystem (`src/syscall/`) provides the low-level NT dispatch machinery and routes calls to C handler implementations in `src/msvcrt/ntdll_*.c`. In the current codebase, many guest-facing `ntdll.dll` imports resolve directly to `handler_Nt*` functions through `src/loader/import_table.c`, while the generated thunk and dispatcher path remains available as shared infrastructure.

---

## Architecture

There are two relevant call paths today:

1. Imported `ntdll.dll` functions such as `NtAllocateVirtualMemory` or `NtWriteFile` resolve directly to `handler_Nt*` functions via `src/loader/import_table.c`.
2. The thunk and dispatcher path generated in `src/syscall/` is prepared during guest setup and remains the shared NT dispatch mechanism for code that uses it.

The dispatcher pipeline itself has four stages:

```
guest call site             my_wine runtime
───────────────             ─────────────────
 thunk / stub path       →  thunk (generated)
                        →  __wine_dispatcher (asm)
                        →  c_dispatch_syscall (C)
                        →  handler_NtXXX (C, src/msvcrt/)
```

### Syscall Number Assignment

The NT syscall numbers defined in `include/nt_syscalls.def` currently fall in the range **0x03–0x5E**. They are Windows NT numbers used as dispatcher keys inside `dispatcher_generated.c`.

In the current implementation, these numbers matter to generated thunks and the named constants in `include/nt_constants.h`. The codebase does not currently rewrite every guest `syscall` instruction during PE loading.

---

## Syscall Dispatcher

The dispatcher maps Windows NT syscall numbers to C handler functions. It is generated from a single source of truth: `include/nt_syscalls.def`.

### Source File: `include/nt_syscalls.def`

`nt_syscalls.def` is a human-editable definition file. Each syscall is a block separated by blank lines:

```
0x18  NtAllocateVirtualMemory
handler: handler_NtAllocateVirtualMemory
args: ptr(wb) arg2 as h_baseAddr | ptr(wb) arg4 as h_regionSz
call: handler_NtAllocateVirtualMemory(arg1, &h_baseAddr, arg3, &h_regionSz, STACK(1), STACK(2))
```

**Format:**
- **Header line:** `0xNN  NtFunctionName` — syscall number and Windows API name
- **handler:** `handler_NtFunctionName` — the C function in `src/msvcrt/ntdll_*.c`
- **args:** (optional) argument descriptors, pipe-separated:
  - `ptr(wb) argN as h_name` — pointer argument, 64-bit write-back (e.g. `PVOID*`)
  - `ptr(wb32) argN as h_name` — pointer argument, 32-bit write-back (e.g. `PBOOLEAN`, `PULONG`)
  - `ptr(ro) argN name "label"` — read-only pointer, validated before the call (register source)
  - `ptr(ro) h_name name "label"` — read-only pointer validated from a stack-local variable
  - `stack N as h_name` — extra argument read from the guest stack at index N
- **call:** the expanded handler invocation referencing `argN`, `&h_name`, `STACK(N)`, or `raw`

The `nt_syscalls.def` file currently defines **26 NT syscalls** for the generated dispatcher switch body.

### Generator: `scripts/gen_dispatcher.py`

The Python script reads `include/nt_syscalls.def` and produces `src/syscall/dispatcher_generated.c`:

```bash
python3 scripts/gen_dispatcher.py --generate   # write the file
python3 scripts/gen_dispatcher.py --check      # verify freshness
python3 scripts/gen_dispatcher.py              # verify mode (parse + validate)
```

For each syscall block, the generator produces:
1. **Local variable declarations** — `uint64_t h_xxx` and `void *p_xxx` for write-back pointers
2. **Stack reads** — `read_guest_stack(N)` for `stack N` arguments
3. **Validation** — `dispatch_ptr_inout()` for `ptr(wb)`/`ptr(wb32)`, `read_guest_ptr()` for `ptr(ro)`
4. **Handler call** — the `call:` template expanded with `STACK(N)` → `read_guest_stack(N)`, `raw` → first register-sourced `ptr(ro)`
5. **Write-back** — stores modified locals back to guest pointers

### Generated File: `src/syscall/dispatcher_generated.c`

The output is a `switch(nr)` body wrapped in `#ifndef DISPATCHER_GENERATED_C`. It is `#include`d directly inside `dispatcher_core()` in `dispatcher.c`:

```c
static uint64_t dispatcher_core(uint64_t nr, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4)
{
    uint64_t result = 0;
    #define arg1 a1
    #define arg2 a2
    #define arg3 a3
    #define arg4 a4
    #include "dispatcher_generated.c"
    #undef arg1; #undef arg2; #undef arg3; #undef arg4
    return result;
}
```

The `arg1`-`arg4` macros are defined *before* the include so the generated code references them by name. On x86_64 they are function parameters; on x86 they expand to `read_guest_stack` calls (see below).

### Dispatcher: `src/syscall/dispatcher.c`

The C dispatcher has three responsibilities:

1. **Guest stack reading** — `read_guest_stack(index)` reads from the guest's stack via `__wine_guest_regs.esp`/`.rsp` with bounds checking
2. **Guest pointer validation** — `read_guest_ptr()` and `dispatch_ptr_inout()` validate that guest-space pointers are within the guest address region before dereferencing
3. **Dispatch core** — the `dispatcher_core()` function with the `#include`d switch body

**ABI portability:** The dispatcher uses a macro-based abstraction so the *same* generated switch body works on both architectures:

| Macro | x86_64 (Windows x64 ABI) | x86 (cdecl) |
|---|---|---|
| `arg1` | `a1` (RCX parameter) | `read_guest_stack(4)` (stack[ESP+16]) |
| `arg2` | `a2` (RDX parameter) | `read_guest_stack(5)` (stack[ESP+20]) |
| `arg3` | `a3` (R8 parameter) | `read_guest_stack(6)` (stack[ESP+24]) |
| `arg4` | `a4` (R9 parameter) | `read_guest_stack(7)` (stack[ESP+28]) |
| `STACK(n)` | `read_guest_stack(n)` (arg 5+) | `read_guest_stack(n+7)` (arg 5+, offset-adjusted) |
| `writeback_ptr(p, v)` | `*(uint64_t*)p = v` | `*(uint32_t*)p = (uint32_t)v` |

**32-bit stack layout** (cdecl with thunk frame):
```
[ESP+0]   = EFLAGS (pushf)
[ESP+4]   = thunk return address
[ESP+8]   = thunk's saved EBP
[ESP+12]  = guest caller's return address
[ESP+16]  = arg1 (index 4 = 1+3)
[ESP+20]  = arg2 (index 5 = 2+3)
[ESP+24]  = arg3 (index 6 = 3+3)
[ESP+28]  = arg4 (index 7 = 4+3)
[ESP+32]  = arg5+ (index 8 = 1+7 via STACK(1))
```

**64-bit stack layout** (register args, args 5+ on stack):
```
[RSP+0]   = thunk return address
[RSP+8]   = original RDI (pushed by thunk)
[RSP+16]  = guest caller's return address
[RSP+24]  = arg5 (index 1 via STACK(1))
```

The `c_dispatch_syscall()` entry point is the single function that the assembly dispatcher calls. On x86_64 it reads RCX/RDX/R8/R9 from `__wine_guest_regs`; on x86 it reads EDX (syscall number) and uses stack macros.

### Handler Functions

The `handler_NtXXX()` functions live in `src/msvcrt/ntdll_*.c`:

| File | Handlers |
|---|---|
| `ntdll_memory.c` | NtAllocateVirtualMemory, NtFreeVirtualMemory, NtCreateSection, NtMapViewOfSection, NtUnmapViewOfSection |
| `ntdll_io.c` | NtReadFile, NtWriteFile, NtOpenFile |
| `ntdll_objects.c` | NtCreateEvent, NtCreateThreadEx, NtGetContextThread, NtSetContextThread |
| `ntdll_process.c` | NtTerminateProcess, NtCallbackReturn, NtQueryInformationProcess |
| `ntdll_synchronization.c` | NtCreateMutex, NtCreateSemaphore, NtSetEvent, NtResetEvent, NtWaitForSingleObject, NtReleaseMutex |
| `ntdll_time.c` | NtQuerySystemTime, NtDelayExecution, NtQueryPerformanceCounter, NtQueryPerformanceFrequency |
| `ntdll_handle.c` | NtClose |

`thunk_gen.c` currently emits thunks for 25 syscall numbers from `nt_constants.h`. `NtCreateSemaphore` is present in `nt_syscalls.def` and the generated dispatcher, but it is not currently included in the thunk generator's static list.


---

## Thunk Generation

Thunk generation allocates a reusable block of small machine-code entry points. Each thunk sets up the syscall number and calls `__wine_dispatcher`.

### Source File: `src/syscall/thunk_gen.c`

`thunk_gen.c` allocates a single `mmap`d executable blob and writes one thunk per NT syscall into it. The `generate_all_thunks()` function returns a pointer array indexed by syscall number.

### x86_64 Thunk (23 bytes)

```
Offset  Bytes  Instruction
──────  ─────  ───────────
0       2      push rdi          # 41 57  — save original RDI
2       7      mov rdi, imm32    # 48 C7 C7 XX XX XX XX  — load syscall number
9       10     mov rax, imm64    # 48 B8 XX ... XX XX XX  — load dispatcher address (absolute)
19      2      call rax          # FF D0  — enter dispatcher
21      1      pop rdi           # 5F  — restore RDI (after dispatcher returns)
22      1      ret               # C3  — return to guest caller
```

Key design decisions:
- **Absolute indirect call** (`mov rax, imm64; call rax`) instead of relative `call rel32`: avoids ASLR displacement overflow when the thunk blob and dispatcher are >2 GB apart
- **RDI save/restore** in the thunk (not the dispatcher): the dispatcher's `ret` lands directly on `pop rdi`, keeping the assembly path minimal
- **No relocations:** the dispatcher address is baked in at runtime, so no `.plt` or dynamic relocation is needed

### x86 Thunk (15 bytes)

```
Offset  Bytes  Instruction
──────  ─────  ───────────
0       1      push ebp          # 55  — save guest EBP (callee-saved)
1       5      mov eax, imm32    # B8 XX XX XX XX  — load dispatcher address
6       5      mov edx, imm32    # BA XX XX XX XX  — load syscall number
11      2      call eax          # FF D0  — indirect call to dispatcher
13      1      pop ebp           # 5D  — restore EBP
14      1      ret               # C3  — return to guest caller
```

On x86, EAX carries the dispatcher address (32-bit fits), EDX carries the syscall number to `c_dispatch_syscall(edx)`, and EBP is the callee-saved register preserved across the dispatcher call. EAX is free to carry the syscall return value back to the guest.

### Lookup

`lookup_thunk(syscall_number)` returns the thunk function pointer from the `thunk_array` (size `0x60`) or NULL if not registered. In the current tree, kernel32 stubs mainly use this as an availability check before proceeding with direct handlers or inline syscalls.

---

## ABI Translation

The full path from guest code to handler involves careful ABI management across three domains: the guest PE's calling convention, the Linux kernel's syscall ABI, and the C handler's native ABI.

### Assembly Entry: `src/syscall/dispatcher_entry_asm.S`

`__wine_dispatcher` is the assembly trampoline that bridges guest state and the C dispatcher:

**x86_64:**
1. Save all guest registers to `__wine_guest_regs` (RCX, RDX, R8, R9, RSI, RBX, R12–R15, RSP, ret_addr) — RDI is NOT saved (thunk handles it via push/pop)
2. Switch RSP to `unix_stack_ptr_val` (pre-allocated UNIX stack, 16-byte aligned)
3. Call `c_dispatch_syscall` (RDI already has syscall number)
4. Save RAX result to `__wine_guest_regs`
5. Switch RSP back to original guest stack
6. Restore all guest registers
7. `ret` — returns to thunk's `pop rdi`

**x86:**
1. Save guest registers to `__wine_guest_regs` (EAX, EBX, ECX, EDX, ESI, EDI, ESP, EIP, EFLAGS) — EBP is NOT saved (thunk handles it via push/pop)
2. Switch ESP to `unix_stack_ptr_val - 8` (16-byte aligned)
3. Call `c_dispatch_syscall` (EDX has syscall number)
4. Save EAX result to `__wine_guest_regs`
5. Switch ESP back, popf to restore EFLAGS
6. Restore guest registers
7. `ret` — returns to thunk's `pop ebp`

**No red zone:** The assembly entry never uses the System V red zone because the guest stack is still active during the stack switch.

### C Glue: `src/syscall/dispatcher_entry.c`

Manages the UNIX stack used during dispatch:
- Allocates a 128 KB (x86) or 2 MB (x86_64) `mmap`d region
- On x86, uses `MAP_FIXED` at `0x00600000` (between guest stack at `0x00500000+512K` and signal stack at `0x00800000`)
- `setup_unix_stack()` sets `unix_stack_ptr_val` to the top of the region (stack grows down)
- `wine_dispatcher_addr()` returns the address of `__wine_dispatcher` for thunk generation

### Inline Syscall Helpers: `src/syscall/syscalls_inline.h`

Provides `INLINE_SYSCALL_*` macros for direct kernel syscalls — used by the loader infrastructure and dispatcher without glibc:

| Macro | x86_64 | x86 |
|---|---|---|
| `INLINE_SYSCALL_MMAP` | `syscall` instruction | `synct_mmap2()` (int $0x80, from `mmap2_asm.S`) |
| `INLINE_SYSCALL_WRITE_ERR` | `syscall` instruction | `int $0x80` |
| `INLINE_SYSCALL_CLONE` | `wine_clone()` (from `clone64.S`) | `wine_clone()` (from `clone.S`) |
| `INLINE_SYSCALL_EXIT` | `syscall` instruction | `int $0x80` |

The 32-bit implementation uses `int $0x80` because our custom `_start` never initializes `gs:[0x10]` (vDSO pointer), so musl's `syscall()` would crash. Assembly wrappers in `clone.S` and `mmap2_asm.S` handle the complex calling conventions directly.

### ABI Wrappers: `src/syscall/abi_wrappers.c`

After the GS register is switched to the TEB during PE initialization, glibc functions that access the vDSO via GS-relative offsets will crash. The `sysv_*` functions in `abi_wrappers.c` are marked `__attribute__((sysv_abi))` and use:
- `heap_backend_malloc/calloc/free` instead of `glibc malloc`
- `__builtin_memcpy/strlen/strncmp` instead of `glibc string` functions
- `INLINE_SYSCALL_MMAP/MPROTECT` instead of `glibc mmap/mprotect`

These are used by code that runs after TEB setup but before the C runtime is fully initialized.

---

## Data Flow: Complete Example

Tracing `NtAllocateVirtualMemory` from guest code through the dispatcher:

```
PE code: syscall 0x18  (RAX=0x18, RCX=hProcess, RDX=&baseAddr, ...)

→ Patched thunk at PE's original syscall location:
    push rdi                  ; save RDI
    mov rdi, 0x18             ; syscall number
    mov rax, <dispatcher>    ; absolute address
    call rax                  ; → __wine_dispatcher
    pop rdi                   ; restore RDI
    ret                       ; → guest caller

→ __wine_dispatcher (dispatcher_entry_asm.S):
    save RCX, RDX, R8, R9, RSI, RBX, R12-15, RSP, ret_addr to __wine_guest_regs
    switch to UNIX stack
    call c_dispatch_syscall   ; RDI already = 0x18

→ c_dispatch_syscall (dispatcher.c, x86_64):
    call dispatcher_core(0x18, RCX, RDX, R8, R9)
        arg1 = a1 (RCX = hProcess)
        arg2 = a2 (RDX = &baseAddr)
        arg3 = a3 (R8 = 0 for allocation)
        arg4 = a4 (R9 = regionSize)
        result = handler_NtAllocateVirtualMemory(
            arg1,           // hProcess
            &h_baseAddr,    // read from arg2, written back via p_baseAddr
            arg3,           // 0
            &h_regionSz,    // read from arg4, written back via p_regionSz
            read_guest_stack(1),  // allocation type
            read_guest_stack(2)   // page protection
        )
    __wine_guest_regs.rax = result
    return result

→ __wine_dispatcher (dispatcher_entry_asm.S):
    save result to __wine_guest_regs.rax
    switch back to guest stack
    restore all guest registers
    ret → thunk's pop rdi → ret → guest caller

Guest now has: EAX/RAX = STATUS_SUCCESS, [baseAddr] = allocated address
```

---

## File Index

| File | Role |
|---|---|
| `include/nt_syscalls.def` | Source of truth — syscall definitions |
| `scripts/gen_dispatcher.py` | Generates `dispatcher_generated.c` from `.def` |
| `src/syscall/dispatcher_generated.c` | Auto-generated switch body (26 cases) |
| `src/syscall/dispatcher.c` | `dispatcher_core()`, `c_dispatch_syscall()`, guest stack/ptr helpers |
| `src/syscall/dispatcher_entry_asm.S` | `__wine_dispatcher` — register save/restore + stack switch |
| `src/syscall/dispatcher_entry.c` | `setup_unix_stack()`, `wine_dispatcher_addr()` |
| `src/syscall/thunk_gen.c` | `generate_all_thunks()`, `lookup_thunk()`, machine code emission |
| `src/syscall/syscalls_inline.h` | `INLINE_SYSCALL_*` macros (syscall/int $0x80) |
| `src/syscall/abi_wrappers.c` | `sysv_malloc()`, `sysv_mmap()` — post-TEB-safe helpers |
| `src/syscall/clone.S` | 32-bit `wine_clone()` for thread creation |
| `src/syscall/mmap2_asm.S` | 32-bit `synct_mmap2()` — direct int $0x80 |
| `include/syscall/dispatcher_entry.h` | `struct guest_regs`, shared declarations |
| `include/syscall/dispatcher.h` | Dispatcher declarations |
| `include/syscall/thunk_gen.h` | `generate_all_thunks()`, `lookup_thunk()` declarations |
