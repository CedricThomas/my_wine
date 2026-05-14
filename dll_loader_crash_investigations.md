# DLL Loader 32 Crash Investigation

## Crash Summary

`samples/dll_loader_32/dll_loader_32.exe` crashes with `EIP=0x00000000` (NULL pointer call) immediately after printing "=== DLL Loader Test ===\r\n". The crash occurs before `LoadLibraryA` executes.

**10 out of 11 other 32-bit samples pass.** Only this one fails.

## Latest Crash Handler Output

```
CRASH: SIGSEGV
CRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: ucontext invalid, skipping register dump
CRASH: fallback ESP=0xf7c56fd0, EBP=0xf7c56fd0, signal_ret=0x00000000, stack=[00000000 00000000 00000000 00000015]
```

### Analysis

- `ucontext=0x0000006b` — The kernel-provided ucontext pointer is 0x6b, which is clearly invalid (below 0x1000, in the null page).
- `ESP=EBP=0xf7c56fd0` — ESP equals EBP, both in the host glibc range (0xf7xxxxx).
- `signal_ret=0x00000000` — The return address from [ESP+4] is zero, meaning the kernel's sigreturn trampoline address on this frame is 0x0.
- `stack=[0,0,0,15]` — The 4 words at ESP are `[0x00000000 0x00000000 0x00000000 0x00000015]`. The last value 0x15 = 21 decimal.

The ESP/EBP both being 0xf7c56fd0 in the **host** address space means the signal handler is NOT running on our alternate signal stack — it's running on the host stack where the crash happened. This suggests `sigaltstack` may have failed or been overwritten.

### Stack Frame Reconstruction

The signal frame at ESP=0xf7c56fd0:
```
[ESP]     = 0x00000000 (sigcookie[0] — always 0)
[ESP+4]   = 0x00000000 (sigcookie[1] — should be sigreturn addr)
[ESP+8]   = siginfo_t (starts here)
```

The sigcookie[1] at [ESP+4] should contain the sigreturn function address, but it's 0x0. This is the kernel's signal frame that got corrupted.

## Crash State (from GDB)

```
EIP  = 0x00000000  (NULL)
EAX  = 0x00000001
ESP  = 0xf7cXXXXX  (in host libc range!)
EBP  = 0xf7cXXXXX  (in host libc range!)
ECX  = 0x40404b
si_addr = 0x00000000
```

### EBP-ESP = 212 bytes

This is consistent with `_main` having `char buf[128]` + function prologue. The crash is **inside `_main`** (or its caller chain), not in the CRT startup code.

### ESP in host libc range (0xf7xxxxxx)

The ESP is in the host glibc/shared library range, NOT in the expected guest stack area. The guest stack should be near 0x7FFE0000. This is the primary indicator of something going wrong with the stack setup or a stack pointer corruption during the CRT startup.

## PE Structure

| Field | Value |
|-------|-------|
| Format | PE32 (i386) |
| ImageBase | 0x00400000 |
| SizeOfImage | 0x00049000 |
| EntryPoint | 0x000014C0 → `__tmainCRTStartup` at VA 0x4014C0 |
| Section Align | 0x1000 |
| File Align | 0x200 |

### Sections

| # | Name | VA | VSize | FOff | FSize |
|---|------|-----|-------|------|-------|
| 0 | .text | 0x401000 | 0x1aa4 | 0x0600 | 0x1c00 |
| 1 | .data | 0x403000 | 0x002c | 0x2200 | 0x0200 |
| 2 | .rdata | 0x404000 | 0x0840 | 0x2400 | 0x0800 |
| 3 | .eh_frame | 0x405000 | 0x01b0 | 0x2e00 | 0x0200 |
| 4 | .bss | 0x406000 | 0x00c8 | 0x0000 | 0x0000 |
| 5 | .idata | 0x407000 | 0x0510 | 0x3000 | 0x0400 |
| 6 | .CRT | 0x408000 | 0x0034 | 0x3600 | 0x0200 |
| 7 | .tls | 0x409000 | 0x0008 | 0x3800 | 0x0200 |
| 8 | .reloc | 0x40a000 | 0x02ac | 0x3a00 | 0x0400 |

### Entry Point: `__tmainCRTStartup` (0x4014C0)

The CRT startup path:
```
__tmainCRTStartup (0x4014C0)
  → __pei386_runtime_relocator (0x401B80)  [call at 0x40123c]
  → call *0x407128 (SetUnhandledExceptionFilter, idx 13 in IAT)
  → __set_invalid_parameter_handler
  → __fpreset
  → ___p__acmdln
  → command line parsing
  → _initterm
  → __getmainargs (via call *0x40714C, idx 0 in msvcrt IAT)
  → _main (0x402740) [call at 0x401391]
```

