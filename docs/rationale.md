# Rationale

This document explains why my_wine makes its architectural choices. Each
section covers a key decision and the reasoning behind it.

---

## 1. Why single-process

The loader and the guest run in the **same process**. There is no
`fork()`, no parent/child split — the UNIX side is always alive and in
control.

### One Process, Two Roles

The loader performs all heavy lifting first:

- **Load PE** — open file, parse headers, `mmap` image at preferred base
- **Resolve imports** — walk ILT/IAT, patch IAT with our thunk addresses
- **Patch .refptr** — rewrite CRT global pointers to our stubs (in `main()`)
- **Setup TEB/PEB** — allocate and initialize Windows-expected structures

Then, instead of forking, the loader **switches to the guest stack** and
jumps to the entry point. The UNIX side remains present throughout: it
handles all syscalls via our dispatcher.

### Guest Runs on Its Own Stack

After setup, the loader:

- Switches `RSP` to a dedicated guest stack (a `mmap`-allocated page)
- Sets up `GS` base for the TEB (via `arch_prctl(ARCH_SET_GS)` with FSGSBASE fallback)
- Jumps to the entry point

When the guest executes a syscall, it enters our dispatcher via a direct
call (not via seccomp). The dispatcher runs on the **UNIX stack**, reads
arguments from the global `__wine_guest_regs` struct, dispatches to the
appropriate handler, writes the result back to `__wine_guest_regs.rax`,
then switches back to the guest stack and returns.

### No Fork Needed

Without `fork()`, there is no copy-on-write to worry about and no
parent/child separation. The PE mappings exist once, at their preferred
addresses. The UNIX side manages everything from a single address space.

### Trade-off: Guest Crashes Kill the Loader

If the guest crashes (SIGSEGV, unhandled exception), the entire process
terminates. Unlike the fork model, there is no parent to collect
`waitpid()` status and report diagnostics. This is the accepted trade-off
for a simpler architecture that is closer to Wine's approach.

### Single Process Flow

```
UNIX loader                 Guest code
─────────────               ──────────
- Load PE file              (not yet running)
- Parse headers
- Resolve imports
- Patch .refptr
- Setup TEB/PEB
- Setup SEH + generate thunks
- Patch __acrt_iob_func
- Switch to guest stack
- Jump to entry point  →   Guest code runs
                           call NtWriteFile
                           │
                           ▼
                 Switch to UNIX stack
                 c_dispatch_syscall()
                 Switch to guest stack
                           Guest continues...
```

### Why Not Fork?

The fork model adds complexity for limited benefit:

- **fork + seccomp** — requires a child process, a seccomp filter, a
  SIGSYS handler, and a parent waiting via `waitpid()`. The fork
  boundary complicates address space reasoning. Wine does not fork;
  neither do we.
- **Single process** — simpler mental model. The UNIX side is always present.
  Stack switching is a well-known technique (used by Wine, v8, and
  others) for managing two execution contexts in one process.

---

## 2. Why direct dispatch + stack switching

my_wine intercepts NT syscalls using **direct dispatch** with **stack
switching**. When guest code calls an NT syscall, it jumps to our
dispatcher, which switches to the UNIX stack, handles the call, then
switches back.

### How Interception Works

Each NT syscall has a **dynamically generated 23-byte thunk** (in
`thunk_gen.c`) that is wired into the PE's IAT during import resolution.
The thunks live in a single `mmap`'d executable blob.

The thunk loads the **raw NT syscall number** (e.g. `0x3D` for
`NtWriteFile`) into `RDI` and calls `__wine_dispatcher` via absolute
indirect call. The IAT is patched to point to these thunks — so when
guest code calls `NtWriteFile`, it jumps to the thunk, which calls our
dispatcher.

Linux syscalls (numbers `< 0x400`) pass through directly to the kernel
— they are never intercepted because the guest never calls them through
the dispatcher. Our stubs call Linux syscalls directly (via inline
syscall instructions) without going through `__wine_dispatcher`.

The dispatcher is reached **only** via IAT-patched thunk calls, not via
any syscall number detection or kernel trap.

