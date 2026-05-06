# Magic Numbers

> From quality_report.log, generated 2026-05-06

---

## Findings

[FINDING C1] — Page size 4096/4095 hardcoded in multiple files
- **Severity**: MEDIUM
- **Files**:
  src/main.c:69-70 (4095)
  src/loader/guest_setup.c:181 (0xFFFFF)
  src/loader/teb_peb.c:40,61,135,143-144 (4096, 4095)
  src/loader/image_mapper.c:181 (4095)
- **Description**: While PAGE_SIZE=4096 and PAGE_MASK=4095 are defined in include/common.h, the literal values 4095 and 4096 appear directly in 4+ source files. src/main.c uses bare 4095. src/loader/teb_peb.c uses bare 4096/4095. src/loader/image_mapper.c uses bare 4095. guest_setup.c uses 0xFFFFF.

  These should all use PAGE_SIZE/PAGE_MASK/PAGE_ALIGN_MASK from common.h for consistency and maintainability.

- **Suggested Fix**: Replace all bare 4096/4095/0xFFFFF with PAGE_SIZE/PAGE_MASK. Add #include "include/common.h" where missing.

---

[FINDING C2] — CRT BSS offset 0x30 hardcoded
- **Severity**: MEDIUM
- **Files**: src/msvcrt/crt_refptrs.c:112-114
- **Description**: The 'initialized' flag at offset 0x30 from .bss start is hardcoded:
  ```c
  *(uint32_t *)((char *)image_base + g_crt_ctx.bss_vaddr + 0x30) = 1;
  ```
  This is a mingw-w64-specific offset. If the CRT layout changes between mingw-w64 versions, this silently does the wrong thing.

- **Suggested Fix**: Define as named constant: `#define CRT_BSS_INITIALIZED 0x30` Or discover it via COFF symbol lookup for __native_startup_state.

---

[FINDING C3] — DLL base allocator start 0x60000000 (1.5GB)
- **Severity**: LOW
- **Files**: src/loader/import_resolve.c:27
- **Description**: g_dll_base_next = 0x60000000 is hardcoded. The comment explains this is to keep DLLs below 4GB to avoid GCC ms_abi truncation. However the value itself is not defined as a constant and is not easily configurable.

- **Suggested Fix**: Define as: `#define DLL_ALLOC_BASE 0x60000000`

---

[FINDING C4] — Windows access flags 0x80000000 / 0x40000000
- **Severity**: MEDIUM
- **Files**: src/msvcrt/ntdll_io.c:70-71
- **Description**: GENERIC_READ (0x80000000) and GENERIC_WRITE (0x40000000) are defined inline in the handler function. These are Windows API constants that should be defined in a shared header.

- **Suggested Fix**: Add to include/nt_constants.h:
  ```c
  #define GENERIC_READ  0x80000000
  #define GENERIC_WRITE 0x40000000
  ```

---

[FINDING C5] — Error codes hardcoded in kernel32_misc.c
- **Severity**: LOW
- **Files**: src/msvcrt/kernel32_misc.c:66,78,118,134,155
- **Description**: Magic values: 87 (ERROR_INVALID_PARAMETER), 1 (ERROR_FALSE), 122 (ERROR_INSUFFICIENT_BUFFER) are used as bare integers.

- **Suggested Fix**: Define in include/nt_constants.h:
  ```c
  #define ERROR_INVALID_PARAMETER      87
  #define ERROR_SUCCESS                0
  #define ERROR_INSUFFICIENT_BUFFER   122
  #define ERROR_ACCESS_DENIED          5
  ```

---

[FINDING C6] — Exit codes 139 (SIGSEGV) and 134 (SIGABRT) hardcoded
- **Severity**: LOW
- **Files**: src/loader/crash_handlers.c:97, src/msvcrt/crt_stdlib.c:102
- **Description**: crash_handlers.c uses 139 for SIGSEGV exit code. crt_stdlib.c uses 134 for SIGABRT exit code. These are conventional (128+signal) but bare.

- **Suggested Fix**: Define: `#define EXIT_SIGSEGV 139` `#define EXIT_SIGABRT 134`

---

[FINDING C7] — 0xC0000005 ACCESS_VIOLATION hardcoded
- **Severity**: LOW
- **Files**: src/loader/crash_handlers.c:39
- **Description**: The SEH crash handler hardcodes 0xC0000005 as exit code. Should be a named NTSTATUS constant.

- **Suggested Fix**: Use STATUS_ACCESS_VIOLATION from include/nt_constants.h (already defined as 0xC0000005 there — just use the name).

---