### Two IAT Copies in .idata

The PE has two IAT (Import Address Table) copies within the `.idata` section:

| IAT Copy | Location | Filled By | Notes |
|----------|----------|-----------|-------|
| Descriptor IAT (FirstThunk) | 0x4070F8 - 0x407298 | Our pass 1 | Contains resolved function addresses |
| PLT IAT | 0x4074A0+ | Not by our loader | Contains HIBIT32 values (0x00007014, etc.) |

The descriptor IAT (FirstThunk at 0x4070F8) is filled by our loader during pass 1 with correct function addresses. The PLT IAT is a separate copy that our loader does NOT fill.

### Import Descriptors

```
ID[0] = kernel32.dll
  Name RVA = 0x74b0 ("kernel32.dll")
  OriginalFirstThunk (ILT) = 0x703c
  FirstThunk (IAT) = 0x70f8
  19 functions: DeleteCriticalSection, EnterCriticalSection, ExitProcess, FreeLibrary,
                GetLastError, GetModuleHandleA, GetProcAddress, GetStartupInfoA,
                GetStdHandle, InitializeCriticalSection, LeaveCriticalSection,
                LoadLibraryA, SetUnhandledExceptionFilter, Sleep, TlsGetValue,
                VirtualProtect, VirtualQuery, WriteFile, lstrcpyA, lstrlenA

ID[1] = msvcrt.dll
  Name RVA = 0x74c0 ("msvcrt.dll")
  OriginalFirstThunk (ILT) = 0x7090
  FirstThunk (IAT) = 0x714c
  12 functions: __getmainargs, __initenv, __lconv_init, __p__acmdln, __p__commode,
                __p__fmode, __set_app_type, __setusermatherr, _amsg_exit, _cexit,
                _initterm, _iob, _onexit, abort, calloc, exit, fprintf, free,
                fwrite, malloc, memcpy, signal, strlen, strncmp, vfprintf
```

### IAT for LoadLibraryA

```
VA 0x407124 = IAT entry for LoadLibraryA
  Resolved to: 0x08055a80 (our LoadLibraryA stub)
  Verified correct both in pass 1 and in pre-entry IAT scan
```

### Direct Call in `_main`

The `_main` function at 0x402740 contains:
```
402782: ff 15 24 71 40 00    call *0x407124     ; LoadLibraryA
```

This directly calls the IAT at 0x407124 (descriptor IAT).

## IAT Verification

Our loader verifies the IAT after pass 1 resolution:
- `LoadLibraryA` IAT entry at VA 0x407124 → 0x08055a80 ✓
- All 4 JMP thunk targets → non-zero ✓
- All IAT entries read back correctly after write ✓

**Pre-entry check**: Immediately before jumping to the PE, the IAT at 0x407124 is still 0x08055a80.

## Relocations

The `.reloc` section (0x40a000, 0x2ac bytes) contains relocation entries.

Analysis shows the relocations are standard 32-bit relocations. Since the PE is loaded at its preferred base (0x400000), the relocation delta is 0, meaning relocations are a no-op.

## What Works

All other 32-bit samples pass:
- hello_world_32 ✓
- multi_import_32 ✓
- multi_syscall_32 ✓
- dispatcher_regs_32 ✓
- sync_test_32 ✓
- file_io_32 ✓
- heap_test_32 ✓
- virtual_mem_32 ✓
- time_test_32 ✓
- null_deref_32 ✓

**Key difference**: `dll_loader_32` is the ONLY sample that calls `LoadLibraryA` at runtime.

## What Doesn't Work

### Hypothesis: `__pei386_runtime_relocator` corruption

The `__pei386_runtime_relocator` function (MinGW CRT) at 0x401b80 is called early in `__tmainCRTStartup`. Even though delta=0 (preferred base), the function might still do something that corrupts memory or registers.

### Hypothesis: Stack corruption during CRT startup

The ESP in the crash is in the host libc range (0xf7cXXXXX). The guest stack should be at ~0x7FFE0000. Something in the CRT startup sequence (possibly `__getmainargs`, `_initterm`, or `_malloc`) is setting ESP to a host address.

The crash frame (EBP-ESP=212) matches `_main`'s local buffer, so the crash is in `_main` code. But the ESP was set by the CRT startup, not by `_main` itself.

### Hypothesis: `__getmainargs` or `_initterm` accessing host memory

