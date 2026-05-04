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
- **Resolve imports** — walk ILT/IAT, patch IAT with our stub addresses
- **Patch .refptr** — rewrite CRT global pointers to our stubs
- **Setup TEB/PEB** — allocate and initialize Windows-expected structures

Then, instead of forking, the loader **switches to the guest stack** and
jumps to the entry point. The UNIX side remains present throughout: it
handles all syscalls via our dispatcher.

### Guest Runs on Its Own Stack

After setup, the loader:

- Switches `RSP` to a dedicated guest stack (a `mmap`-allocated page)
- Sets up `GS` base for the TEB
- Jumps to the entry point

When the guest executes a syscall, it enters our dispatcher via a direct
call (not via seccomp). The dispatcher runs on the **UNIX stack**, reads
arguments from the Windows x64 ABI registers, dispatches to the
appropriate handler, writes the result back to `RAX`, then switches back
to the guest stack and returns.

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
UNIX loader                  Guest code
─────────────                ──────────
- Load PE file               (not yet running)
- Parse headers
- Resolve imports
- Setup TEB/PEB
- Switch to guest stack
- Jump to entry point  →     Guest code runs
                             call NtWriteFile
                             │
                             ▼
                   Switch to UNIX stack
                   handle_syscall()
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

### Wine-Style Offset Scheme

NT syscalls use `0xF000 + nr` as their syscall number. This follows
the Wine convention (`WINE_SYSCALL_OFFSET = 0xF000`). Linux syscall
numbers are all `< 0x400`. The gap between `0x400` and `0xF000` is
unused on x86_64.

```
  syscall number < 0xF000  →  Linux kernel executes directly
  syscall number >= 0xF000 →  intercepted by our dispatcher
```

### Direct Call, Not Seccomp

Each NT syscall has a **dispatcher entry** in our code. The IAT in the
PE is patched to point to these entries instead of the original import
targets.

Unlike the seccomp + SIGSYS approach, there is no kernel trap. The
guest directly enters our C code via a call instruction. The dispatcher
runs on the UNIX stack, not the guest stack.

### Stack Switching

The key mechanism is **stack switching**:

```asm
; c_dispatch_syscall() — src/syscall/dispatcher.c
switch_to_unix_stack:
    mov  rax, [gs:0x10]     ; save guest RSP from TEB
    mov  rsp, <unix_stack>  ; switch to UNIX stack
    call handle_syscall     ; run on UNIX stack, System V ABI
    mov  [gs:0x10], rsp     ; restore guest RSP into TEB
    mov  rsp, rax           ; switch back to guest stack
    ret                      ; return to guest code
```

The sequence is:

1. **Save guest RSP** — read from the TEB (at `GS:0x10`)
2. **Switch to UNIX stack** — set `RSP` to our pre-allocated UNIX stack
3. **Call handler** — `handle_syscall()` runs with full C ABI on the
   UNIX stack; it reads arguments from the Windows x64 ABI registers
   (`RCX`, `RDX`, `R8`, `R9`), dispatches to the appropriate handler,
   and writes the result back to `RAX`
4. **Restore guest RSP** — write current `RSP` back into the TEB
5. **Switch back** — set `RSP` to the saved guest stack pointer
6. **Return** — `ret` goes back to guest code; the result is in `RAX`

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

### Linux Syscalls Pass Through

Linux syscalls (`< 0x400`) are never intercepted by our dispatcher.
They execute directly by the kernel. This means the guest can still use
standard Linux facilities (reading `/dev/null`, basic memory operations)
without going through our handler. Our stubs call Linux syscalls
directly, not through the dispatcher.

See [architecture.md §§2,3](architecture.md) for the full syscall
dispatch flow.

---

## 3. Why .refptr patching

GCC/MinGW generates a `.refptr` section in the PE's `.data` segment
containing pointers to CRT globals (`__argc`, `__argv`, `__envp`, etc.).
In our Linux environment these pointers resolve to garbage — the PE's
own `.data` at its preferred base has no CRT globals. We must redirect
them to our Linux-side stubs (`ctor_list_stub`, `dtor_list_stub`, etc.).

### Two Strategies

We support two approaches for locating the `.refptr` entries:

#### COFF Symbol Table Lookup

When the PE retains its symbol table, we can dynamically discover
the exact offsets of each `.refptr` entry by name. This is the
preferred approach — it works regardless of linker ordering.

```c
// src/stubs/crt_refptrs.c — runs in parent before fork
struct external *ext = coff_get_external(pe);
for (uint16_t i = 0; i < ext->NumberOfSymbols; i++) {
    uint8_t *sym = sym_buf + i * SYMBOL_SIZE;
    const char *name = get_symbol_name(sym, sym_buf, ext);
    if (strcmp(name, "__imp___argc") == 0
        || strcmp(name, "__imp___argv") == 0
        || strcmp(name, "__imp___environ") == 0) {
        // patch the pointer at sym->Value in .data
    }
}
```

#### Hardcoded Offsets (Fallback)

When the symbol table is stripped, we fall back to known offsets.
These are determined empirically from the MinGW linker layout:

- `__argc` at offset `0x018`
- `__argv` at offset `0x020`
- `__environ` at offset `0x028`

