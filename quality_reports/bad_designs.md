# Bad Designs

> From quality_report.log, generated 2026-05-06

---

## Findings

[FINDING E1] — Thread-unsafe global state
- **Severity**: MEDIUM
- **Files**: src/loader/import_resolve.c (g_dll_base_next), src/msvcrt/ntdll_priv.h (handle_table, sections, events, mutexes, threads), src/msvcrt/crt_globals.c (g_crt_ctx, __imp___initenv_stub), src/msvcrt/kernel32_priv.h (g_last_error)
- **Description**: Multiple globals are written without synchronization:
  - g_dll_base_next is modified in load_dll() — if two threads load DLLs simultaneously, they'll allocate overlapping addresses
  - g_crt_ctx is written during patch_crt_refptrs() and read in __getmainargs() — data race if another thread accesses it
  - g_last_error is __thread in kernel32_priv.h (good), but the handle_table, sections, events, mutexes are all process-global

- **Suggested Fix**: For single-process model: document the limitation. For future multi-process support: add spinlocks or use atomic operations for g_dll_base_next. Protect handle_table with a mutex.

---

[FINDING E2] — Silent failures on critical resource allocation
- **Severity**: MEDIUM
- **Files**: src/loader/crash_handlers.c:108-114
- **Description**: setup_signal_handlers() silently continues if mmap for the signal stack fails:
  ```c
  if (sigstack_mem == MAP_FAILED) {
      /* Fallback: handlers will run on the current stack */
  }
  ```
  This means crash handlers may run on a potentially-corrupted guest stack, defeating the entire purpose of the alternate signal stack.

- **Suggested Fix**: At minimum, emit a warning via direct syscall. Consider making this a hard error if the alternate stack cannot be allocated.

---

[FINDING E3] — refptr_patch_arg is a single global struct
- **Severity**: LOW
- **Files**: src/msvcrt/crt_refptrs.c:42-46
- **Description**: refptr_patch_arg is a static global struct used to pass data to the with_mprotect_rw callback. This means apply_refptr_patch() is not reentrant — if two threads call it simultaneously, the second call will overwrite the first's data.

- **Suggested Fix**: For single-threaded use this is acceptable, but consider passing a per-call allocation instead. The callback already takes void *arg.

---

[FINDING E4] — find_dll_path uses environ directly (not syscall-safe)
- **Severity**: LOW
- **Files**: src/loader/import_resolve.c:455-468
- **Description**: find_dll_path() iterates over environ to find WINE_DLL_PATH. This is safe before GS switch (when glibc is still functional), but if called after GS→TEB switch (from a WINE_STUB context), environ may not be valid.

  The function is documented as "syscall-safe" but environ access is not async-signal-safe or GS-switch-safe.

- **Suggested Fix**: Accept the environ pointer as a parameter, or cache WINE_DLL_PATH before the GS switch.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Document thread-safety limitations and add atomic guard for g_dll_base_next
- **Related Finding(s)**: E1
- **Impact**: Prevents silent data races in the most critical shared state; documents limitations for future maintainers
- **Files to create**: (none)
- **Files to modify**:
  - src/loader/import_resolve.c — change g_dll_base_next to use __atomic builtins or mark volatile
  - src/msvcrt/ntdll_priv.h — add thread-safety documentation comments
  - src/msvcrt/crt_globals.c — add thread-safety documentation comments
  - src/msvcrt/kernel32_priv.h — verify g_last_error is __thread
- **Steps**:
  1. Add `// SINGLE-THREAD ONLY: not safe for concurrent access` comments to ntdll_priv.h globals
  2. Change g_dll_base_next allocation from bare assignment to `__atomic_add_fetch(&g_dll_base_next, size, __ATOMIC_SEQ_CST)`
  3. Add documentation comment to g_crt_ctx in crt_globals.c
  4. Verify g_last_error is declared `__thread` in kernel32_priv.h
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; atomic operations used for g_dll_base_next; documentation is clear

---

### Task 2: Emit warning on signal stack allocation failure
- **Related Finding(s)**: E2
- **Impact**: Makes silent failures visible; at minimum alerts the user that crash handling is degraded
- **Files to create**: (none)
- **Files to modify**:
  - src/loader/crash_handlers.c — replace silent fallback with a warning emitted via direct syscall
- **Steps**:
  1. In setup_signal_handlers(), after the MAP_FAILED check, emit a warning using a direct write() syscall to stderr
  2. Consider adding a static flag `g_alt_stack_available` that downstream crash handlers can check
  3. If g_alt_stack_available is false, make crash_handlers.c log "WARNING: running on guest stack — crash may be unrecoverable"
  4. Compile and verify behavior is unchanged on success path
- **Dependencies**: none
- **Verification**: Build succeeds; warning is emitted when mmap fails; normal operation unchanged

---

### Task 3: Make refptr_patch_arg per-call instead of global
- **Related Finding(s)**: E3
- **Impact**: Makes apply_refptr_patch() reentrant in theory; cleaner design even for single-thread use
- **Files to create**: (none)
- **Files to modify**:
  - src/msvcrt/crt_refptrs.c — replace static global refptr_patch_arg with a stack-allocated struct passed via the callback's void *arg
- **Steps**:
  1. Change `static refptr_patch_arg` to a local variable in apply_refptr_patch()
  2. Ensure the with_mprotect_rw callback receives the address of the local struct
  3. Verify the callback doesn't store the pointer beyond the call (it shouldn't)
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; no global state used for patching; behavior unchanged

---

### Task 4: Cache WINE_DLL_PATH before GS switch
- **Related Finding(s)**: E4
- **Impact**: Eliminates unsafe environ access after GS→TEB switch; makes find_dll_path truly syscall-safe
- **Files to create**: (none)
- **Files to modify**:
  - src/loader/import_resolve.c — cache WINE_DLL_PATH early; modify find_dll_path() to use cached value
  - src/main.c or src/loader/loader_priv.h — add global for cached DLL path
- **Steps**:
  1. In main() (before GS switch), extract WINE_DLL_PATH from environ and store in a global (e.g., `g_wine_dll_path`)
  2. Modify find_dll_path() to use `g_wine_dll_path` instead of scanning environ
  3. Update the function's documentation to remove the "syscall-safe" claim about environ
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; find_dll_path() no longer accesses environ; DLL resolution works correctly
