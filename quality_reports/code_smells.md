# Code Smells

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING B1] — Duplication: dll_strcasecmp, dll_copy_str, dll_memset in two files
- **Status**: ❌ OPEN (partially — loader_utils.h exists but is not used by all modules)
- **Severity**: HIGH
- **Files**: src/loader/import_resolve.c (lines 37-55, 384-423), src/loader/module_list.c (lines 15-31), src/loader/loader_utils.h
- **Description**: Both import_resolve.c and module_list.c define their own copies of `dll_strcasecmp()`, `dll_copy_str()`, `dll_memset()`. Additionally, import_resolve.c defines `dll_strlen()`, `dll_strncmp()`, `dll_strchr()`, `dll_build_path()`, `dll_path_exists()` — none shared with module_list.c.

  `loader_utils.h` exists with shared implementations of these functions, but **neither import_resolve.c nor module_list.c actually include or use it** — they both keep their own `static` copies.

  Total ~8 hand-rolled string/memory functions duplicated across 2 files.

- **Suggested Fix**: Make import_resolve.c and module_list.c `#include "loader_utils.h"` and remove their local `static` copies. Consider using `__builtin_memcpy`, `__builtin_strlen` etc. which don't access vDSO, instead of hand-rolled loops.

---

### [FINDING B2] — Long function: find_symbol_rva_from_file()
- **Status**: ❌ OPEN
- **Severity**: MEDIUM
- **Files**: src/msvcrt/crt_offset_discovery.c (~165 lines for this function)
- **Description**: This function opens a file, mmaps it, parses COFF symbols, and searches for symbol names. It contains 4+ symbol matching strategies (refptr prefix, exact match, substring fallback) with extensive debug logging sprinkled throughout. The debug output alone is ~30 lines.

- **Suggested Fix**: Split into: `open_and_map_symbols()` + `find_matching_symbol()` + `compute_rva_from_symbol()`. Move debug output behind DEBUG() macro.

---

### [FINDING B3] — Dead/unused code: handle_syscall() in dispatcher.c
- **Status**: ✅ FIXED
- **Severity**: LOW
- **Files**: src/syscall/dispatcher.c
- **Description**: ~~handle_syscall() was a legacy ucontext-based dispatcher kept "for backward compatibility with existing tests." It was a complete duplicate of c_dispatch_syscall() with the only difference being register source.~~

  **Current state**: The dispatcher now uses a single `dispatcher_core()` function that includes the generated switch body, shared by the `c_dispatch_syscall()` entry point. The duplicate `handle_syscall()` has been removed. The dispatcher.c file has a clean comment: "Single entry point: c_dispatch_syscall(). Uses dispatcher_core() which includes the generated switch body."

- **Remaining risk**: The preamble (trace output, arg extraction) in c_dispatch_syscall() could still be extracted for further dedup if new entry points are added.

---

### [FINDING B4] — Large import_table[] with mixed static/dynamic entries
- **Status**: ❌ OPEN
- **Severity**: LOW
- **Files**: src/loader/import_table.c (lines 27–128)
- **Description**: The import_table[] array mixes three categories:
  1. ntdll syscall handlers (resolved via thunk lookup)
  2. kernel32 stubs (our C implementations)
  3. msvcrt functions — half statically known, half dynamically filled

  The dynamic entries (NULL address) are filled by init_msvcrt_imports() at runtime. The mix of resolved and unresolved entries in one flat table means binary search works for both but the intent is unclear.

- **Suggested Fix**: Document the three tiers clearly. Or use a struct with resolution strategy field (THUNK / STUB / DYNAMIC) for clarity.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Deduplicate dll_* string helpers
- **Related Finding(s)**: B1
- **Impact**: Removes ~60 lines of duplicated code; single source of truth for syscall-safe string/memory ops
- **Files to modify**:
  - src/loader/import_resolve.c — remove local dll_strcasecmp, dll_copy_str, dll_strlen, etc.; add `#include "loader_utils.h"`
  - src/loader/module_list.c — remove local dll_strcasecmp, dll_copy_str, dll_memset; add `#include "loader_utils.h"`
- **Steps**:
  1. Verify loader_utils.h has complete implementations of all needed functions
  2. In import_resolve.c: remove static copies of dll_strcasecmp, dll_copy_str, dll_strlen, dll_strncmp, dll_strchr, dll_build_path; add include
  3. In module_list.c: remove static copies of dll_memset, dll_copy_str, dll_strcasecmp; add include
  4. Compile and run tests
- **Dependencies**: none
- **Verification**: Build succeeds; grep confirms no remaining static copies of these functions in .c files

---

### Task 2: Split find_symbol_rva_from_file()
- **Related Finding(s)**: B2
- **Impact**: Reduces a 165-line function into 3 focused ~40-50 line functions; moves debug spam behind DEBUG() macro
- **Files to modify**:
  - src/msvcrt/crt_offset_discovery.c
- **Steps**:
  1. Split into `open_and_map_symbols()`, `find_matching_symbol()`, `compute_rva_from_symbol()`
  2. Move DEBUG_printf calls behind DEBUG() macro
  3. Compile and verify behavior is identical
- **Dependencies**: none
- **Verification**: Build succeeds; each sub-function is under 60 lines; symbol discovery still works

---

### Task 3: Document or structure import_table[] tiers
- **Related Finding(s)**: B4
- **Impact**: Improves readability; makes the resolution strategy explicit
- **Files to modify**:
  - src/loader/import_table.c — add section comments separating the three tiers
- **Steps**:
  1. Add `// ── Tier 1: ntdll syscall handlers ──` etc. comments
  2. Document the NULL-address convention for dynamic entries
  3. (Optional) Add a `resolution_type` field to the entry struct
- **Dependencies**: none
- **Verification**: Code is more readable; no behavioral change
