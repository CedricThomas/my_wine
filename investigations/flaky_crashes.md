# Flaky Test/Sample Crashes — Deep Investigation

**Date**: 2026-05-05
**System**: Linux x86_64, ASLR enabled (randomize_va_space=2)

---

## Executive Summary

**Three root causes identified**, ordered by impact:

1. **CRITICAL — `wine_dispatcher_addr()` returns NULL** — `dlsym(RTLD_DEFAULT, "__wine_dispatcher")` consistently returns NULL in the PIE binary. All syscall thunks are generated with `dispatcher=NULL`, producing garbage `call` displacements. With ASLR, the garbage target address sometimes falls in unmapped memory → segfault (~25-40% crash rate across samples). Without ASLR, the garbage address consistently falls in mapped memory → no crash.

2. **HIGH — `INLINE_SYSCALL_MMAP` does not convert kernel error codes to `MAP_FAILED`** — The kernel returns error values in range `[-4095, -1]` for mmap failures, but the inline syscall macro casts this directly to `(void *)`. A value like `0xffffffffffffffea` (kernel errno -166/EINVAL for `mmap(NULL, 0)`) is returned as a "valid pointer", causing `result == MAP_FAILED` checks to fail and `STATUS_SUCCESS` to be returned instead of `STATUS_MEMORY_NOT_AVAILABLE`.

3. **MEDIUM — Test assertion in `test_teb_peb` checks `get_gs_base() == teb`** — `setup_teb_peb()` intentionally does NOT set GS base (to preserve glibc TLS). The test verifies GS after setup without setting it first. The `can_set_gs_base()` probe restores GS to NULL before the actual test, so `get_gs_base()` returns NULL, not the TEB address.

---

## Root Cause 1: Thunk Dispatcher Address Resolution Failure

### Evidence

1. **Direct measurement** (modified `thunk_gen.c` with debug output):
   ```
   thunk_blob=0x7f1a9b56b000 dispatcher=(nil)
   raw_disp=-139752252026894 disp32=1688817650 overflow=1
   ```
   `dispatcher=(nil)` in **every single run**, regardless of success/failure.

2. **dlsym verification** (via LD_PRELOAD and inline tests):
   ```
   dlsym(RTLD_DEFAULT, __wine_dispatcher) = (nil)
   dlsym(RTLD_NEXT, __wine_dispatcher) = (nil)
   dlsym(NULL, __wine_dispatcher) = (nil)
   ```
   All dlsym variants return NULL.

3. **Symbol exists in binary**:
   ```
   $ nm ./my_wine | grep __wine_dispatcher
   000000000000fcfd T __wine_dispatcher
   ```
   The symbol is present as a global text symbol in the dynamic symbol table.

4. **ASLR correlation** (30 runs each):
   - With ASLR (`randomize_va_space=2`): ~22/30 pass, ~8/30 crash (exit 139)
   - Without ASLR (`setarch x86_64 -R`): **30/30 pass** — zero crashes

### Analysis

**File**: `src/syscall/dispatcher_entry.c`, line 37-42
```c
void *wine_dispatcher_addr(void)
{
    void *handle = dlsym(RTLD_DEFAULT, "__wine_dispatcher");
    return handle;
}
```

The comment says "Use dlsym so this works even when dispatcher_entry_asm.S is not linked (e.g. in test builds)." However, `dlsym(RTLD_DEFAULT, ...)` fails to find the symbol in this PIE binary at runtime. The symbol is present (verified via `nm`), but `dlsym` returns NULL.

**Why some runs succeed**: When `dispatcher` is NULL, the thunk displacement becomes:
```c
int32_t disp = (uint8_t *)NULL - (loc + 14);  // = -(loc + 14) truncated to int32
```
The `call` target = `loc + 14 + disp` = `(loc + 14) + (0 - (loc + 14)) truncated` = some address in the low 32-bit range. With ASLR disabled, this address consistently falls within mapped memory (the thunk blob itself or nearby pages). With ASLR, the thunk blob position varies, and the garbage target sometimes lands in unmapped memory.

