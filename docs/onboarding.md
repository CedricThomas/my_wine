# Onboarding

Welcome to my_wine. This guide walks you through the documentation and helps you understand the project structure, so you can start contributing quickly.

---

## What Is my_wine?

my_wine is a minimal user-space PE (Portable Executable) loader for Linux x86_64. It runs mingw-w64-compiled Windows executables without Wine — no virtual machine, no translation layer, just a thin loader that maps the PE image into memory and intercepts the few syscalls the guest needs.

The loader maps the PE at its preferred base address, patches CRT globals (`.refptr` section), resolves imports by wiring them to stub functions, and sets up the TEB/PEB structures expected by Windows x64 code. It then generates syscall thunks that call `__wine_dispatcher` directly, switching from the guest stack to a pre-allocated UNIX stack for each intercepted syscall.

The result: Windows executables compiled with mingw-w64 can run natively on Linux with minimal overhead.

The project uses a **single-process model**: everything runs in one process. The loader performs all heavy lifting (mapping, import resolution, TEB/PEB setup) in the same process that subsequently runs the guest code. There is no `fork()` — the guest executes in-place after setup completes. This simplifies debugging, eliminates the parent/child lifecycle, and mirrors Wine's architecture more closely.

Syscall interception works through **direct dispatch**: each NT syscall has a dynamically generated thunk that `call`s `__wine_dispatcher`. The assembly dispatcher saves guest register state to a global struct, switches to a pre-allocated UNIX stack, calls the C dispatcher (`c_dispatch_syscall`), writes results back, restores the guest stack, and returns. No seccomp filters or signal handlers are involved.

## Quick Start

**Prerequisites:** `gcc`, Docker (for building samples).

```bash
make                    # build the loader
make samples            # build sample Windows binaries (requires Docker)
./my_wine samples/hello_world/hello_world.exe   # run a sample
```

For full build instructions, see the [README](../README.md).

---

## Reading Order

The docs below form a learning path. Each step builds on the previous — start with the README to get things building, then learn PE basics before diving into how the loader works.

1. **[README](../README.md)** — Build, run, project structure. Get things compiling first.
2. **[PE Format Primer](pe_format.md)** — Understand PE file structure (headers, sections, imports, RVA). You need this before reading the architecture.
3. **[Rationale](rationale.md)** — Why single-process, why direct dispatch, requirements, limitations. Understand the design choices.
4. **[Architecture](architecture.md)** — How it works: data flow, single-process model, syscall dispatch, TEB/PEB. The deep-dive.
5. **[CRT refptr Patching](refptr.md)** — CRT `.refptr` patching deep-dive. Specialized topic.

```
README.md → pe_format.md → rationale.md → architecture.md → refptr.md
  (build)      (format)        (why)           (how)         (deep-dive)
```

---

## Project Structure

High-level directory layout (not the full tree from README):

| Directory | Description | Start Here |
|---|---|---|
| `src/main.c` | Entry point: orchestrates map, patch, resolve, run | Read first |
| `Makefile` | Build configuration: loader and samples | — |
| `src/loader/` | Image mapping, import resolution, TEB/PEB, guest setup | `image_mapper.c`, `entry.c` |
| `src/stubs/` | Windows API stub implementations (ntdll, kernel32, msvcrt) | `crt_refptrs.c` |
| `src/syscall/` | Thunk generation, dispatcher entry, NT syscall dispatcher | `thunk_gen.c`, `dispatcher_entry_asm.S`, `dispatcher.c` |
| `src/pe_*.c` | PE format parsing (headers, imports, symbols) | `pe_headers.c` |
| `include/` | Public headers (PE structs, ABI macros, syscall constants) | `pe.h`, `nt_constants.h` |
| `samples/` | Mingw-w64 test programs | `hello_world/` |
| `tests/` | Unit tests | `test_parse.c` |

Two files worth exploring early: `run_guest.S` (naked assembly trampoline) and `dispatcher_entry_asm.S` (stack-switching dispatcher entry).

For the full tree, see the [README](../README.md) Project Structure section.

---

## Tracing a PE Execution

Step-by-step source code walkthrough showing the execution flow.

**Loader phase (single process, before guest runs):**

1. **`main()` in `src/main.c`** — Entry point. Opens the PE file, orchestrates the loading process.
2. **`map_image()` in `src/loader/image_mapper.c`** — Opens the PE file, maps it read-only, parses DOS/NT headers, maps the image at the preferred base, copies section data, sets per-section protections via `mprotect`.
3. **`patch_crt_refptrs()` in `src/stubs/crt_refptrs.c`** — Fixes CRT `.refptr` pointers to point at our Linux-side stubs.
4. **`resolve_imports()` in `src/loader/import_resolve.c`** — Walks the import descriptor chain and patches IAT entries to point at our stub functions.
5. **`setup_teb_peb()` in `src/loader/teb_peb.c`** — Allocates and populates the TEB and PEB; sets the GS base via `arch_prctl` (**src/loader/gs_base.c**).
6. **`setup_stack()` in `src/loader/image_mapper.c`** — Allocates the guest stack from the PE's stack size values.

**Guest entry (single process, after loader phase):**

