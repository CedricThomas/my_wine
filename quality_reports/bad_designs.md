# Bad Designs

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING E1] — Thread-unsafe global state
- **Status**: ⚠️ PARTIALLY FIXED
- **Severity**: MEDIUM
- **Files**: src/loader/import_resolve.c, src/msvcrt/ntdll_priv.h, src/msvcrt/crt_globals.c, src/msvcrt/kernel32_priv.h
- **Description**: Multiple globals are written without synchronization:
  - **g_dll_base_next**: ⚠️ Now uses `__atomic_compare_exchange_n` and `__atomic_add_fetch` with `__ATOMIC_SEQ_CST` — atomic, but no spinlock for the full allocation loop
  - **g_crt_ctx**: ❌ Still a plain global; written during `patch_crt_refptrs()` and read in `__getmainargs()` — data race if another thread accesses it
  - **g_last_error**: ✅ Already declared `__thread` in kernel32_priv.h
  - **handle_table, sections, events, mutexes**: ❌ Still process-global with no synchronization

- **Suggested Fix**: For single-process model: add `// SINGLE-THREAD ONLY` documentation to ntdll_priv.h globals. For future multi-process support: add spinlocks or use atomic operations for g_dll_base_next. Protect handle_table with a mutex.

---

### [FINDING E2] — Silent failures on critical resource allocation
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Files**: src/loader/crash_handlers.c
- **Description**: ~~setup_signal_handlers() silently continued if mmap for the signal stack failed.~~

  **Current state**: Now emits a warning via `INLINE_SYSCALL_WRITE(2, warn_msg, ...)` to stderr, and sets `g_alt_stack_available = 0` flag for downstream crash handlers.

- **Remaining improvement**: Consider making this a hard error if the alternate stack cannot be allocated.

---

### [FINDING E3] — refptr_patch_arg is a single global struct
- **Status**: ⚠️ PARTIALLY FIXED
- **Severity**: LOW
- **Files**: src/msvcrt/crt_refptrs.c
- **Description**: ~~refptr_patch_arg was a static global struct used to pass data to the with_mprotect_rw callback.~~

  **Current state**: The `refptr_patch_arg` struct is now defined (lines 43-46) and a local instance is created on the stack (line 69: `struct refptr_patch_arg arg = { ... }`). However, the struct type definition itself remains in the file, and the callback still uses `void *arg` pattern. The function is still not fully reentrant in a multi-threaded context due to shared globals it references.

- **Remaining improvement**: The current stack-local approach is an improvement but the function still depends on global state (g_crt_ctx). Full reentrancy would require passing all context via the arg pointer.

---

### [FINDING E4] — find_dll_path uses environ directly (not syscall-safe)
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/loader/import_resolve.c
- **Description**: ~~find_dll_path() iterated over environ to find WINE_DLL_PATH.~~

  **Current state**: WINE_DLL_PATH is now cached in `g_wine_dll_path` (documented as "Cached from environ in main() before GS switch — syscall-safe"). find_dll_path() uses `g_wine_dll_path` instead of scanning environ.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Document thread-safety limitations in ntdll_priv.h and crt_globals.c
- **Related Finding(s)**: E1
- **Impact**: Documents limitations for future maintainers; prevents silent data races
- **Files to modify**:
  - src/msvcrt/ntdll_priv.h — add `// SINGLE-THREAD ONLY: not safe for concurrent access` comments to all extern globals
  - src/msvcrt/crt_globals.c — add thread-safety documentation comments to g_crt_ctx
- **Steps**:
  1. Add documentation comments to ntdll_priv.h globals
  2. Add documentation comment to g_crt_ctx in crt_globals.c
  3. No code changes needed for single-thread model
- **Dependencies**: none
- **Verification**: Documentation is clear; no behavioral change

---

### Task 2: Consider hard error for signal stack allocation failure
- **Related Finding(s)**: E2
- **Impact**: Prevents running with degraded crash handling
- **Files to modify**:
  - src/loader/crash_handlers.c — consider making MAP_FAILED a hard error (abort) instead of warning + fallback
- **Steps**:
  1. Evaluate whether alternate stack failure should be fatal
  2. If yes: change warning to `INLINE_SYSCALL_EXIT(EXIT_SIGSEGV)` or similar
  3. If no: current warning + g_alt_stack_available flag is sufficient
- **Dependencies**: none
- **Verification**: Behavior is consistent with project goals

---

### Task 3: Full reentrancy for apply_refptr_patch()
- **Related Finding(s)**: E3
- **Impact**: Makes the function truly reentrant in multi-threaded context
- **Files to modify**:
  - src/msvcrt/crt_refptrs.c — pass all context via the arg pointer; eliminate dependency on g_crt_ctx global
- **Steps**:
  1. Extend refptr_patch_arg to include all needed context (image_base, bss_vaddr, etc.)
  2. Modify do_refptr_patch to read exclusively from arg
  3. Verify no other global state is accessed
- **Dependencies**: none
- **Verification**: Build succeeds; function is fully reentrant; behavior unchanged