`__getmainargs` is resolved to our stub at 0x08058130. Our stub writes `g_argv_fallback` and `_acmdln`. If these pointers are wrong (host addresses vs. guest addresses), the PE code would access host memory.

### Hypothesis: `__getmainargs` returns host pointers

If `__getmainargs` returns argv/envp pointers that point to host memory (our `g_argv_page` is in the host address space), and the PE code dereferences these as if they were guest addresses, it would crash.

## What Was Tried

1. **NOP `__pei386_runtime_relocator`** at VA 0x40123c: No change. Crash persists at EIP=0x0.

2. **Jump directly to `_main`** (bypassing all CRT startup): Not yet fully tested (build issue with hardcoded VA).

3. **IAT verification before entry**: All entries correct, including LoadLibraryA at 0x407124 → 0x08055a80.

4. **Crash handler ucontext validation**: Fixed crash handler to not crash itself when ucontext is invalid (ucontext=0x6b).

5. **Signal alt stack verification**: The signal handler is NOT running on the alt stack — it's on the host stack (ESP=0xf7c56fd0). This suggests `sigaltstack` may have been overwritten by the PE/CRT code.

6. **GDB breakpoint at 0x402782** (LoadLibraryA call): Process crashes BEFORE reaching this point. The crash is in the CRT setup or transition from CRT to `_main`.

7. **GDB inspection of signal frame**: sigcookie[1] at [ESP+4] is 0x0 (should be sigreturn address). The signal frame is corrupted.

## Key Questions

1. **Why is ESP in the host libc range (0xf7cXXXXX) instead of the guest stack?** The guest stack should be near 0x7FFE0000.
2. **Why is the signal handler running on the host stack?** Did the PE code overwrite the sigaltstack?
3. **Is the crash actually inside `_main` or earlier in the CRT path?** The EBP-ESP=212 frame matches `_main`'s `buf[128]`, but this might be from a different frame entirely.
4. **Does the CRT write host addresses into the PE's data section?** If `__getmainargs` returns pointers to host memory, and the PE dereferences them, they'd point to host addresses.
5. **Is `LoadLibraryA` returning NULL?** EAX=0x1 in GDB suggests it didn't return yet.
6. **Did `__pei386_runtime_relocator` corrupt the sigaltstack or something else?** Even though delta=0, it still iterates the relocation table and may do writes.

## Sample Code

```c
int main(void)
{
    char buf[128];
    HMODULE hDll, hMod;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);  // ← banner prints after this

    out("=== DLL Loader Test ===\r\n");  // ← banner prints OK

    hDll = LoadLibraryA("exportlib.dll");  // ← crash before this line
    // ...
}
```

The crash happens between `out()` completing and `LoadLibraryA` starting. The EIP=0x0 indicates a NULL pointer dereference at the call target.

## `_main` Disassembly (Critical Section)

```
402740: lea    0x4(%esp),%ecx     ; stack alignment preamble
402744: and    $0xfffffff0,%esp
402747: push   -0x4(%ecx)
40274a: push   %ebp
40274b: mov    %esp,%ebp
40274d: push   %edi
40274e: push   %esi
40274f: push   %ebx
402750: push   %ecx
402751: sub    $0xa8,%esp         ; 168 bytes local vars
402757: call   4017f0 <___main>   ; GC initialization
40275c: movl   $0xfffffff5,(%esp) ; -11 = STD_OUTPUT_HANDLE
402763: call   *0x407118          ; GetStdHandle (IAT)
402769: sub    $0x4,%esp          ; stack alignment fixup
40276c: mov    %eax,0x40603c      ; save hStdout to global
402771: mov    $0x40404b,%eax     ; string ptr for banner
402776: call   4015d0 <_out>      ; _out() — banner prints OK
40277b: movl   $0x404065,(%esp)   ; "exportlib.dll"
402782: call   *0x407124          ; LoadLibraryA (IAT) ← CRASH HERE
402788: sub    $0x4,%esp
40278b: test   %eax,%eax
40278d: je     4028d2 <_main+0x192> ; FAIL path
402793: mov    %eax,%ebx          ; hDll
```

The crash happens at 0x402782 (`call *0x407124`). The IAT at 0x407124 was verified as 0x08055a80 (LoadLibraryA stub) before entry. But at crash time, EIP=0x0, meaning the IAT entry was 0x0.

### Key Observation: The IAT is 0x0 at crash time

Since the IAT was correct (0x08055a80) at loader verification time, something must overwrite it between loader setup and `_main` executing. The most likely culprits:
1. `__pei386_runtime_relocator` — iterates all relocations (even when delta=0)
2. A function in the CRT path that writes to `.idata` 
3. Memory corruption from an incorrect pointer write