### Thunk Layout

Each thunk is 23 bytes, using an **absolute indirect call** (not a
relative call) to avoid displacement overflow when the thunk blob and
dispatcher are more than 2 GB apart due to ASLR:

```asm
  push rdi                     ; 41 57   (save original RDI, 2 bytes)
  mov  rdi, imm32(NT_NR)      ; 48 C7 C7 XX XX XX XX  (set syscall nr, 7 bytes)
  mov  rax, imm64(dispatcher) ; 48 B8 XX XX XX XX XX XX XX XX  (load dispatcher addr, 10 bytes)
  call rax                     ; FF D0  (indirect call, 2 bytes)
  pop  rdi                     ; 5F     (restore original RDI, 1 byte)
  ret                          ; C3     (return to guest caller, 1 byte)
```

### Stack Switching

The key mechanism is **stack switching** via `dispatcher_entry_asm.S`:

```asm
__wine_dispatcher:
    /* Save guest state to __wine_guest_regs (global struct, RIP-relative) */
    mov (%rsp), %rax
    mov %rax, __wine_guest_regs+RET_ADDR_OFF(%rip)
    mov %rsp, __wine_guest_regs+RSP_OFF(%rip)
    mov %rcx, __wine_guest_regs+RCX_OFF(%rip)
    mov %rdx, __wine_guest_regs+RDX_OFF(%rip)
    mov %r8,  __wine_guest_regs+R8_OFF(%rip)
    mov %r9,  __wine_guest_regs+R9_OFF(%rip)
    mov %rsi, __wine_guest_regs+RSI_OFF(%rip)
    ; RDI is NOT saved here — the thunk push/pop rdi wraps the call

    /* Switch to UNIX stack */
    mov unix_stack_ptr_val(%rip), %rsp
    sub $8, %rsp                    ; ABI stack alignment

    /* Call C dispatcher (RDI already = syscall_nr) */
    call c_dispatch_syscall

    /* Save result to __wine_guest_regs.rax */
    mov %rax, __wine_guest_regs+RAX_OFF(%rip)

    /* Switch back to guest stack, restore guest registers */
    mov __wine_guest_regs+RSP_OFF(%rip), %rsp
    mov __wine_guest_regs+RCX_OFF(%rip), %rcx
    mov __wine_guest_regs+RDX_OFF(%rip), %rdx
    mov __wine_guest_regs+R8_OFF(%rip), %r8
    mov __wine_guest_regs+R9_OFF(%rip), %r9
    mov __wine_guest_regs+RSI_OFF(%rip), %rsi
    mov __wine_guest_regs+RAX_OFF(%rip), %rax

    ret     ; returns to thunk's `pop rdi`, then thunk's `ret` to guest
```

The sequence is:

1. **Save guest registers** — all guest registers (RCX, RDX, R8, R9, RSI,
   RSP, return address) are saved to the global `__wine_guest_regs` struct
   using RIP-relative addressing. RDI is handled by the thunk's push/pop.
2. **Switch to UNIX stack** — set `RSP` to `unix_stack_ptr_val - 8` (aligned
   for System V ABI `call`)
3. **Call handler** — `c_dispatch_syscall(nr)` runs with full C ABI on the
   UNIX stack; it reads arguments from `__wine_guest_regs.rcx/rdx/r8/r9`
   plus the guest stack (for args 5+) via `__wine_guest_regs.rsp`,
   dispatches to the appropriate handler, and writes the result to
   `__wine_guest_regs.rax`
4. **Restore guest registers** — all saved registers are restored from
   `__wine_guest_regs`
5. **Switch back** — set `RSP` to the saved guest stack pointer
6. **Return** — `ret` goes to the thunk's `pop rdi`, then the thunk's `ret`
   returns to the guest caller; the result is in `RAX`

Because the UNIX side runs on its own stack, it can use the full C ABI
safely — no risk of clobbering guest stack data.

### Why Not seccomp + SIGSYS?

The seccomp + SIGSYS approach requires:

