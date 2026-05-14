# Debug Notes — my_wine

## Runtime Diagnostic Levels

Runtime diagnostics are controlled with `MY_WINE_DEBUG_LEVEL`.

- `0` or unset: quiet normal execution; guest stdout/stderr is not mixed with loader traces.
- `1`: setup milestones and diagnostic warnings.
- `2`: loader, import, DLL, thunk, and syscall trace summaries.
- `3`: very verbose per-symbol, per-path, and per-IAT diagnostics.

Examples:

```sh
MY_WINE_DEBUG_LEVEL=1 ./my_wine samples/hello_world/hello_world.exe
MY_WINE_DEBUG_LEVEL=2 ./my_wine samples/hello_world/hello_world.exe
MY_WINE_DEBUG_LEVEL=3 ./my_wine samples/dll_loader_32/dll_loader_32.exe
```

## Crash Status

```
my_wine: CRASH rip=0x00000001400011d4 rsp=0x00007fc987d5bf08
my_wine: child exited with code 139
```

- **RIP = 0x1400011d4** — instruction `mov 0x8(%rax),%rsi` inside `__tmainCRTStartup`
- **RSP = guest stack** — valid, in our allocated region
- **Signal = SIGSEGV (11)** — segfault, not seccomp/SIGSYS

## Entry Point

- PE `AddressOfEntryPoint = 0x14f0`
- `0x14f0` is **inside** `__tmainCRTStartup` (function starts at `0x1190`), not at the prologue
- The linker optimizes the entry point to skip TLS callback code at the top
- Entry at `0x14f0` executes the loop at `0x13f0`, which jumps to `0x11cd`

## Crash Chain

Entry at `0x14f0` → loop at `0x13f0`-`0x1440` → jump to `0x11cd`:
```
1400011cd:  mov    0x31cc(%rip),%rbx        # .refptr.__native_startup_lock (RVA 0x43a0)
1400011d4:  mov    0x8(%rax),%rsi           # RAX was loaded from gs:[0x30]
```

`gs:[0x30]` returns `0x0` (our TEB slot 0x30 is zero). So `RAX=0`, and `mov 0x8(%rax)` dereferences address `0x8` → **SIGSEGV**.

## Known Refptr Patches (in msvcrt.c)

Currently patched: `mingw_app_type` (0x4460), `_fmode` (0x4420), `_commode` (0x4400).

Missing patches (needed by `__tmainCRTStartup`):
- `__native_startup_lock` (0x43a0) — **crash point**
- `__native_startup_state` (0x43b0)
- `__image_base__` (0x4340)
- `__dyn_tls_init_callback` (0x4330)
- `__mingw_oldexcpt_handler` (0x4390)
- `__dowildcard` (0x4410)
- `__xc_a` / `__xc_z` (0x43c0, 0x43d0)
- `_newmode` (0x4450)
- `_MINGW_INSTALL_DEBUG_MATHERR` (0x42f0)
- `mingw_initltsdrot_force` (0x4480)
- `mingw_initltsdyn_force` (0x4490)
- `mingw_initltssuo_force` (0x44a0)
- `mingw_initcharmax` (0x4470)

Plus data variables (`argc`, `argv`, `envp`, `startinfo`, `managedapp`, `has_cctor`, etc.) at 0x7000+.

## Runtime State at Crash

- PE mapped at 0x140000000 ✓
- Sections copied and mprotected ✓
- Imports resolved (KERNEL32 + msvcrt) ✓
- TEB allocated, GS set via arch_prctl(ARCH_SET_GS) ✓
- TEB[0] = TEB self-pointer, TEB[0x60] = PEB pointer
- **TEB[0x30] = 0x0** (not set — this is the Windows current thread handle slot)
- PEB[0x8] = image base, PEB[0x2] = BeingDebugged (0) ✓
- Guest stack: 64KB committed, 2MB reserved ✓
- Sigsys handler + seccomp filter installed ✓
- Crash handler installed (SIGSEGV, SIGILL) — writes RIP/RSP to stderr ✓

## What Works

- `arch_prctl(ARCH_SET_GS)` succeeds (test confirmed)
- GS-relative memory reads work: `gs:[0]`, `gs:[0x60]` return correct values
- `ARCH_GET_GS` returns -1 on kernel 7.0 (deprecated but functional)
- `setcontext()` with custom stack — kernel delivers signals to new stack
- All imports resolve correctly

## What's Broken

1. **TEB[0x30] is zero** — `__tmainCRTStartup` reads `gs:[0x30]` (current thread pointer). Returns 0 → null deref
2. **Missing refptr patches** — 15+ refptrs not patched, will crash when reached
3. **Entry point skips prologue** — callee-saved registers not pushed, local variable area not allocated, RBP=0
4. **No thread object** — `gs:[0x30]` on real Windows points to a `PETHREAD` structure, not just the TEB

## Next Steps

1. Set `TEB[0x30]` to a valid thread pointer (or skip the lock code by jumping to the prologue at `0x1190`)
2. Add all missing refptr patches to `msvcrt.c`
3. Patch data variables (`argc`, `argv`, `envp`, `startinfo`, etc.)
4. Handle `_pei386_runtime_relocator` (base relocation processing)
5. Add `__xc_a`/`__xc_z` for CRT init/fini arrays
6. Add `SetUnhandledExceptionFilter` and `__C_specific_handler` in import table (already present but verify)