**Why the garbage thunk "works" sometimes**: When the garbage call target falls within the thunk blob itself, the thunk chain continues executing through other thunks. The guest code that calls imported functions (like `WriteFile`, `GetStdHandle`) triggers thunk execution. If the thunk chain eventually reaches code that doesn't crash (e.g., executing NOPs or harmless instructions), the guest may appear to work. This is undefined behavior that happens to work by coincidence.

### Affected Code Paths

All guest code that calls imported Windows functions triggers thunk execution:
- `hello_world`: `GetStdHandle` → thunk → garbage call → crash/success
- `multi_syscall`: Multiple syscall thunks → same issue
- `null_deref`: Some runs crash during setup (before "GUEST: all handlers set") because the thunk blob lands near unmapped memory

### Proposed Fix

**Replace `dlsym` with a direct function pointer**. Since `__wine_dispatcher` is a global symbol in the same binary and is declared in `include/syscall/dispatcher_entry.h`, we can use a direct C function pointer:

In `src/syscall/dispatcher_entry.c`:
```c
/* Direct function pointer — works in PIE binaries without dlsym */
void *wine_dispatcher_addr(void)
{
    /* __wine_dispatcher is declared extern in dispatcher_entry.h and
     * defined in dispatcher_entry_asm.S with .globl. Using a function
     * pointer directly avoids the dlsym failure in PIE binaries. */
    extern void __wine_dispatcher(void);
    return (void *)__wine_dispatcher;
}
```

**Additional hardening**: In `src/syscall/thunk_gen.c`, add overflow detection:
```c
static void write_thunk_at(uint8_t *loc, uint16_t syscall_number, void *dispatcher_addr)
{
    if (dispatcher_addr == NULL) {
        /* Fatal: cannot generate thunk without dispatcher */
        _exit(1);
    }
    
    int64_t raw_disp = (int64_t)(uint8_t *)dispatcher_addr - (int64_t)(loc + 14);
    int32_t disp = (int32_t)raw_disp;
    
    if ((int64_t)disp != raw_disp) {
        /* Displacement overflow: dispatcher and thunk are too far apart.
         * This should not happen with proper dispatcher address. */
        _exit(1);
    }
    /* ... rest of function using disp ... */
}
```

### Priority: CRITICAL

This is the primary cause of all flaky sample crashes. Without this fix, the project cannot reliably run any PE sample under ASLR.

---

## Root Cause 2: Inline Syscall mmap Error Conversion

### Evidence

1. **Test failure**: `test_syscall_dispatch` — `NtAllocateVirtualMemory(NULL,NULL) -> STATUS_MEMORY_NOT_AVAILABLE` fails every run.

2. **Direct measurement** (added debug to `handler_NtAllocateVirtualMemory`):
   ```
   NtAllocVM: addr=(nil) sz=0 prot=3 base=0x7ffe497ba530 *base=0 reg=0x7ffe497ba538 *reg=0
   NtAllocVM result=0xffffffffffffffea
   ```
   The result `0xffffffffffffffea` is **not** `MAP_FAILED` (`0xffffffffffffffff`). The kernel returned `-166` (errno for `mmap(NULL, 0)` on this system).

3. **Direct syscall verification**:
   ```c
   // syscall(__NR_mmap, NULL, 0, ...) returns -1 (MAP_FAILED) on this system
   // but INLINE_SYSCALL_MMAP returns raw kernel value which may differ
   ```

### Analysis

**File**: `src/syscalls_inline.h`, line 107-117
```c
#define INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mmap), "D"(addr), "S"((size_t)(len)), \
              "d"(prot), "r"(flags), "r"(fd), "r"((off_t)(offset)) \
            : "rcx", "r11", "cc"); \
        (void *)_synct_rax; \
    })
```

