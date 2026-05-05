# Onboarding

Welcome to my_wine. This guide walks you through the documentation and helps you understand the project structure, so you can start contributing quickly.

---

## What Is my_wine?

my_wine is a minimal user-space PE (Portable Executable) loader for Linux x86_64. It runs mingw-w64-compiled Windows executables without Wine — no virtual machine, no translation layer, just a thin loader that maps the PE image into memory and intercepts the few syscalls the guest needs.

The loader maps the PE at its preferred base address, patches CRT globals (`.refptr` section), resolves imports by wiring them to stub functions, and sets up the TEB/PEB structures expected by Windows x64 code. It then generates syscall thunks that call `__wine_dispatcher` directly, switching from the guest stack to a pre-allocated UNIX stack for each intercepted syscall.

The result: Windows executables compiled with mingw-w64 can run natively on Linux with minimal overhead.

The project uses a **single-process model**: everything runs in one process. The loader performs all heavy lifting (mapping, import resolution, TEB/PEB setup, thunk generation, signal handlers) in the same process that subsequently runs the guest code. There is no `fork()` — the guest executes in-place after setup completes. This simplifies debugging, eliminates the parent/child lifecycle, and mirrors Wine's architecture more closely.

Syscall interception works through **direct dispatch**: each NT syscall has a dynamically generated 23-byte thunk (absolute indirect call via `push rdi; mov rdi,imm32; mov rax,imm64; call rax; pop rdi; ret`) that calls `__wine_dispatcher`. The assembly dispatcher saves guest register state to a global struct (`__wine_guest_regs` in `dispatcher_entry.c`), switches to a pre-allocated UNIX stack, calls the C dispatcher (`c_dispatch_syscall`), writes results back, restores guest state, and returns. No seccomp filters or signal handlers are not used for dispatch.

## Quick Start

**Prerequisites:** `gcc`, Docker (for building samples).