1. **`run_guest_entry()` in `src/loader/entry.c`** — Calls `setup_guest_and_run()` from `guest_setup.c`.
2. **`src/loader/guest_setup.c`** — Single-process guest initialization:
   - `setup_signal_handlers()` — installs crash handlers (SIGSEGV, SIGILL)
   - `generate_all_thunks()` — generates syscall thunks that `call __wine_dispatcher`
   - `setup_unix_stack()` — allocates the UNIX stack used during syscall dispatch
   - Re-sets GS base to TEB, wires SEH chain into TEB
   - Patches `__acrt_iob_func` to return `__wine_iob_data` directly
3. **`run_guest.S`** — Naked assembly trampoline that switches to the guest stack and jumps to the PE entry point. No prologue/epilogue.

**Syscall dispatch (when guest calls NT syscall):**

1. Guest code → thunk (`mov rdi,NR; call __wine_dispatcher`)
2. **`dispatcher_entry_asm.S`** — `__wine_dispatcher` saves all guest state (`RCX`, `RDX`, `R8`, `R9`, `RSP`, return address) into `__wine_guest_regs`, switches to the UNIX stack, calls `c_dispatch_syscall()`, writes result back, restores guest state, returns
3. **`dispatcher.c`** — `c_dispatch_syscall(nr)` reads arguments from `__wine_guest_regs` (RCX/RDX/R8/R9 + guest stack for args 5+), dispatches to the appropriate handler function via switch/case

**Syscall dispatch details:**

The C dispatcher (`src/syscall/dispatcher.c`) reads arguments directly from `__wine_guest_regs` (populated by the assembly entry) and uses a switch/case table to find the handler. Each handler function is declared in the stub files (`src/stubs/ntdll_*.c`, `src/stubs/kernel32_*.c`, `src/stubs/msvcrt_*.c`) and implements the Windows API semantics using Linux primitives. For example, `NtWriteFile` for console output translates the Windows handle (STD_OUTPUT_HANDLE) to a Linux file descriptor and calls `write(2)`.

If no handler is registered, the dispatcher prints an error and raises `SIGSEGV`.

For the full flow diagram, see [Architecture](architecture.md) §1.

---

## Contributing Tips

Practical advice for new contributors:

- **Build a sample** with `make samples SAMPLE=hello_world` to get a test binary.
- **Run with** `./my_wine samples/hello_world/hello_world.exe` to see it working.
- **Add a new stub:** Create `src/stubs/ntdll_new.c` with a `WINE_STUB` function (defined in `include/wine_abi.h`), register it in `src/loader/import_table.c`, and add a handler case in `src/syscall/dispatcher.c` (both `c_dispatch_syscall` and `handle_syscall`).
- **Note:** All stubs use `ms_abi` (Windows x64 calling convention: RCX, RDX, R8, R9 for the first four args), not the Linux System V ABI.
- **Run tests** with `make test` after any changes.
- **Debug tip:** Start with `make samples SAMPLE=hello_world` as your test case — it's the simplest PE and exercises the core flow.
- **Debugging workflow:** Use `gdb` on the single process. Since there's no fork, you debug one process end-to-end. Set breakpoints on `__wine_dispatcher` (assembly entry) or `c_dispatch_syscall` (C dispatcher) to observe syscall interception. Set breakpoints in `run_guest.S` to observe the handoff from Linux to guest code.
- **Understanding a new import:** Search the PE's import table for the function name, then check `src/loader/import_table.c` to see if it's already registered. If not, add a `WINE_STUB` function and a dispatcher case.
- **Adding a new sample:** Create a C file in `samples/`, add it to `samples/samples.sh`, and run `make samples SAMPLE=your_sample` to cross-compile it.

### Useful Commands

- **`make`** — Build the loader binary.
- **`make samples`** — Build all sample Windows binaries (requires Docker for mingw-w64 cross-compilation).
- **`make samples SAMPLE=<name>`** — Build a single sample binary.
- **`make test`** — Run the unit test suite.
- **`gdb -ex 'break run_guest' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug the guest handoff.
- **`gdb -ex 'break __wine_dispatcher' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug syscall dispatch at the assembly entry point.
- **`gdb -ex 'break c_dispatch_syscall' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug syscall dispatch at the C dispatcher.
- **`strace ./my_wine samples/hello_world/hello_world.exe`** — Trace system calls for the single process.

### Testing Workflow

1. Build the sample you'll test: `make samples SAMPLE=hello_world`
2. Run it: `./my_wine samples/hello_world/hello_world.exe`
3. If it crashes, run under `gdb` or `strace` to get more info.
4. After any changes, rebuild with `make` and re-run the sample.
5. Run `make test` to ensure existing tests still pass.

### Common Pitfalls

- **Stack alignment:** The Windows x64 ABI requires 16-byte stack alignment before calls. Use `__attribute__((force_align_arg_pointer))` on stub functions.
- **Calling convention:** Windows x64 uses RCX/RDX/R8/R9 for the first four integer args (not RDI/RSI/RDX/RCX as in Linux). The `ms_abi` attribute handles this — never forget it on stub functions.
- **Red zone:** With `-mno-red-zone`, the area 128 bytes below RSP is not guaranteed to be preserved. The assembly dispatcher and guest code must not rely on it.

---

## Related Documents

- [README](../README.md) — Build, run, project structure
- [PE Format Primer](pe_format.md) — PE format primer
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [Architecture](architecture.md) — Deep-dive architecture
- [CRT refptr Patching](refptr.md) — CRT .refptr patching deep-dive
