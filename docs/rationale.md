# Rationale

This document explains why my_wine makes its architectural choices. Each
section covers a key decision and the reasoning behind it.

---

## 1. Why fork()

The loader runs in the **parent** process. The guest runs in the
**child** process. `fork()` is the boundary between the two.

### Parent Manages the Guest

The parent performs all heavy lifting before forking:

- **Load PE** — open file, parse headers, `mmap` image at preferred base
- **Resolve imports** — walk ILT/IAT, patch IAT with our stub addresses
- **Patch .refptr** — rewrite CRT global pointers to our stubs
- **Setup TEB/PEB** — allocate and initialize Windows-expected structures

All of this requires a clean Linux environment with normal syscalls.
If any step fails, the parent can report the error and exit cleanly.

### Child Runs the PE

After `fork()`, the child:

- Inherits the mapped image via copy-on-write (same virtual addresses)
- Installs the seccomp filter (traps `syscall >= 0xF000`)
- Generates thunks in `PROT_EXEC` pages
- Sets up `GS` base for the TEB
- Jumps to the entry point

The child is a sealed environment: only intercepted syscalls can reach
the kernel, and the parent monitors it via `waitpid()`.

### MAP_FIXED Is Inherited

`mmap(..., MAP_FIXED, ...)` creates a mapping at a specific address.
`fork()` duplicates the parent's address space via copy-on-write. The
child sees the exact same mappings at the same addresses — no
remapping needed.

### Parent Survives Crashes

If the guest crashes in the child (SIGSEGV, unhandled exception), the
parent is unaffected. It collects the exit status via
`waitpid()` → `WEXITSTATUS()` and can report diagnostics.

### Clean Separation

```
Parent (PID 1)              Child (PID 2, forked)
──────────────────          ─────────────────────────
- Load PE file              - Inherits mapped image
- Parse headers             - Applies seccomp filter
- Resolve imports           - Runs guest code
- Setup TEB/PEB             - SIGSYS on NT syscalls
- fork()                    - Parent waits via waitpid()
- waitpid() → exit code     - Exit/crash in isolation
```

### Alternatives Considered

- **No fork** — running guest code in the same process would mean a
  guest crash kills the loader. The parent would have no way to report
  diagnostics.
- **execve** — `execve` replaces the process image. The mapped PE
  segments would be lost. We need the mappings to survive the
  transition.
- **threads** — threads share the same address space but share the
  same seccomp filter. You cannot install a seccomp filter on one
  thread without affecting all threads in the process.

---

## 2. Why seccomp + SIGSYS + thunks

my_wine intercepts NT syscalls using a three-part mechanism:
dynamically generated thunks, a seccomp-BPF filter, and a signal
handler.

### Wine-Style Offset Scheme

NT syscalls use `0xF000 + nr` as their syscall number. This follows
the Wine convention (`WINE_SYSCALL_OFFSET = 0xF000`). Linux syscall
numbers are all `< 0x400`. The gap between `0x400` and `0xF000` is
unused on x86_64. This allows a simple seccomp filter to distinguish
native Linux syscalls from intercepted NT syscalls.

```
  syscall number < 0xF000  →  Linux kernel executes directly
  syscall number >= 0xF000 →  seccomp traps, sends SIGSYS
```

### seccomp-BPF Filter

The filter is a Berkeley Packet Filter (BPF) program attached via
`prctl(PR_SET_SECCOMP_FILTER, ...)`. It inspects the syscall number:

```
BPF: LOAD syscall_number
BPF: JGE WINE_SYSCALL_OFFSET → TRAP (send SIGSYS)
BPF: ALLOW (native Linux syscall, pass through)
```

**TRAP** action sends `SIGSYS` to the process. Unlike **KILL** (which
terminates the process), TRAP allows user-space handling.

The filter is installed in the child after `fork()` so the parent
remains unaffected.

### Thunk Generation

Each NT syscall has a dynamically generated **thunk** (11 bytes)
placed in a `PROT_EXEC` memory page. The IAT in the PE is patched to
point to these thunks instead of the original import targets.

