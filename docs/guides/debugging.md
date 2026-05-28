# Debugging

This guide covers the debugging facilities in my_wine: diagnostic output levels, crash handling, and running under a debugger.

## Debug Levels

my_wine uses a single environment variable to control all diagnostic output:

```bash
MY_WINE_DEBUG_LEVEL=2 ./my_wine path/to/binary.exe
```

The variable is parsed by `parse_debug_level()` (in `src/common.c`) and stored in the global `g_debug_level`. The wrapper passes the environment through `execvp` to `my_wine64` or `my_wine32`, so the debug level persists through the entire exec chain. For PE32 images, `pe32_bootstrap.c` reads the same variable independently.

| Level | Name | What You Get |
|-------|------|-------------|
| `0` (default) | Quiet | No debug output. Only errors and warnings (which never go through the debug system) are printed. |
| `1` | Milestones | Key lifecycle events: PE entry point resolved, TEB/PEB installed, imports resolved, CRT initialized, window creation. Warnings from subsystems (e.g. missing runtime slots, unresolved symbols). |
| `2` | Loader trace | Import resolution details (`resolve_import ordinal: user32#123 → ...`), syscall dispatcher addresses, PE header dump (sections, entry point, image base), .bss writes, thunk generation. Sufficient for tracing why a specific import or syscall is misbehaving. |
| `3` | Verbose | Per-symbol and per-IAT diagnostics. Section lookups report "not found" with search count. Useful when debugging symbol resolution or COFF table lookups. |

### Macros

All debug output goes to **stderr**. The macros in `include/debug.h` check the level at runtime via a function pointer (`debug_check_fn` / `debug_level_fn`) so they produce no output and no branching overhead at level 0:

```c
DEBUG("message");                       // level 1+
DEBUG_LEVEL(2, "trace: %p", ptr);       // level 2+
DEBUG_WRITE_ERR(buf, len);              // level 1+, signal-safe (uses write())
```

**Important:** `DEBUG` and `DEBUG_LEVEL` use `fprintf(stderr, ...)` — they are **not** async-signal-safe. Inside crash handlers, use `INLINE_SYSCALL_WRITE()` or `DEBUG_WRITE_ERR()` which use the `write()` syscall directly.

## Crash Handling

my_wine installs crash handlers before jumping into guest code. Two layers protect against faults:

### POSIX Signal Handlers

`src/loader/crash_handlers.c` installs handlers for six signals via `sigaction` with `SA_SIGINFO | SA_ONSTACK`:

| Signal | Cause |
|--------|-------|
| `SIGSEGV` | Invalid memory access (most common crash) |
| `SIGILL` | Illegal instruction |
| `SIGABRT` | `abort()` called |
| `SIGFPE` | Arithmetic exception (div-by-zero, overflow) |
| `SIGBUS` | Bus error (unaligned access, hardware) |
| `SIGTRAP` | Int3 breakpoint or hardware watchpoint |

An alternate signal stack is mapped via `mmap` + `sigaltstack` (64KB, `SIG_STACK_SIZE`) so the handler runs on a safe stack even if the guest stack is corrupted. On 32-bit, the stack is fixed at `0x00800000` (above the UNIX stack at `0x00620000`). If the `mmap` fails (OOM), the handler degrades gracefully: it still runs and prints diagnostics, but with a `WARNING` noting the dump may be unreliable.

### What the Crash Handler Prints

On a crash, the handler outputs to stderr using direct syscalls (async-signal-safe):

```
CRASH: SIGSEGV
CRASH: si_addr=0x00401abc, ucontext=0x00800abc
 RIP=0000000000401a50 RSP=0000000000620800 RAX=0000000000000000
```

- **Signal name**: identifies the fault type
- **si_addr**: the faulting address from `siginfo_t`
- **Register dump**: `RIP`/`RSP`/`RAX` on x86_64, or `EIP`/`ESP`/`EAX` on i386 (when the ucontext is valid)
- **Fallback dump**: if the ucontext pointer is invalid, reads `ESP`/`EBP` and signal-frame return address inline (x86 only)

The handler uses a recursion guard (`g_in_crash_handler`) — a second crash inside the handler triggers an immediate `INLINE_SYSCALL_EXIT_GROUP(EXIT_SIGSEGV)` with no further output.

### SEH Handler

The Windows-style SEH handler (`seh_crash_handler`) is set up in the guest exception chain. When invoked, it writes `"SEV: SEH handler invoked (exception in guest code)"` and exits with `STATUS_ACCESS_VIOLATION` (0xC0000005 & 0xFF = 5).