- **libseccomp** — extra dependency for constructing the BPF filter
- **A signal handler** — which has severe restrictions (only async-
  signal-safe functions, no heap allocation, etc.)
- **Per-thunk pages** — each thunk needs its own 4096-byte `PROT_EXEC`
  page for the signal handler to validate the call site
- **RIP advancement** — manually advancing the instruction pointer past
  the `syscall` instruction before returning from the signal handler

Direct dispatch with stack switching avoids all of this. It is simpler,
faster, and closer to what Wine actually does.

### Why Not ptrace?

`ptrace` can intercept syscalls, but it requires a context switch per
**instruction** (not per syscall). The overhead is prohibitive: every
instruction in the guest triggers a trap to the tracer. For a program
that makes many syscalls (like CRT initialization), ptrace is orders of
magnitude too slow.

### Why Not LD_PRELOAD?

`LD_PRELOAD` intercepts **library functions** (`libc.so`), not raw
syscalls. The Windows x64 ABI (`ms_abi`) passes arguments in
`rcx`/`rdx`/`r8`/`r9`. `LD_PRELOAD` can only intercept functions
called with the System V ABI (`rdi`/`rsi`/`rdx`/`rcx`/`r8`/`r9`).
Guest code calls our dispatcher directly — there is no `libc` boundary
to hook.

See [architecture.md §§2,3](architecture.md) for the full syscall
dispatch flow.

---

## 3. Why .refptr patching

GCC/MinGW generates `.refptr` entries in the PE's data sections
containing pointers to CRT globals (`__CTOR_LIST__`, `__DTOR_LIST__`,
`__imp___initenv`, etc.). In our Linux environment these pointers
resolve to garbage — the PE's own `.data` at its preferred base has no
real CRT globals. We must redirect them to our Linux-side stubs.

### Three-Layer Patching

Patching is done in `crt_refptrs.c` during `main()` (the loader phase),
before the guest ever runs. Three layers are used, each falling through
to the next for symbols the previous layer did not find:

#### Layer 1: COFF Symbol Table Lookup

When the PE retains its COFF symbol table, we discover the exact RVAs of
`.refptr` entries by name. This is the preferred approach — it works
regardless of linker ordering.

```c
// src/stubs/crt_offset_discovery.c — find_symbol_rva_from_file()
uint64_t find_symbol_rva_from_file(const char *file_path,
                                   IMAGE_NT_HEADERS64 *nt,
                                   IMAGE_SECTION_HEADER *sections,
                                   const char *name)
{
    // Open PE file, mmap it, walk COFF symbol table
    // Prefer .rdata$.refptr.NAME and .refptr.NAME prefixes
    // Prefer section-bound symbols over absolute
}
```

The `patch_crt_refptrs()` function in `crt_refptrs.c` iterates over a
`refptr_mappings[]` table (e.g. `__CTOR_LIST__` → `&ctor_list_stub`) and
looks up each symbol's RVA from the COFF table, then patches the pointer
at that location.

#### Layer 2: Data Section Scan for .bss Pointers

Some mingw-w64 builds don't include certain CRT symbols in the COFF
symbol table (only in DWARF debug info). For symbols that Layer 1 missed,
we scan `.rdata` and `.data` sections for 8-byte values that point into
`.bss`, and patch them to our stubs.

```c
// src/stubs/crt_refptrs.c — fallback data scan
for each entry in .rdata/.data at 8-byte boundaries:
    if entry value points into .bss:
        match to next unpatched .bss mapping
        patch to stub
```

#### Layer 3: .text Pattern Scan for __imp___initenv

For `__imp___initenv` specifically, if Layers 1 and 2 did not find it,
we scan `.text` for the two-level indirection pattern
(`mov rax, [rip+disp32]` followed by `mov rax, [rax]` + write to `[rax]`)
and patch the discovered target. This handles builds where `__imp___initenv`
is entirely absent from the COFF symbol table.

```c
// src/stubs/crt_offset_discovery.c — scan_text_for_refptrs()
for each instruction in .text:
    if pattern matches "mov reg, [rip+disp]" + deref + store:
        patch target to __imp___initenv_stub
```