```asm
; generated at runtime by src/syscall/thunk_gen.c
mov  r10, rcx          ; 41 89 CF  (Windows: arg1 in RCX; Linux: in R10)
mov  eax, NR + 0xF000  ; B8 XX XX XX XX
syscall                ; 0F 05
ret                    ; C3
```

When guest code calls `NtWriteFile`, it jumps to the thunk. The thunk:

1. Copies `rcx` to `r10` (realigns first argument from Windows ABI
   to Linux ABI — Linux expects arg1 in `r10` for `syscall`, Windows
   places it in `rcx`)
2. Loads the syscall number into `eax`
3. Executes `syscall` — this triggers the seccomp filter
4. The filter sends `SIGSYS` to our handler

Each thunk occupies its own page (4096 bytes) to allow independent
`mprotect` and for the signal handler to validate the call site.

### SIGSYS Handler → Dispatcher

`SIGSYS` is delivered to `sigsys_handler()` in
`src/syscall/signal_handler.c`. The handler:

1. **Validates the call address** — ensures the return address (from
   the signal frame) is within ±4096 bytes of a registered thunk page.
   If not, it raises `SIGSEGV` (fatal — something unexpected happened).
2. **Dispatches to `g_dispatcher()`** — passes the syscall number and
   the signal's `ucontext_t` to `handle_syscall()` in
   `src/syscall/dispatcher.c`.
3. **The dispatcher** reads arguments from the Windows ABI registers
   (`rcx`, `rdx`, `r8`, `r9`), routes to the appropriate handler
   function, and writes the result back to `rax`.
4. **Advances RIP** past the `syscall` instruction (2 bytes:
   `0x0F 0x05`) so the thunk resumes at `ret`.

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
  ├── validate: call_addr within ±4096 of registered thunk
  │     │
  │     └── if not: raise(SIGSEGV) — fatal
  │
  └── g_dispatcher(syscall_num, ucontext)
          │
          ▼
  handle_syscall() (src/syscall/dispatcher.c)
    │
    ├── nt_nr = syscall_num - WINE_SYSCALL_OFFSET
    ├── read args from RCX/RDX/R8/R9 (Windows x64 ABI)
    ├── switch(nt_nr):
    │     case NT_SYSCALL_WRITE_FILE → handler_NtWriteFile(...)
    │     case NT_SYSCALL_TERMINATE_PROCESS → handler_NtTerminateProcess(...)
    │     ...
    └── write result to RAX
          │
          ▼