### Int3 Breakpoints

On x86 32-bit, `int 3` breakpoints (`0xCC`) are handled specially. The SIGTRAP handler checks if the faulting `EIP - 1` contains `0xCC` and falls within a loaded module. If so, the signal is silently returned (not treated as a crash), allowing GDB-style breakpoints in guest code:

```nasm
; In guest assembly:
int 3           ; resumes to next instruction
```

### EIP Sampling

At debug level 2+, a `SIGALRM` timer fires every 100ms and prints guest EIP samples:

```
SAMPLE: EIP=0x00401abc RVA=0x00001abc mod=DOOM95.EXE count=1
```

This helps identify which code region the guest is stuck in during infinite loops. The sample output is throttled: first 8 samples printed, then powers of 2 (16, 32, 64, ...) to avoid flooding.

## Running Under GDB

### Quick Debug with `debug-tests`

The simplest way to see debug output during testing:

```bash
make debug-tests              # runs all tests with MY_WINE_DEBUG_LEVEL=1
make debug-tests TEST=test_name  # runs a specific test
```

This invokes `scripts/run_tests.sh` with `MY_WINE_DEBUG_LEVEL=1` set in the environment.

### Debugging a PE Binary Directly

To debug a guest binary under GDB, **bypass the `my_wine` wrapper** and run the backend directly. The wrapper is only a thin router that calls `execvp` — it adds nothing to debugging:

```bash
# For PE32+ binaries:
gdb --args ./my_wine64 samples/hello_world_64/hello_world_64.exe

# For PE32 binaries:
gdb --args ./my_wine32 samples/hello_world_32/hello_world_32.exe
```

Useful GDB commands:

```gdb
# Set a breakpoint on the crash handler
break crash_handler

# Set a breakpoint on the SEH handler
break seh_crash_handler

# Print the debug level
print g_debug_level

# Set debug level from GDB (before guest code runs)
set var g_debug_level = 2

# Run with debug level via environment
set env MY_WINE_DEBUG_LEVEL=2
run
```

When `my_wine64` or `my_wine32` crashes, GDB will stop at the `INLINE_SYSCALL_EXIT_GROUP` in `crash_handler`. Use `bt` to see the stack trace and determine if the crash originated in host code (loader) or guest code (the PE binary).

### Debugging with Symbols

Build with debug symbols (default for non-release builds):

```bash
make clean
make my_wine my_wine32 my_wine64
```

The object files are built with `-g`. If you need to ensure debug info is present:

```bash
make CFLAGS_EXTRA="-g" my_wine64
```

## Environment Variable Reference

| Variable | Parsed By | Purpose |
|----------|-----------|---------|
| `MY_WINE_DEBUG_LEVEL` | `main.c` (PE32+), `pe32_bootstrap.c` (PE32) | Set debug output level 0-3 |
| `WINE_DLL_PATH` | `main.c` | Override DLL search path for imports |
| `WINE32_PE_PATH` | `pe32_bootstrap.c` | PE path when no argv[1] provided |

The `my_wine` wrapper passes the full environment through `execvp`, so all variables are visible to the backend. Set them on the wrapper command line:

```bash
MY_WINE_DEBUG_LEVEL=2 ./my_wine ./samples/unpacked/doom95/DOOM95.EXE
```

## Common Debugging Patterns

### Trace import resolution

```bash
MY_WINE_DEBUG_LEVEL=2 ./my_wine binary.exe 2>&1 | grep resolve_import
```

### Trace window creation

```bash
MY_WINE_DEBUG_LEVEL=1 ./my_wine binary.exe 2>&1 | grep user32
```

### Debug audio initialization

```bash
MY_WINE_DEBUG_LEVEL=1 ./my_wine binary.exe 2>&1 | grep -E 'dsound|winmm'
```

### Catch a specific crash

```bash
gdb --args ./my_wine64 binary.exe
(gdb) break crash_handler
(gdb) run
```

## Notes

- Debug output goes to stderr only, never mixed with the guest program's stdout.
- The `DEBUG` macros use `fprintf(stderr, ...)` — never call them from signal handlers. Use `DEBUG_WRITE_ERR()` or raw `INLINE_SYSCALL_WRITE()` in that context.
- The crash handler uses `INLINE_SYSCALL_EXIT_GROUP()` (direct syscall) instead of `_exit()` because the TEB/GS base has been repointed and glibc TLS access would segfault.
- Debug level 3 can produce large output. Pipe through `grep` or `less` to find relevant lines.