```c
// Fallback when symbols are stripped
const struct { const char *name; uint32_t offset; void *stub; } fallbacks[] = {
    { "__argc",     0x018, &argc_stub },
    { "__argv",     0x020, &argv_stub },
    { "__environ",  0x028, &environ_stub },
    // ...
};
```

This approach is fragile (depends on linker version and CRT layout)
but covers the common case of stripped release builds.

### `__acrt_iob_func` Patch

The mingw-w64 CRT function `__acrt_iob_func` has a bug in its
wrapper: it clobbers the upper 32 bits of `RCX`. This causes
undefined behavior when `printf`-family functions use it to
access the `iob` array.

We fix this with a 15-byte overwrite at the function's entry point:

```asm
; Replace the buggy wrapper with a direct return of our iob array
movabs rax, <__wine_iob_data()>   ; 48 B8 xx xx xx xx xx xx xx xx  (10 bytes)
ret                               ; C3                              (1 byte)
; + 4 NOPs for alignment padding  ; 90 90 90 90                    (4 bytes)
```

This must be done in the **child** process because
`__wine_iob_data()` returns a child-specific address that does not
exist in the parent. The implementation lives in
`src/loader/entry.c`.

### Summary

| What | Where | When |
|------|-------|------|
| `.refptr` redirection | `src/stubs/crt_refptrs.c` | Parent, before `fork()` |
| `__acrt_iob_func` patch | `src/loader/entry.c` | Child, before entry point |

See [CRT refptr Patching](refptr.md) for full implementation details.

---

## 4. Requirements

| Requirement | Why |
|---|---|
| Linux x86_64 | We use the GS segment for TEB access and the x86_64 syscall ABI. No other architecture is supported. |
| GCC | We need GCC-specific attributes: `__attribute__((ms_abi))` for Windows x64 calling convention, `__attribute__((force_align_arg_pointer))` for stack alignment, `__attribute__((naked))` for trampoline assembly. |
| Docker + mingw-w64 | Cross-compilation to PE format via `x86_64-w64-mingw32-gcc`. Used for building sample Windows binaries, not for the loader itself. |
| `-mno-red-zone` | The Windows x64 ABI has no red zone. Without this flag, GCC assumes a 128-byte red zone below RSP, which conflicts with stack switching and guest stack operations. |
| `-fno-stack-protector` | Stack canaries require `__stack_chk_fail` from glibc, which the guest process can't call. Disabling them prevents crashes from missing glibc symbols. |
| `-fno-exceptions` | No C++ exception handling is needed. Disabling avoids generating unwind tables and reducing code size. |
| `-ldl` | Needed for `dlsym` in test builds. Not required for the loader itself. |
| `arch_prctl(ARCH_SET_GS)` | We set the GS base to point to the TEB. This requires `arch_prctl` syscall (not FSGSBASE instructions). |

The compiler flags (`-mno-red-zone`, `-fno-stack-protector`, `-fno-exceptions`) are applied via `SPECIAL_CFLAGS` in the Makefile to `loader/`, `stubs/`, and `syscall/` files.

---

## 5. Limitations

| Limitation | Rationale |
|---|---|
| **Guest crashes kill the loader** | The single-process model means a guest SIGSEGV terminates the entire process. There is no parent to collect diagnostics. This is the accepted trade-off for simplicity and a closer match to Wine's approach. |
| **No nested syscall dispatch** | When our stubs call Linux syscalls (e.g., `write(2)` from `handler_NtWriteFile`), they go directly to the kernel, not through the dispatcher. Nested NT syscall dispatch is not supported. |
| **No relocation support** | We don't implement relocation processing. A `MAP_STACK` fallback exists but relocations are never applied. |
| **No dynamic loading** | `LoadLibraryA` returns `NULL`. Runtime DLL loading would require a full PE loading path at runtime. |
| **No TLS support** | `TlsGetValue` returns `NULL`; `__dyn_tls_init_callback` is stubbed. Per-thread slot management and callback invocation add complexity for minimal gain in single-threaded targets. |
| **Stubbed synchronization** | CriticalSection ops are no-ops. Full sync support adds significant complexity for minimal gain in single-threaded targets. |
| **Only mingw-w64 executables** | We assume mingw-w64 CRT layout and import patterns. MSVC binaries have different CRT structures and import conventions. |
| **Limited syscall handlers** | Only NT syscalls we explicitly implement work. Unsupported syscalls cause `STATUS_NOT_IMPLEMENTED` in the dispatcher. |
| **No heap management** | No `HeapAlloc`/`HeapFree` — only limited virtual memory via `mmap`. A full Windows-compatible allocator is out of scope. |
| **No filesystem I/O** | Only console I/O via `NtWriteFile`/`NtReadFile`. File I/O requires Windows-to-Linux path mapping and Windows file semantics. |
| **Single-thread SEH** | The SEH chain is global; no per-thread cleanup. Per-thread SEH requires thread-aware exception chain management. |

---

## Related Documents

- [Onboarding](onboarding.md) — Getting started guide and reading order
- [PE Format Primer](pe_format.md) — PE structure basics
- [Architecture](architecture.md) — How it works: data flow, single-process model, syscall dispatch
- [CRT refptr Patching](refptr.md) — .refptr details
- [README](../README.md) — Build, run, quick start

This document covers the **WHY**. See [Architecture](architecture.md) for the **HOW** and [PE Format Primer](pe_format.md) for the **WHAT**.
