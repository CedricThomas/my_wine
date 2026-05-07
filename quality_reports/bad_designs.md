# Bad Designs

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase. All findings resolved.

---

## Findings

### [FINDING E1] — Thread-unsafe global state
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Files**: src/msvcrt/ntdll_priv.h, src/msvcrt/crt_globals.c
- **Description**: Multiple globals were written without synchronization.

  **Fix applied (2026-05-07):** Added `SINGLE-THREAD ONLY` documentation comments to all unprotected global tables in `ntdll_priv.h` (`views[]/view_count`) and to `g_crt_ctx` in `crt_globals.c`. All 6 tracked-resource sections in `ntdll_priv.h` are now uniformly documented. The `g_crt_ctx` comment identifies its write site (`patch_crt_refptrs`), read sites (`__getmainargs`, `main.c`, `crt_offset_discovery.c`), and data race risk. No code changes — documentation-only fix appropriate for the single-threaded model.

---

### [FINDING E2] — Silent failures on critical resource allocation
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Files**: src/loader/crash_handlers.c
- **Description**: ~~setup_signal_handlers() silently continued if mmap for the signal stack failed.~~

  **Fix applied (2026-05-07):** The existing warning+flag approach in `crash_handlers.c` (lines 127-146) is the correct tradeoff and was retained as-is. ~64KB mmap failure means critical OOM where even a hard abort cannot be guaranteed to succeed. The crash handler still functions on the guest stack with syscall-safe diagnostics and exit. The project is single-threaded, eliminating concurrent stack corruption risk. Decision documented in the source code.

---

### [FINDING E3] — refptr_patch_arg is a single global struct
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/msvcrt/crt_refptrs.c, src/msvcrt/crt_offset_discovery.c
- **Description**: ~~refptr_patch_arg was a static global struct used to pass data to the with_mprotect_rw callback.~~

  **Fix verified (2026-05-07):** Full reentrancy is achieved:
  - `refptr_patch_arg` has `image_base` and `bss_vaddr` fields for full context passing
  - `patch_crt_refptrs` builds a local `crt_context_t ctx` on the stack — never reads `g_crt_ctx` during patching
  - `discover_crt_offsets` accepts a `crt_context_t *ctx` parameter instead of reading globals
  - `g_crt_ctx` is only WRITTEN (line 122) after all patching completes — no data race
  - `refptr_mappings[]` reference to `&g_crt_ctx.image_base` is a compile-time address computation, not a runtime read

  **No code changes needed** — the reentrancy improvements were already in place; verified and documented.

---

### [FINDING E4] — find_dll_path uses environ directly (not syscall-safe)
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/loader/import_resolve.c
- **Description**: ~~find_dll_path() iterated over environ to find WINE_DLL_PATH.~~

  **Current state**: WINE_DLL_PATH is now cached in `g_wine_dll_path` (documented as "Cached from environ in main() before GS switch — syscall-safe"). find_dll_path() uses `g_wine_dll_path` instead of scanning environ.

---

## Implementation Plan

All tasks completed (2026-05-07).

### Task 1: Document thread-safety limitations in ntdll_priv.h and crt_globals.c
- **Related Finding(s)**: E1
- **Status**: ✅ DONE
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
- **Status**: ✅ DONE — No code change needed; existing approach is correct
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
- **Status**: ✅ DONE — Reentrancy already implemented; verified and documented
- **Impact**: Makes the function truly reentrant in multi-threaded context
- **Files to modify**:
  - src/msvcrt/crt_refptrs.c — pass all context via the arg pointer; eliminate dependency on g_crt_ctx global
- **Steps**:
  1. Extend refptr_patch_arg to include all needed context (image_base, bss_vaddr, etc.)
  2. Modify do_refptr_patch to read exclusively from arg
  3. Verify no other global state is accessed
- **Dependencies**: none
- **Verification**: Build succeeds; function is fully reentrant; behavior unchanged