### `__acrt_iob_func` Patch

The mingw-w64 CRT function `__acrt_iob_func` has a bug in its
wrapper: it clobbers the upper 32 bits of `RCX`. This causes
undefined behavior when `printf`-family functions use it to
access the `iob` array.

We fix this with a 15-byte overwrite at the function's entry point
in `guest_setup.c` during guest setup (not in the loader phase):

```asm
; Replace the wrapper with a direct return of our iob array base
movabs rax, <__wine_iob_data()>   ; 48 B8 xx xx xx xx xx xx xx xx  (10 bytes)
ret                               ; C3                               (1 byte)
NOP NOP NOP NOP                   ; 90 90 90 90                      (4 bytes)
```

The implementation lives in `src/loader/guest_setup.c` in
`patch_acrt_iob()`, which is called from `apply_final_patches()`
inside `setup_guest_and_run()`. It finds the `.text` jmp-thunk
whose IAT target resolves to `__iob_func` and overwrites it.

### Summary

| What | Where | When |
|------|-------|------|
| `.refptr` redirection | `src/stubs/crt_refptrs.c` | `main()` (loader phase) |
| `__acrt_iob_func` patch | `src/loader/guest_setup.c` | Guest setup (before entry) |

See [CRT refptr Patching](refptr.md) for full implementation details.

---

## 4. Requirements

| Requirement | Why |
|---|---|
| Linux x86_64 | We use the GS segment for TEB access and the x86_64 syscall ABI. No other architecture is supported. |
| GCC | We need GCC-specific attributes: `__attribute__((ms_abi))` for Windows x64 calling convention, `__attribute__((force_align_arg_pointer))` for stack alignment. |
| Docker + mingw-w64 | Cross-compilation to PE format via `x86_64-w64-mingw32-gcc`. Used for building sample Windows binaries, not for the loader itself. |
| `-mno-red-zone` | The Windows x64 ABI has no red zone. Without this flag, GCC assumes a 128-byte red zone below RSP, which conflicts with stack switching and guest stack operations. |
| `-fno-stack-protector` | Stack canaries require `__stack_chk_fail` from glibc, which the guest process can't call. Disabling them prevents crashes from missing glibc symbols. |
| `-fno-exceptions` | No C++ exception handling is needed. Disabling avoids generating unwind tables and reducing code size. |
| `-lrt -lpthread -ldl` | Link-time dependencies: `librt` for timer/realtime, `libpthread` for threading (NtCreateThreadEx), `libdl` for `dlsym` in test builds. |
| `arch_prctl(ARCH_SET_GS)` | We set the GS base to point to the TEB. Implemented in `gs_base.c` with `arch_prctl` first, falling back to `wrgsbase`/`rdgsbase` FSGSBASE instructions. |

The compiler flags (`-mno-red-zone`, `-fno-stack-protector`, `-fno-exceptions`)
are applied via `SPECIAL_CFLAGS` in the Makefile to `main.c`, `common.c`,
`entry.c`, `teb_peb.c`, `guest_setup.c`, `crash_handlers.c`, `gs_base.c`,
`thunk_gen.c`, `dispatcher.c`, `dispatcher_entry_asm.S`, and all stub files.

The `WINE_STUB` macro (`__attribute__((ms_abi, force_align_arg_pointer))`)
is used for all functions called from guest PE code.

---

## 5. Limitations

### What my_wine supports