## GDB Findings

1. **Breakpoint at 0x402782 (LoadLibraryA call)**: Process crashes BEFORE reaching this point.
2. **Register dump at crash**: EIP=0x0, EAX=0x1, ESP=0xf7cf5e54, EBP=0xf7cf5f28, ECX=0x40404b.
3. **Stack frame at crash**: EBP-ESP = 212 bytes, consistent with `_main`'s `buf[128]` + prologue.
4. **Backtrace**: Shows `#0 0x0`, `#1 0x19`, `#2 0x0` — corrupted frame chain.
5. **Signal frame analysis**: sigcookie[1] = 0x0 (should be sigreturn address). Signal handler is running on host stack (ESP in 0xf7xxxx range).

## Root Cause Analysis (Emerging)

The ESP at crash is in the **host** address space (0xf7cXXXXX), not the guest stack. This means:

1. The signal handler is NOT running on our alternate signal stack — `sigaltstack` was either never set up correctly, or was overwritten.
2. The guest stack pointer was corrupted during the CRT startup sequence, switching to a host stack.
3. This likely happens in a function that does a `call` to a thunk — the thunk switches to the host (UNIX) stack for the syscall, but the return goes to a corrupted ESP.

The most likely culprit: **`__getmainargs` or `_initterm`**. Both are resolved to our stubs. If our stub incorrectly manipulates ESP during the syscall dispatch, the PE would resume on the host stack instead of the guest stack.

Alternatively: **`__pei386_runtime_relocator`** may be overwriting memory that includes the sigaltstack registration.

## Comparison: hello_world_32 vs dll_loader_32

### hello_world_32 (works)
- Same CRT startup path: `__tmainCRTStartup → __pei386_runtime_relocator → ... → _main`
- Imports: kernel32.dll (GetStdHandle, WriteFile, ExitProcess) + msvcrt.dll
- .idata size: 0x4e0 bytes
- `__pei386_runtime_relocator` at VA 0x401a00
- Does NOT call LoadLibraryA at runtime
- **PASS**: Prints "Hello from 32-bit my_wine!"

### dll_loader_32 (crashes)
- Same CRT startup path: `__tmainCRTStartup → __pei386_runtime_relocator → ... → _main`
- Imports: kernel32.dll (19 functions including LoadLibraryA) + msvcrt.dll (24 functions)
- .idata size: 0x510 bytes
- `__pei386_runtime_relocator` at VA 0x401b80
- **CALLS LoadLibraryA** at runtime (IAT entry at 0x407124)
- **FAIL**: Crashes at `call *0x407124` with EIP=0x0

### Key differences
1. **More imports**: dll_loader has 19 kernel32 + 24 msvcrt = 43 total (vs ~8 for hello_world)
2. **Larger .idata**: 0x510 vs 0x4e0
3. **Calls LoadLibraryA**: This is the only sample that dynamically loads a DLL at runtime
4. **`__pei386_runtime_relocator` at different VA**: 0x401b80 (dll_loader) vs 0x401a00 (hello_world)

## TODO

- [ ] **NOP `__pei386_runtime_relocator` AND jump directly to `_main`**: Bypass both the relocator and the CRT startup entirely
- [ ] **Verify IAT at runtime**: Add a `call` to our own code that reads 0x407124 via syscall and prints the value, to see what's there
- [ ] **Compare IAT before vs. after `__pei386_runtime_relocator`**: Add instrumentation right after the NOP (if we NOP it) vs. right before `_main`
- [ ] **Check if `__getmainargs` returns host pointers**: If argv/envp point to host memory, PE code dereferencing them could corrupt things
- [ ] **Check `_initterm` behavior**: It calls constructor/destructor functions — do any of our stubs do something harmful?
- [ ] **Try the same with `hello_world_32.exe` but add LoadLibraryA call**: Confirm the issue is specific to the dll_loader binary, not our LoadLibraryA implementation
- [ ] **Test: disable all CRT startup functions**: NOP all imported calls in `__tmainCRTStartup` and jump to `_main`
- [ ] **Verify signal stack is intact**: Add a pre-crash check in the signal handler to confirm `sigaltstack` state
- [ ] **Check if `.idata` is writable at runtime**: The IAT is in `.idata` which has `MEM_WRITE` — is it also `MEM_READ`? Is it executable?
- [ ] **GDB: set watch on 0x407124**: Monitor when the IAT entry changes from 0x08055a80 to 0x0