[FINDING C8] — WINE_FILE_SIZE 48 hardcoded as struct size
- **Severity**: LOW
- **Files**: src/msvcrt/msvcrt_priv.h:52
- **Description**: WINE_FILE_SIZE = 48 is used to define the size of wine_FILE struct. This is derived from the MSVCRT FILE layout. If the struct definition changes, the constant must be updated manually.

- **Suggested Fix**: Use sizeof(wine_FILE) instead of WINE_FILE_SIZE where possible, or add a compile-time assert: `_Static_assert(sizeof(wine_FILE) == 48, ...)`.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Replace bare 4096/4095/0xFFFFF with PAGE_SIZE/PAGE_MASK
- **Related Finding(s)**: C1
- **Impact**: Eliminates inconsistent page-size literals across 4 files; ensures all page math uses the defined constants from common.h; recommended as #2 priority fix in the quality report
- **Files to create**: (none)
- **Files to modify**:
  - src/main.c — replace bare 4095 with PAGE_MASK, add `#include "common.h"`
  - src/loader/guest_setup.c — replace 0xFFFFF with PAGE_ALIGN_MASK or equivalent, add `#include "common.h"`
  - src/loader/teb_peb.c — replace bare 4096/4095 with PAGE_SIZE/PAGE_MASK
  - src/loader/image_mapper.c — replace bare 4095 with PAGE_MASK
- **Steps**:
  1. Verify PAGE_SIZE, PAGE_MASK, and PAGE_ALIGN_MASK definitions in include/common.h
  2. Grep for bare 4096, 4095, 0xFFFFF across all src/ files
  3. Replace each occurrence with the appropriate constant
  4. Add `#include "include/common.h"` to any file that doesn't already include it
  5. Compile and verify no behavioral changes
- **Dependencies**: none
- **Verification**: Build succeeds; grep confirms no remaining bare page-size literals; tests pass

---

### Task 2: Define named constants for CRT and Windows API magic values
- **Related Finding(s)**: C2, C4, C5
- **Impact**: Names make intent clear for 3 categories of magic numbers (CRT offset, Windows access flags, Windows error codes); improves readability and maintainability
- **Files to create**: (none)
- **Files to modify**:
  - include/nt_constants.h — add GENERIC_READ, GENERIC_WRITE, ERROR_INVALID_PARAMETER, ERROR_SUCCESS, ERROR_INSUFFICIENT_BUFFER, ERROR_ACCESS_DENIED
  - src/msvcrt/crt_refptrs.c — add `#define CRT_BSS_INITIALIZED 0x30` and use it
  - src/msvcrt/ntdll_io.c — replace inline 0x80000000/0x40000000 with GENERIC_READ/GENERIC_WRITE
  - src/msvcrt/kernel32_misc.c — replace bare error codes with named constants
- **Steps**:
  1. Add all new constants to include/nt_constants.h with comments explaining their source (Windows API, mingw-w64 CRT)
  2. Update ntdll_io.c to use GENERIC_READ/GENERIC_WRITE
  3. Update kernel32_misc.c to use named error constants
  4. Add CRT_BSS_INITIALIZED in crt_refptrs.c and use it
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; all magic values have named constants; behavior unchanged

---

### Task 3: Name remaining magic values (DLL base, exit codes, ACCESS_VIOLATION, WINE_FILE_SIZE)
- **Related Finding(s)**: C3, C6, C7, C8
- **Impact**: Completes the magic number cleanup; each remaining bare literal gets a descriptive name
- **Files to create**: (none)
- **Files to modify**:
  - src/loader/import_resolve.c — add `#define DLL_ALLOC_BASE 0x60000000`
  - src/loader/crash_handlers.c — replace 139 with EXIT_SIGSEGV, replace 0xC0000005 with STATUS_ACCESS_VIOLATION
  - src/msvcrt/crt_stdlib.c — replace 134 with EXIT_SIGABRT
  - src/msvcrt/msvcrt_priv.h — add `_Static_assert(sizeof(wine_FILE) == 48, ...)` or use sizeof() directly
  - include/common.h or include/nt_constants.h — define EXIT_SIGSEGV, EXIT_SIGABRT
- **Steps**:
  1. Add EXIT_SIGSEGV (139) and EXIT_SIGABRT (134) to include/common.h
  2. Add DLL_ALLOC_BASE (0x60000000) in import_resolve.c (or to a loader config header)
  3. Replace bare 0xC0000005 with STATUS_ACCESS_VIOLATION in crash_handlers.c
  4. Replace 139 and 134 with named constants in their respective files
  5. Add _Static_assert for WINE_FILE_SIZE in msvcrt_priv.h
  6. Compile and verify
- **Dependencies**: Task 2 must complete first (shared header modifications)
- **Verification**: Build succeeds; grep confirms no remaining bare magic numbers for these values; _Static_assert passes