| Feature | Status | Files |
|---|---|---|
| PE loading (preferred base + MAP_STACK fallback) | ✅ | `image_mapper.c` |
| Base relocations (DIR64) | ✅ | `relocations.c` |
| Import resolution (Pass 1 IAT + Pass 2 thunk scanning) | ✅ | `import_resolve.c` |
| Dynamic loading (`LoadLibraryA`/`FreeLibraryA`) | ✅ | `kernel32_module.c`, `import_resolve.c` |
| Export table parsing + lookup (name & ordinal) | ✅ | `export_table.c` |
| Module registry + PEB LDR (3 doubly-linked lists) | ✅ | `module_list.c`, `peb_ldr.c` |
| TEB / PEB + GS base setup | ✅ | `teb_peb.c`, `gs_base.c` |
| Heap management (`HeapCreate/Alloc/Free/ReAlloc/Destroy/Size/GetProcessHeap`) | ✅ | `wine_heap.c` (musl malloc backend) |
| Synchronization (CRITICAL_SECTION, Events, Mutexes) | ✅ | `kernel32_sync.c`, `ntdll_synchronization.c` |
| Thread creation (`NtCreateThreadEx` via `clone()`) | ✅ | `ntdll_objects.c` |
| 25 NT syscall handlers | ✅ | `nt_syscalls.def` → `dispatcher_generated.c` |
| File I/O (`NtOpenFile`, `NtReadFile`, `NtWriteFile`) | ✅ | `ntdll_io.c` |
| Virtual memory (`NtAllocate/FreeVirtualMemory`, `NtCreateSection`, `NtMapViewOfSection`) | ✅ | `ntdll_memory.c` |
| Time (`NtQuerySystemTime`, `NtQueryPerformanceCounter/Frequency`, `NtDelayExecution`) | ✅ | `ntdll_time.c` |
| CRT startup (`__getmainargs`, `_initterm`, `__iob_func`, `__acrt_iob_func`) | ✅ | `crt_startup.c`, `crt_stdio.c` |
| Ordinal imports (ntdll/kernel32/msvcrt) | ✅ | `ordinal_table.c` |
| SEH + POSIX signal crash handlers | ✅ | `crash_handlers.c` |
| Dispatcher auto-generation (`.def` + Python generator) | ✅ | `nt_syscalls.def`, `gen_dispatcher.py` |

### What is not supported

| Limitation | Impact |
|---|---|
| **Guest crashes kill the loader** | Single-process model: SIGSEGV terminates everything. No parent to collect diagnostics. |
| **No nested syscall dispatch** | Stubs call Linux syscalls directly. Cannot dispatch NT syscall from within a stub. |
| **No TLS** | `TlsGetValue` returns `NULL`; `__declspec(thread)` is not supported. |
| **No `NtCreateFile`** | Only `NtOpenFile` is implemented. `CreateFileA`/`CreateFileW` are not in the import table. |
| **No `NtProtectVirtualMemory`** | `VirtualProtect` is a kernel32 stub using `mprotect`, but the NT syscall is unregistered. |
| **No `NtTerminateThread`** | Threads exit via `INLINE_SYSCALL_EXIT(0)` which kills the entire process. |
| **No `NtWaitForMultipleObjects`** | Only `NtWaitForSingleObject` is implemented. |
| **No `NtQueryAttributesFile`** | No `stat()`-equivalent for file attribute queries. |
| **Shared-TEB threading model** | All threads share the same TEB and GS base. No per-thread SEH, no per-thread TLS. `NtGetContextThread`/`NtSetContextThread` are stubs. |
| **No Unicode conversion** | `MultiByteToWideChar`/`WideCharToMultiByte` return `0`. `NtOpenFile` only handles ASCII. |
| **Missing kernel32 stubs** | `CreateFileA/W`, `CloseHandle`, `GetTickCount`, `GetModuleFileNameA/W`, `GetFileAttributesA/W`, `GetStartupInfoW`, `GetModuleHandleW`, `SetUnhandledExceptionFilter` are not registered. |
| **Only mingw-w64 executables** | CRT layout and import patterns are specific to mingw-w64 + GCC. MSVC binaries are not supported. |

---

## Related Documents

- [Onboarding](onboarding.md) — Getting started guide and reading order
- [PE Format Primer](pe_format.md) — PE structure basics
- [Architecture](architecture.md) — How it works: data flow, single-process model, syscall dispatch
- [CRT refptr Patching](refptr.md) — .refptr details
- [README](../README.md) — Build, run, quick start

This document covers the **WHY**. See [Architecture](architecture.md) for the **HOW** and [PE Format Primer](pe_format.md) for the **WHAT**.