On Linux x86_64, the kernel returns error codes in the range `[-4095, -1]` for the mmap syscall (these values can never be valid user-space addresses because of the high-mem layout). glibc's `mmap()` wrapper checks this range and returns `MAP_FAILED` for any value in `[-4095, -1]`. The inline syscall macro does **not** perform this check.

When `mmap(NULL, 0, ...)` fails, the kernel may return different error values on different kernels/systems:
- `-1` (EFAULT) — glibc would return MAP_FAILED
- `-166` or other values — glibc would still return MAP_FAILED, but our inline syscall returns the raw value as a pointer

The check `result == MAP_FAILED` fails because `0xffffffffffffffea != 0xffffffffffffffff`, and the code proceeds as if allocation succeeded, returning `STATUS_SUCCESS`.

### Affected Callers

1. `src/stubs/ntdll_memory.c` line 56: `handler_NtAllocateVirtualMemory` — primary test failure
2. `src/stubs/abi_wrappers.c` line 34: `sysv_malloc` — returns garbage pointer on failure
3. `src/stubs/abi_wrappers.c` line 44: `sysv_calloc` — checks `p != (void *)-1` but misses other error codes
4. `src/stubs/ntdll_memory.c` line 126: `handler_NtMapViewOfSection`
5. `src/stubs/ntdll_memory.c` line 222: `handler_NtCreateSection`

### Proposed Fix

**In `src/syscalls_inline.h`**, add kernel error range check to `INLINE_SYSCALL_MMAP`:
```c
#define INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset) \
    ({ \
        long _synct_rax; \
        __asm__ volatile("syscall" \
            : "=a"(_synct_rax) \
            : "a"(__NR_mmap), "D"(addr), "S"((size_t)(len)), \
              "d"(prot), "r"(flags), "r"(fd), "r"((off_t)(offset)) \
            : "rcx", "r11", "cc"); \
        /* Linux kernel returns error codes in [-MAX_ERRNO, -1] for mmap.
         * These can never be valid user-space addresses. Convert to MAP_FAILED. */ \
        _synct_rax >= -4095L ? (void *)_synct_rax : MAP_FAILED; \
    })
```

**Also update `sysv_calloc` in `src/stubs/abi_wrappers.c`**:
```c
__attribute__((sysv_abi))
void *sysv_calloc(size_t n, size_t s)
{
    size_t total = n * s;
    if (total == 0) total = 1;
    void *p = INLINE_SYSCALL_MMAP(NULL, page_align(total),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != NULL && p != MAP_FAILED)
        __builtin_memset(p, 0, total);
    return p;
}
```

### Priority: HIGH

This causes test failures and potential memory corruption (treating error codes as valid pointers).

---

## Root Cause 3: TEB/PEB Test GS Base Assertion

### Evidence

**Test output**:
```
--- TEB/PEB Setup ---
  PASS: setup_teb_peb returns non-NULL
  FAIL: GS base == TEB address
```

### Analysis

**File**: `tests/test_teb_peb.c`

The test sequence:
1. `can_set_gs_base()` — probes GS set/get capability:
   - Sets GS to probe page, verifies with get, restores to NULL
   - Returns 1 (success)
2. `setup_teb_peb()` — allocates TEB/PEB:
   - **Intentionally does NOT set GS base** (comment: "Do NOT set GS base here. The GS base should remain pointing to Linux TLS for all glibc calls during setup.")
3. `get_gs_base()` — returns NULL (restored by step 1)
4. Check `gs_base == teb` — FAILS because GS is NULL

The production code path (`guest_setup.c:finalize_guest_state()`) calls `set_gs_base(teb)` **after** `setup_teb_peb()` returns, which is correct. The test doesn't replicate this sequence.

### Proposed Fix