Advance RIP past `syscall` → guest code continues with result in RAX
```

### Why Not ptrace?

`ptrace` can intercept syscalls, but it requires a context switch per
**instruction** (not per syscall). The overhead is prohibitive: every
instruction in the guest triggers a trap to the tracer. For a program
that makes many syscalls (like CRT initialization), ptrace is orders
of magnitude too slow.

### Why Not LD_PRELOAD?

`LD_PRELOAD` intercepts **library functions** (`libc.so`), not raw
syscalls. The Windows x64 ABI (`ms_abi`) passes arguments in
`rcx`/`rdx`/`r8`/`r9`. `LD_PRELOAD` can only intercept functions
called with the System V ABI (`rdi`/`rsi`/`rdx`/`rcx`/`r8`/`r9`).
Guest code calls our thunks directly via `syscall` — there is no
`libc` boundary to hook. Additionally, some guest operations (like
`mmap` with specific flags) bypass `libc` entirely and go straight to
the kernel.

### Linux Syscalls Pass Through

Linux syscalls (`< 0x400`) are never trapped. They execute directly by
the kernel. This means the guest can still use standard Linux facilities
(reading `/dev/null`, basic memory operations) without going through
our handler. The seccomp filter only intercepts the `0xF000+` range.

See [architecture.md §§2,3](architecture.md) for the full syscall
interception flow.

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
| Linux x86_64 | We use the GS segment for TEB access, seccomp-BPF for syscall filtering, and the x86_64 syscall ABI. No other architecture is supported. |
| GCC | We need GCC-specific attributes: `__attribute__((ms_abi))` for Windows x64 calling convention, `__attribute__((force_align_arg_pointer))` for stack alignment, `__attribute__((naked))` for trampoline assembly. |
| libseccomp-dev | Provides `libseccomp` for constructing the seccomp-BPF filter that traps NT syscalls. Linked via `-lseccomp`. |
| Docker + mingw-w64 | Cross-compilation to PE format via `x86_64-w64-mingw32-gcc`. Used for building sample Windows binaries, not for the loader itself. |
| `-mno-red-zone` | The Windows x64 ABI has no red zone. Without this flag, GCC assumes a 128-byte red zone below RSP, which conflicts with signal handlers and guest stack operations. |
| `-fno-stack-protector` | Stack canaries require `__stack_chk_fail` from glibc, which the guest process can't call. Disabling them prevents crashes from missing glibc symbols. |
| `-fno-exceptions` | No C++ exception handling is needed. Disabling avoids generating unwind tables and reducing code size. |
| `arch_prctl(ARCH_SET_GS)` | We set the GS base to point to the TEB. This requires `arch_prctl` syscall (not FSGSBASE instructions). |

The compiler flags (`-mno-red-zone`, `-fno-stack-protector`, `-fno-exceptions`) are applied via `SPECIAL_CFLAGS` in the Makefile to `loader/`, `stubs/`, and `syscall/` files.

---

## 5. Limitations

| Limitation | Rationale |
|---|---|
| **No relocation support** | We don't implement relocation processing. A `MAP_STACK` fallback exists but relocations are never applied. |
| **No dynamic loading** | `LoadLibraryA` returns `NULL`. Runtime DLL loading would require a full PE loading path at runtime. |
| **No TLS support** | `TlsGetValue` returns `NULL`; `__dyn_tls_init_callback` is stubbed. Per-thread slot management and callback invocation add complexity for minimal gain in single-threaded targets. |
| **Stubbed synchronization** | CriticalSection ops are no-ops. Full sync support adds significant complexity for minimal gain in single-threaded targets. |
| **Only mingw-w64 executables** | We assume mingw-w64 CRT layout and import patterns. MSVC binaries have different CRT structures and import conventions. |
| **Limited syscall handlers** | Only NT syscalls we explicitly implement work. Unsupported syscalls cause `STATUS_NOT_IMPLEMENTED` in the dispatcher. |
| **No heap management** | No `HeapAlloc`/`HeapFree` — only limited virtual memory via `mmap`. A full Windows-compatible allocator is out of scope. |
| **No filesystem I/O** | Only console I/O via `NtWriteFile`/`NtReadFile`. File I/O requires Windows-to-Linux path mapping and Windows file semantics. |
| **Hardcoded CRT fallback offsets** | Offsets 0x018/0x020/0x028 when symbols are stripped. Different CRT versions may change these offsets. |
| **No ordinal imports** | Ordinal imports (high-bit set in thunks) are not resolved. Only name-based imports via `IMAGE_IMPORT_BY_NAME` are supported. Ordinal imports are skipped with a warning. |
| **60s watchdog** | Child process has a 60-second timeout. Prevents indefinite hangs from blocking the parent's `waitpid()`. |
| **Single-thread SEH** | The SEH chain is global; no per-thread cleanup. Per-thread SEH requires thread-aware exception chain management. |

---

## Related Documents

- [Onboarding](onboarding.md) — Getting started guide and reading order
- [PE Format Primer](pe_format.md) — PE structure basics
- [Architecture](architecture.md) — How it works: data flow, fork model, syscall interception
- [CRT refptr Patching](refptr.md) — .refptr details
- [README](../README.md) — Build, run, quick start

This document covers the **WHY**. See [Architecture](architecture.md) for the **HOW** and [PE Format Primer](pe_format.md) for the **WHAT**.
