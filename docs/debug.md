# Debug Notes

## Runtime Diagnostics

Runtime diagnostics are controlled with `MY_WINE_DEBUG_LEVEL`.

| Level | Meaning |
|---|---|
| `0` or unset | Quiet normal execution; guest stdout/stderr is not mixed with loader traces. |
| `1` | Setup milestones and diagnostic warnings. |
| `2` | Loader, import, DLL, thunk, and syscall trace summaries. |
| `3` | Very verbose per-symbol, per-path, and per-IAT diagnostics. |

Examples:

```sh
MY_WINE_DEBUG_LEVEL=1 ./my_wine samples/hello_world/hello_world.exe
MY_WINE_DEBUG_LEVEL=2 ./my_wine samples/hello_world/hello_world.exe
MY_WINE_DEBUG_LEVEL=3 ./my_wine samples/dll_loader_32/dll_loader_32.exe
```

The wrapper keeps the same environment when it `execvp`s `my_wine64` or
`my_wine32`, so the same variable works through the wrapper and when running a
backend directly.

## Binary Selection

Most debugging should start through the wrapper:

```sh
./my_wine samples/hello_world/hello_world.exe
./my_wine samples/hello_world_32/hello_world_32.exe
```

Run a backend directly only when you need backend-specific breakpoints or want
to bypass wrapper detection:

```sh
./my_wine64 samples/hello_world/hello_world.exe
./my_wine32 samples/hello_world_32/hello_world_32.exe
```

`my_wine64` rejects PE32 inputs and `my_wine32` expects PE32 inputs.

## GDB Entry Points

Useful breakpoints for PE32+:

```sh
gdb -ex 'break main' \
    -ex 'break run_guest_entry' \
    -ex 'break setup_guest_and_run' \
    -ex run --args ./my_wine64 samples/hello_world/hello_world.exe
```

```sh
gdb -ex 'break __wine_dispatcher' \
    -ex 'break c_dispatch_syscall' \
    -ex run --args ./my_wine64 samples/hello_world/hello_world.exe
```

Useful breakpoints for PE32:

```sh
gdb -ex 'break main' \
    -ex 'break pe32_run_guest' \
    -ex 'break c_dispatch_syscall' \
    -ex run --args ./my_wine32 samples/hello_world_32/hello_world_32.exe
```

## Direct Dispatch

Current syscall dispatch is direct thunk dispatch, not seccomp/SIGSYS:

1. Import resolution writes NT imports to generated thunks.
2. The thunk calls `__wine_dispatcher`.
3. `dispatcher_entry_asm.S` saves guest registers and switches to the UNIX
   stack prepared by `setup_unix_stack()`.
4. `c_dispatch_syscall()` decodes guest arguments and calls the selected
   handler.
5. The assembly entry restores guest state and returns to the guest caller.

PE32+ thunks are 23 bytes. PE32 thunks are 15 bytes. The generated dispatcher
switch body comes from `include/nt_syscalls.def` via
`scripts/gen_dispatcher.py`.

## Crash Reports

Crash handlers report the guest instruction and stack pointer when they can,
for example:

```text
my_wine: CRASH rip=0x00000001400011d4 rsp=0x00007fc987d5bf08
```

For PE32, expect `eip`/`esp` context in the same style. A crash exits the
backend process directly; there is no parent loader process collecting
`waitpid()` status.

## Verification Commands

```sh
make my_wine my_wine64 my_wine32
make run-tests
make check-generated
make run-samples SAMPLE=hello_world
make run-samples SAMPLE=hello_world_32
```

Use `strace ./my_wine <sample.exe>` for host syscall traces. For guest NT
syscall flow, prefer `MY_WINE_DEBUG_LEVEL=2` or breakpoints on
`__wine_dispatcher` / `c_dispatch_syscall`.