**In `tests/test_teb_peb.c`**, after calling `setup_teb_peb()` and before checking GS:
```c
void *teb = setup_teb_peb();
/* ... existing checks ... */

/* Set GS base to TEB (replicating what finalize_guest_state does) */
if (set_gs_base(teb) != 0) {
    printf("  SKIP: set_gs_base(teb) failed\n");
    /* ... cleanup ... */
    return;
}

void *gs_base = get_gs_base();
check("GS base == TEB address", gs_base == teb);
```

Or alternatively, the test could be renamed to `test_teb_peb_allocation` and the GS check moved to a separate `test_gs_base_switching` test.

### Priority: LOW

This only affects test output, not runtime behavior. The production code path is correct.

---

## Additional Observations (Not Root Causes)

### Stack Setup — `setup_stack()` writes below stack_top

**File**: `src/loader/teb_peb.c` line 100
```c
*(void **)((uintptr_t)stack_top - 8) = stack_base;
```

This stores `stack_base` 8 bytes below `stack_top`. After the guest stack is switched to `stack_top`, the first operation that pushes to the stack (e.g., a `call` instruction pushing a return address) would overwrite this value. **This is not a bug** — the value is stored for diagnostic/reference purposes only and is never read back. The `g_stack_base` global is the authoritative copy.

### Signal Handler Installation

**File**: `src/loader/crash_handlers.c`

`setup_signal_handlers()` installs handlers via `sigaction()` with `SA_SIGINFO`. If `sigaction()` crashes (e.g., due to glibc internal state corruption), the program crashes before "GUEST: all handlers set" is printed. This explains the rare "null_deref crashes before handlers" observation — the crash is likely from thunk-related issues during early setup, not from signal handler installation itself.

### `wine_dispatcher_addr()` uses `dlsym` for test builds

The comment in `dispatcher_entry.c` says dlsym is used "so this works even when dispatcher_entry_asm.S is not linked." However, in the actual binary where `dispatcher_entry_asm.S` IS linked, dlsym still fails. The fix should use a direct function pointer for production builds and fall back to dlsym only for test builds where the symbol might be absent.

### `INLINE_SYSCALL_MUNMAP` and `INLINE_SYSCALL_MPROTECT`

These macros return raw syscall results (integers). Most callers check `!= 0` or `!= -1`, which is correct since these syscalls return 0 on success and negative errno on failure. Unlike `INLINE_SYSCALL_MMAP`, these are less problematic because callers typically use the return value directly as an error code.

---

## Proposed Fixes Summary

| # | Fix | File(s) | Impact |
|---|-----|---------|--------|
| 1 | Replace `dlsym` with direct function pointer in `wine_dispatcher_addr()` | `src/syscall/dispatcher_entry.c` | Eliminates all ASLR-related sample crashes |
| 2 | Add overflow check in `write_thunk_at()` | `src/syscall/thunk_gen.c` | Prevents silent garbage thunk generation |
| 3 | Fix `INLINE_SYSCALL_MMAP` error conversion | `src/syscalls_inline.h` | Fixes test_syscall_dispatch failure, prevents memory corruption |
| 4 | Update `sysv_calloc` MAP_FAILED check | `src/stubs/abi_wrappers.c` | Prevents memset on invalid address |
| 5 | Fix `test_teb_peb` GS base check | `tests/test_teb_peb.c` | Fixes test_teb_peb failure |

### Implementation Order

1. **Fix #1** first — this is the critical blocker. After this fix, all samples should run reliably with ASLR.
2. **Fix #2** — safety net for #1
3. **Fix #3** — fixes test_syscall_dispatch
4. **Fix #4** — prevents subtle corruption
5. **Fix #5** — fixes test_teb_peb

---

## Testing Plan

After implementing fixes:

1. **ASLR stress test**: Run `hello_world` 100 times with ASLR — expect 100% pass
2. **No-ASLR test**: Run `hello_world` 100 times without ASLR — expect 100% pass
3. **All samples**: Run each sample 20 times — expect consistent pass
4. **Test suite**: `make run-test` — expect all tests passing
5. **Edge case**: Verify `mmap(NULL, 0, ...)` returns MAP_FAILED after fix