```bash
make                                    # build the loader
make samples                            # build all sample Windows binaries (requires Docker)
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

| Directory / File | Description | Start Here |
|---|---|---|
| `src/main.c` | Entry point: 10-step pipeline orchestrator | Read first |
| `src/loader/` | Image mapping, import resolution, TEB/PEB, guest setup, entry | `main.c` → `loader/` |
| `src/stubs/` | Windows API stub implementations (ntdll, kernel32, msvcrt) | `ntdll_*.c`, `kernel32_*.c`, `crt_*.c` |
| `src/syscall/` | Thunk generation, dispatcher entry, UNIX stack management, NT syscall dispatcher | `thunk_gen.c`, `dispatcher_entry.c`, `dispatcher.c` |
| `src/pe_*.c` | PE format parsing (headers, imports, symbols, RIP scan | `pe_headers.c` |
| `src/run_guest.S` | Naked assembly trampoline — switches to guest stack and jumps to PE entry | — |
| `include/` | Public headers (PE structs, ABI macros, syscall constants) | `pe.h`, `pe_parser.h`, `nt_constants.h`, `wine_abi.h`, `syscall/*.h` |
| `samples/` | Mingw-w64 test programs | `hello_world/` |
| `tests/` | Unit tests | `test_parse.c` |

**Key sub-modules in `src/loader/`:**

| File | Purpose |
|---|---|
| `image_mapper.c` | `map_image()` — opens PE, maps at preferred base, copies sections, sets protections |
| `import_resolve.c` | `resolve_imports()` — walks import descriptors, patches IAT to point at stubs |
| `import_table.c` | Static `import_entry_t[]` table mapping DLL+name → stub function pointer |
| `import_init.c` | `init_msvcrt_imports()` — dynamically populates msvcrt entries from COFF symbols |
| `ordinal_table.c` | Ordinal-based import resolution |
| `teb_peb.c` | `setup_teb_peb()` — allocates TEB/PEB via mmap; `setup_stack()` — allocates guest stack |
| `gs_base.c` | `set_gs_base()` — sets GS segment base to TEB via `arch_prctl` |
| `crash_handlers.c` | `setup_signal_handlers()` — installs SIGSEGV/SIGILL crash handlers; `seh_crash_handler` |
| `guest_setup.c` | `setup_guest_and_run()` — signal handlers, SEH chain, thunk generation, `__acrt_iob_func` patching, UNIX stack setup, GS base finalization, jump to guest |
| `entry.c` | `run_guest_entry()` — thin wrapper that calls `setup_guest_and_run()` from `guest_setup.c` |

Two files worth exploring early: `run_guest.S` (naked assembly trampoline) and `dispatcher_entry_asm.S` (stack-switching dispatcher entry).

For the full tree, see the [README](../README.md) Project Structure section.

---

## Tracing a PE Execution

Step-by-step source code walkthrough showing the execution flow.

### Loader phase (single process, before guest runs)

The pipeline in `src/main.c` (`main()`) runs 10 steps:

1. **`map_image()`** in `src/loader/image_mapper.c` — Opens the PE file, maps it read-only, parses DOS/NT headers, maps the image at the preferred base address, copies section data, sets per-section protections via `mprotect`.

2. **`init_msvcrt_imports()` / `init_import_table()`** in `src/loader/import_init.c` and `src/loader/import_table.c` — Initializes dynamic msvcrt import entries from COFF symbol table and sorts the import table for `bsearch`.

3. **`patch_crt_refptrs()`** in `src/stubs/crt_refptrs.c` — Fixes CRT `.refptr` pointers so the PE can find our Linux-side stub variables (CTOR/DTOR lists, image base, etc.).

4. **`resolve_imports()`** in `src/loader/import_resolve.c` — Walks the import descriptor chain and patches IAT entries to point at our stub functions from `import_table`.

5. **`setup_teb_peb()`** in `src/loader/teb_peb.c` — Allocates and populates the TEB and PEB via `mmap`; sets TEB self-referential pointers and PEB image base. Does NOT set GS base yet (deferred until guest_setup.c to avoid corrupting glibc TLS).

6. **`setup_stack()`** in `src/loader/teb_peb.c` — Allocated and called from `main.c` (step in loader pipeline). Allocates the guest stack according to the PE's `SizeOfStackReserve` / `SizeOfStackCommit` (minimum 512KB committed for CRT startup). **Note:** this function is called from `main.c`, not from `guest_setup.c`.

7. Zero `.data` section and `seed_bss_vars()` in `src/main.c` — Zero the `.data` section and pre-seed `argc`, `argv`, `envp` in the PE's `.bss` section at COFF-derived offsets.

8. **`run_guest_entry()`** in `src/loader/entry.c` — Thin wrapper that delegates to `setup_guest_and_run()` in `guest_setup.c`, completing the handoff from loader to guest setup.

### Guest setup (single process, `src/loader/guest_setup.c`)

`setup_guest_and_run()` performs all the remaining initialization before jumping to guest code — the steps deferred from the main pipeline because they must happen after `setup_stack()` is called from `main.c` (step 7 above):

1. **`setup_signal_handlers()`** (from `crash_handlers.c`) — Installs SIGSEGV and SIGILL crash handlers.
2. **`setup_seh_and_thunks()`** — Creates the static SEH frame, calls `generate_all_thunks()` (from `src/syscall/thunk_gen.c`) to produce all syscall thunks, and calls `setup_unix_stack()` (from `src/syscall/dispatcher_entry.c`) to allocate the 128KB UNIX stack used during syscall dispatch.
3. **`apply_final_patches()`** — Patches `__acrt_iob_func` (finds the `jmp` thunk in `.text` and replaces with `movabs rax,<addr>; ret` to return `__wine_iob_data` directly); ensures `.bss` and other writable sections have `PROT_WRITE`.
4. **`finalize_guest_state()`** — Sets GS base to TEB via `arch_prctl` (from `gs_base.c`); wires SEH chain into TEB at `gs:[0x00]`.
5. **`jump_to_guest()`** — Looks up `ExitProcess` from the import table, then calls `run_guest()` from `src/run_guest.S` which switches to the guest stack and jumps to the PE entry point.

### Guest entry (`src/run_guest.S`)

Naked assembly trampoline — no prologue/epilogue. Switches to the guest stack, sets up Microsoft x64 calling convention arguments (`rcx=argc`, `rdx=argv`, `r8=envp`), and `call`s the PE entry point. After entry returns (exit code in RAX), calls `ExitProcess(exit_code)` which terminates via `NtTerminateProcess`.

### Syscall dispatch (when guest calls NT syscall)

1. Guest code → **thunk** (23-byte: `push rdi; mov rdi,NR; mov rax,imm64(dispatcher); call rax; pop rdi; ret`)
2. **`dispatcher_entry_asm.S`** — `__wine_dispatcher` saves all guest state (RCX, RDX, R8, R9, RSP, return address) into `__wine_guest_regs` (declared in `dispatcher_entry.c`), switches to the UNIX stack, calls `c_dispatch_syscall()`, writes result back, restores guest state, returns.
3. **`dispatcher.c`** — `c_dispatch_syscall(nr)` reads arguments from `__wine_guest_regs` (RCX/RDX/R8/R9 + guest stack for args 5+) and dispatches to the appropriate handler function via switch/case.

Each handler function is declared in the stub files (`src/stubs/ntdll_*.c`, `src/stubs/kernel32_*.c`, `src/stubs/crt_*.c`) and implements the Windows API semantics using Linux primitives. For example, `handler_NtWriteFile` translates the Windows handle (STD_OUTPUT_HANDLE) to a Linux file descriptor and calls `write(2)`.

If no handler is registered, the dispatcher writes an error and raises `SIGSEGV`.

For the full flow diagram, see [Architecture](architecture.md) §1.

---

## Contributing Tips

Practical advice for new contributors:

- **Build a sample** with `make samples SAMPLE=hello_world` to get a test binary.
- **Run with** `./my_wine samples/hello_world/hello_world.exe` to see it working.
- **Add a new stub:** Create a function with the `WINE_STUB` attribute (defined in `include/wine_abi.h` — this gives `ms_abi` calling convention + `force_align_arg_pointer`), add an entry to `src/loader/import_table.c`, and if it's a syscall handler, add a case to `c_dispatch_syscall()` in `src/syscall/dispatcher.c`.
- **Note:** All stubs use `ms_abi` (Microsoft x64 calling convention: RCX, RDX, R8, R9 for the first four args), not the Linux System V ABI. The `WINE_STUB` macro handles this — never forget it on stub functions.
- **Run tests** with `make run-test` after any changes.
- **Debug tip:** Start with `hello_world` as your test case — it's the simplest PE and exercises the core flow.
- **Understanding a new import:** Search the PE's import table for the function name, then check `src/loader/import_table.c` to see if it's already registered. If not, add a `WINE_STUB` function and a dispatcher case.
- **Adding a new sample:** Create a C file in `samples/`, add it to `samples/samples.sh`, and run `make samples SAMPLE=your_sample` to cross-compile it.

### Useful Commands

- **`make`** — Build everything: loader binary, all sample Windows binaries, and all test binaries.
- **`make samples`** — Build all sample Windows binaries (requires Docker for mingw-w64 cross-compilation).
- **`make samples SAMPLE=<name>`** — Build a single sample binary.
- **`make run-sample SAMPLE=<name>`** — Build + run a sample under `./my_wine`.
- **`make tests`** — Build the loader and all test binaries.
- **`make run-test`** — Run all tests. Use `make run-test TEST=<name>` to filter (e.g. `TEST=parse`).
- **`make fclean`** — Deep clean: remove build directory, generated headers, loader binary, and all sample `.exe` files.
- **`make re`** — Rebuild from scratch (`fclean` then `all`).
- **`gdb -ex 'break run_guest' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug the guest handoff (stack switch + entry point call).
- **`gdb -ex 'break __wine_dispatcher' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug syscall dispatch at the assembly entry point.
- **`gdb -ex 'break c_dispatch_syscall' -ex run --args ./my_wine samples/hello_world/hello_world.exe`** — Debug syscall dispatch at the C dispatcher.
- **`strace ./my_wine samples/hello_world/hello_world.exe`** — Trace system calls for the single process.

### Testing Workflow

1. Build the sample you'll test: `make samples SAMPLE=hello_world`
2. Run it: `./my_wine samples/hello_world/hello_world.exe`
3. If it crashes, run under `gdb` or `strace` to get more info.
4. After any changes, rebuild with `make` and re-run the sample.
5. Run `make run-test` to ensure existing tests still pass.

### Common Pitfalls

- **Stack alignment:** The Windows x64 ABI requires 16-byte stack alignment before calls. The `WINE_STUB` macro includes `force_align_arg_pointer` which handles this automatically.
- **Calling convention:** Windows x64 uses RCX/RDX/R8/R9 for the first four integer args (not RDI/RSI/RDX/RCX as in Linux). The `ms_abi` attribute from `WINE_STUB` handles this — never forget it on stub functions.
- **Red zone:** Stub functions and assembly entry points use `-mno-red-zone`. The area 128 bytes below RSP is not guaranteed to be preserved. Never rely on it.
- **GS base:** After GS is switched to the TEB (in `guest_setup.c`), glibc no longer works because it accesses vDSO via GS-relative offsets. Any code that must run after that point (e.g. in `jump_to_guest`) must use inline syscalls, not glibc calls.
- **`setup_teb_peb()` vs `setup_guest_and_run()`:** TEB/PEB allocation happens in `teb_peb.c` during the loader phase (while GS still points to Linux TLS). GS base switch to TEB happens later in `guest_setup.c`, right before the guest jumps. Never set GS base during `setup_teb_peb()` or glibc TLS access breaks.

---

## Related Documents

- [README](../README.md) — Build, run, project structure
- [PE Format Primer](pe_format.md) — PE format primer
- [Rationale](rationale.md) — Design decisions, requirements, limitations
- [Architecture](architecture.md) — Deep-dive architecture
- [CRT refptr Patching](refptr.md) — CRT .refptr patching deep-dive
- [Limitations & Investigations](limitations_investigations.md) — Known limitations and investigation notes
- [Wine vs my_wine](wine_vs_my_wine.md) — Comparison with the full Wine project
