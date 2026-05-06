# Magic Numbers

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING C1] — Page size 4096/4095 hardcoded in multiple files
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Description**: ~~Bare 4096/4095 in stubs/ and msvcrt/.~~ All replaced with `PAGE_SIZE`/`PAGE_MASK` from `include/common.h`.

  **Files fixed**:
  - src/stubs/msvcrt.c — `PAGE_SIZE`, `PAGE_MASK` (include added)
  - src/stubs/kernel32.c — `PAGE_SIZE` (include added)
  - src/msvcrt/crt_stdio.c — `PAGE_MASK` (include added)
  - src/msvcrt/crt_globals.c — `PAGE_SIZE` (include added)
  - src/msvcrt/msvcrt_priv.h — `PAGE_SIZE` (include added)
  - include/msvcrt.h — `PAGE_SIZE` (include added)

- **Suggested Fix**: Replace all bare 4096/4095 with PAGE_SIZE/PAGE_MASK. Add `#include "include/common.h"` where missing. For `_cmdline_storage[4096]`, this is a buffer size (not strictly page alignment) — decide whether `PAGE_SIZE` is semantically correct or if a separate `CMDLINE_BUF_SIZE` is more appropriate.

---

### [FINDING C2] — CRT BSS offset 0x30 hardcoded
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Files**: src/msvcrt/crt_refptrs.c
- **Description**: ~~The 'initialized' flag at offset 0x30 from .bss start was hardcoded.~~

  **Current state**: Now defined as `#define CRT_BSS_INITIALIZED 0x30` in crt_refptrs.c. A separate `CRT_BSS_INITENV` constant is also defined for the __initenv offset.

- **Remaining risk**: Still a mingw-w64-specific offset. If the CRT layout changes between mingw-w64 versions, this silently does the wrong thing. Consider COFF symbol lookup for `__native_startup_state`.

---

### [FINDING C3] — DLL base allocator start 0x60000000 (1.5GB)
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/loader/import_resolve.c
- **Description**: ~~The value was hardcoded without a named constant.~~

  **Current state**: Now defined as `#define DLL_ALLOC_BASE 0x60000000` with a descriptive comment in import_resolve.c.

---

### [FINDING C4] — Windows access flags 0x80000000 / 0x40000000
- **Status**: ✅ FIXED
- **Severity**: MEDIUM
- **Files**: include/nt_constants.h:74-75 (constants defined ✅), src/stubs/ntdll.c (fixed ✅), src/msvcrt/ntdll_io.c:71-72 (uses the named constants ✅)
- **Description**: ~~ntdll.c used local variable declarations instead of header constants.~~ Now uses `#include "include/nt_constants.h"` and the defined `GENERIC_READ`/`GENERIC_WRITE` constants.

---

### [FINDING C5] — Error codes hardcoded in kernel32_misc.c
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: include/nt_constants.h (ERROR_* constants defined ✅), src/msvcrt/kernel32_misc.c (uses named constants ✅)
- **Description**: ~~Bare error code integers.~~ All replaced with named constants (`ERROR_INVALID_PARAMETER`, `ERROR_ACCESS_DENIED`, `ERROR_INSUFFICIENT_BUFFER`) from `include/nt_constants.h`.

---

### [FINDING C6] — Exit codes 139 (SIGSEGV) and 134 (SIGABRT) hardcoded
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/loader/crash_handlers.c, src/msvcrt/crt_stdlib.c
- **Description**: ~~These were bare integers.~~

  **Current state**: Now using `EXIT_SIGSEGV` (crash_handlers.c:112) and `EXIT_SIGABRT` (crt_stdlib.c:104) named constants.

---

### [FINDING C7] — 0xC0000005 ACCESS_VIOLATION hardcoded
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/loader/crash_handlers.c
- **Description**: ~~The SEH crash handler hardcoded 0xC0000005 as exit code.~~

  **Current state**: Now using `EXIT_SIGSEGV` named constant. The constant `STATUS_ACCESS_VIOLATION` is already defined in include/nt_constants.h.

---

### [FINDING C8] — WINE_FILE_SIZE 48 hardcoded as struct size
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/msvcrt/msvcrt_priv.h:69
- **Description**: ~~No compile-time check.~~ Now has `_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, "wine_FILE size mismatch")` in msvcrt_priv.h.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Replace bare 4096/4095 with PAGE_SIZE/PAGE_MASK in stubs/ and msvcrt/
- **Related Finding(s)**: C1
- **Status**: ✅ COMPLETE
- **Impact**: Consistent use of named constants across the entire codebase; maintainability
- **Files modified**:
  - src/stubs/msvcrt.c — replaced all bare 4096/4095
  - src/stubs/kernel32.c — replaced bare 4096
  - src/msvcrt/crt_stdio.c — replaced bare 4095
  - src/msvcrt/crt_globals.c — replaced bare 4096
  - src/msvcrt/msvcrt_priv.h — replaced bare 4096
  - include/msvcrt.h — replaced bare 4096
- **Steps**:
  1. Add `#include "include/common.h"` to files that don't already include it ✅
  2. Replace bare 4096 with PAGE_SIZE, bare 4095 with PAGE_MASK ✅
  3. For `_cmdline_storage`, used `PAGE_SIZE` (semantically a page-sized buffer) ✅
  4. Compile and verify ✅
- **Dependencies**: none
- **Verification**: Build succeeds; grep for bare 4096/4095 returns no false positives ✅

---

### Task 2: Fix GENERIC_READ/WRITE usage in ntdll.c
- **Related Finding(s)**: C4
- **Status**: ✅ COMPLETE
- **Impact**: Removes remaining hardcoded magic values; uses central constant definitions
- **Files modified**:
  - src/stubs/ntdll.c — replaced local variable declarations with header constants
- **Steps**:
  1. Add `#include "include/nt_constants.h"` to ntdll.c ✅
  2. Remove local variable declarations for GENERIC_READ/GENERIC_WRITE ✅
  3. Use the constants directly ✅
  4. Compile and verify ✅
- **Dependencies**: none
- **Verification**: Build succeeds; no local GENERIC_READ/WRITE declarations remain ✅

---

### Task 3: Define error code constants
- **Related Finding(s)**: C5
- **Status**: ✅ COMPLETE (was already fixed)
- **Impact**: Replaces bare integers with named constants
- **Files**:
  - include/nt_constants.h — ERROR_* constants defined ✅
  - src/msvcrt/kernel32_misc.c — uses named constants ✅
- **Verification**: No bare error code integers ✅

---

### Task 4: Add _Static_assert for WINE_FILE_SIZE
- **Related Finding(s)**: C8
- **Status**: ✅ COMPLETE
- **Impact**: Compile-time check that struct size matches the hardcoded constant
- **Files modified**:
  - src/msvcrt/msvcrt_priv.h — _Static_assert already present ✅
  - src/stubs/msvcrt.c — added _Static_assert ✅
- **Verification**: Build succeeds; if struct layout changes, build fails with a clear message ✅
