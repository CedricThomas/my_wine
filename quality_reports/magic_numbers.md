# Magic Numbers

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING C1] — Page size 4096/4095 hardcoded in multiple files
- **Status**: ❌ OPEN
- **Severity**: MEDIUM
- **Files still affected**:
  - src/stubs/msvcrt.c:34 — `_cmdline_storage[4096]`
  - src/stubs/msvcrt.c:481 — `len < 4095`
  - src/stubs/msvcrt.c:607-608, 623 — bare `4095`/`4096` in mprotect calls
  - src/stubs/msvcrt.c:641-642, 653 — bare `4095`/`4096` in mprotect calls
  - src/stubs/kernel32.c:321 — `mbi->RegionSize = 4096`
  - src/msvcrt/crt_stdio.c:34 — `len < 4095`
  - src/msvcrt/crt_globals.c:27 — `_cmdline_storage[4096]`
  - src/msvcrt/msvcrt_priv.h:32 — `extern char _cmdline_storage[4096]`
  - include/msvcrt.h:21 — `extern char _cmdline_storage[4096]`
- **Previously fixed files**:
  - src/main.c — was using bare `4095`, now uses `PAGE_MASK`
  - src/loader/teb_peb.c — was using bare `4096`/`4095`, now uses `PAGE_SIZE`/`PAGE_MASK`
  - src/loader/image_mapper.c — was using bare `4095`, now uses `PAGE_MASK`
- **Description**: While `PAGE_SIZE=4096` and `PAGE_MASK=4095` are defined in include/common.h, the literal values still appear directly in 9 locations across stubs/ and msvcrt/ directories.

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
- **Status**: ⚠️ PARTIALLY FIXED
- **Severity**: MEDIUM
- **Files**: include/nt_constants.h:74-75 (constants defined ✅), src/stubs/ntdll.c:292-293 (still uses local `uint64_t GENERIC_READ = 0x80000000`), src/msvcrt/ntdll_io.c:71-72 (uses the named constants ✅)
- **Description**: `GENERIC_READ` and `GENERIC_WRITE` are now defined in `include/nt_constants.h`. However, `ntdll.c` still declares local variables `uint64_t GENERIC_READ = 0x80000000` and `uint64_t GENERIC_WRITE = 0x40000000` instead of using the header constants.

- **Suggested Fix**: In src/stubs/ntdll.c, replace the local variable declarations with `#include "include/nt_constants.h"` and use the defined constants directly.

---

### [FINDING C5] — Error codes hardcoded in kernel32_misc.c
- **Status**: ❌ OPEN
- **Severity**: LOW
- **Files**: src/msvcrt/kernel32_misc.c (need to check specific lines for bare 87, 1, 122)
- **Description**: Magic values: 87 (ERROR_INVALID_PARAMETER), 1 (ERROR_FALSE), 122 (ERROR_INSUFFICIENT_BUFFER) are used as bare integers.

- **Suggested Fix**: Define in include/nt_constants.h:
  ```c
  #define ERROR_INVALID_PARAMETER      87
  #define ERROR_SUCCESS                0
  #define ERROR_INSUFFICIENT_BUFFER   122
  #define ERROR_ACCESS_DENIED          5
  #define ERROR_FALSE                  1
  ```

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
- **Status**: ❌ OPEN
- **Severity**: LOW
- **Files**: src/stubs/msvcrt.c:101
- **Description**: `WINE_FILE_SIZE = 48` is used to define the size of wine_FILE struct. This is derived from the MSVCRT FILE layout. If the struct definition changes, the constant must be updated manually.

- **Suggested Fix**: Use `sizeof(wine_FILE)` instead of `WINE_FILE_SIZE` where possible, or add a compile-time assert:
  ```c
  _Static_assert(sizeof(wine_FILE) == 48, "wine_FILE size mismatch");
  ```

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Replace bare 4096/4095 with PAGE_SIZE/PAGE_MASK in stubs/ and msvcrt/
- **Related Finding(s)**: C1
- **Impact**: Consistent use of named constants across the entire codebase; maintainability
- **Files to modify**:
  - src/stubs/msvcrt.c — replace all bare 4096/4095 (lines 34, 481, 607-608, 623, 641-642, 653)
  - src/stubs/kernel32.c — replace bare 4096 (line 321)
  - src/msvcrt/crt_stdio.c — replace bare 4095 (line 34)
  - src/msvcrt/crt_globals.c — consider `_cmdline_storage[PAGE_SIZE]` or leave as buffer size
  - src/msvcrt/msvcrt_priv.h, include/msvcrt.h — consider `_cmdline_storage[PAGE_SIZE]`
- **Steps**:
  1. Add `#include "include/common.h"` to files that don't already include it
  2. Replace bare 4096 with PAGE_SIZE, bare 4095 with PAGE_MASK
  3. For `_cmdline_storage`, decide whether PAGE_SIZE is the right semantic or define `CMDLINE_BUF_SIZE`
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; grep for bare 4096/4095 returns no false positives

---

### Task 2: Fix GENERIC_READ/WRITE usage in ntdll.c
- **Related Finding(s)**: C4
- **Impact**: Removes remaining hardcoded magic values; uses central constant definitions
- **Files to modify**:
  - src/stubs/ntdll.c — replace local `uint64_t GENERIC_READ = 0x80000000` with the constant from nt_constants.h
- **Steps**:
  1. Add `#include "include/nt_constants.h"` to ntdll.c
  2. Remove local variable declarations for GENERIC_READ/GENERIC_WRITE
  3. Use the constants directly
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; no local GENERIC_READ/WRITE declarations remain

---

### Task 3: Define error code constants
- **Related Finding(s)**: C5
- **Impact**: Replaces bare integers with named constants
- **Files to modify**:
  - include/nt_constants.h — add ERROR_INVALID_PARAMETER, ERROR_INSUFFICIENT_BUFFER, etc.
  - src/msvcrt/kernel32_misc.c — replace bare values with named constants
- **Steps**:
  1. Add error code #defines to nt_constants.h
  2. Find all bare error code returns in kernel32_misc.c
  3. Replace with named constants
  4. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; no bare error code integers remain

---

### Task 4: Add _Static_assert for WINE_FILE_SIZE
- **Related Finding(s)**: C8
- **Impact**: Compile-time check that struct size matches the hardcoded constant
- **Files to modify**:
  - src/stubs/msvcrt.c — add `_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, ...)`
- **Steps**:
  1. Add `_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, "wine_FILE size mismatch")` after the struct definition
  2. Compile and verify the assert passes
- **Dependencies**: none
- **Verification**: Build succeeds; if struct layout changes, build fails with a clear message
