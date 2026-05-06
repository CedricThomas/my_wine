# Code Smells

> From quality_report.log, generated 2026-05-06

---

## Findings

[FINDING B1] — Duplication: dll_strcasecmp, dll_copy_str, dll_memset in two files
- **Severity**: HIGH
- **Files**: src/loader/import_resolve.c:36-39, src/loader/module_list.c:23-34
- **Description**: Both import_resolve.c and module_list.c define identical (or near-identical) implementations of dll_strcasecmp(), dll_copy_str(), dll_memset().

  import_resolve.c also defines: dll_strlen(), dll_strncmp(), dll_strchr(), dll_build_path(), dll_path_exists() — none of which are shared with module_list.c.

  Total ~8 hand-rolled string/memory functions duplicated across 2 files.

- **Suggested Fix**: Create src/loader/loader_utils.h with these functions. Include from both modules. Consider using __builtin_memcpy, __builtin_strlen etc. which don't access vDSO, instead of hand-rolled loops.

---

[FINDING B2] — Long function: find_symbol_rva_from_file()
- **Severity**: MEDIUM
- **Files**: src/msvcrt/crt_offset_discovery.c (lines 114–260, ~146 lines)
- **Description**: This function opens a file, mmaps it, parses COFF symbols, and searches for symbol names. It contains 4+ symbol matching strategies (refptr prefix, exact match, substring fallback) with extensive debug logging sprinkled throughout. The debug output alone is ~30 lines.

- **Suggested Fix**: Split into: open_and_map_symbols() + find_matching_symbol() + compute_rva_from_symbol(). Move debug output behind DEBUG() macro.

---

[FINDING B3] — Dead/unused code: handle_syscall() in dispatcher.c
- **Severity**: LOW
- **Files**: src/syscall/dispatcher.c (lines ~200+)
- **Description**: handle_syscall() is a legacy ucontext-based dispatcher kept "for backward compatibility with existing tests." It is a complete duplicate of c_dispatch_syscall() with the only difference being register source (ctx->uc_mcontext.gregs vs __wine_guest_regs). This doubles maintenance burden: every new syscall added must be handled in both paths.

- **Suggested Fix**: Extract the common switch body (already done via dispatcher_generated.c inclusion) — this is already done. However the two wrapper functions still have duplicated preamble (trace output, arg extraction). Consider a single dispatcher_core() that takes register values as parameters.

---

[FINDING B4] — Large import_table[] with mixed static/dynamic entries
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

### Task 1: Create loader_utils.h with shared string/memory helpers
- **Related Finding(s)**: B1
- **Impact**: Eliminates ~8 duplicated functions across 2 files; single source of truth for syscall-safe string operations; recommended as the highest-priority fix in the quality report
- **Files to create**:
  - src/loader/loader_utils.h
- **Files to modify**:
  - src/loader/import_resolve.c — remove hand-rolled implementations; add `#include "loader_utils.h"`
  - src/loader/module_list.c — remove hand-rolled implementations; add `#include "loader_utils.h"`
- **Steps**:
  1. Collect all dll_strcasecmp, dll_copy_str, dll_memset, dll_strlen, dll_strncmp, dll_strchr, dll_build_path, dll_path_exists from both files
  2. Create loader_utils.h with static inline implementations (prefer __builtin_* variants where safe)
  3. Remove duplicate definitions from import_resolve.c and module_list.c
  4. Add `#include "loader_utils.h"` to both files
  5. Compile and verify no behavioral changes
- **Dependencies**: none
- **Verification**: Build succeeds; grep confirms only one definition of each function; tests pass

---

### Task 2: Split find_symbol_rva_from_file() into smaller functions
- **Related Finding(s)**: B2
- **Impact**: Reduces a 146-line function into 3 focused functions; improves readability and testability of CRT offset discovery
- **Files to create**: (none)
- **Files to modify**:
  - src/msvcrt/crt_offset_discovery.c — refactor find_symbol_rva_from_file()
- **Steps**:
  1. Extract file open + mmap logic into `static void *open_and_map_symbols(const char *path, size_t *mapped_size)`
  2. Extract symbol matching logic into `static const char *find_matching_symbol(void *map, size_t size, const char *target)`
  3. Extract RVA computation into `static uint32_t compute_rva_from_symbol(const char *sym_ptr, void *map)`
  4. Move all debug printf/fprintf calls behind a `DEBUG()` macro (no-op in release)
  5. Rewrite find_symbol_rva_from_file() as a thin orchestrator calling the 3 helpers
  6. Compile and verify behavior is identical
- **Dependencies**: none
- **Verification**: Build succeeds; new functions are each under 40 lines; debug output unchanged in debug builds

---

### Task 3: Unify dispatcher preamble into dispatcher_core()
- **Related Finding(s)**: B3
- **Impact**: Eliminates duplicated preamble code (trace output, arg extraction) between handle_syscall() and c_dispatch_syscall(); reduces maintenance burden for new syscalls
- **Files to create**: (none)
- **Files to modify**:
  - src/syscall/dispatcher.c — refactor handle_syscall() and c_dispatch_syscall()
- **Steps**:
  1. Identify the duplicated preamble code in both wrapper functions (trace output, arg extraction from registers)
  2. Create `static void dispatcher_core(uint64_t syscall_num, uint64_t *args, uint64_t *ret)` containing the common switch body
  3. Refactor handle_syscall() to extract registers from ucontext, call dispatcher_core()
  4. Refactor c_dispatch_syscall() to extract registers from __wine_guest_regs, call dispatcher_core()
  5. Compile and run all tests (especially the backward-compat ucontext tests)
- **Dependencies**: none
- **Verification**: Build succeeds; both dispatcher paths produce identical output; all tests pass

---

### Task 4: Add resolution tier documentation to import_table[]
- **Related Finding(s)**: B4
- **Impact**: Clarifies the intent of each entry in the mixed import table; prevents accidental misuse; aids future maintainers
- **Files to create**: (none)
- **Files to modify**:
  - src/loader/import_table.c — add documentation comments and optionally a resolution strategy enum
- **Steps**:
  1. Define enum `resolution_type { RESOLVE_THUNK, RESOLVE_STUB, RESOLVE_DYNAMIC }` in import_table.c or loader_priv.h
  2. Convert import_table[] entries from flat `{ name, func_ptr }` to structured `{ name, func_ptr, type }` OR add block comments separating the three tiers
  3. Add a top-of-file comment explaining the three tiers and how init_msvcrt_imports() fills dynamic entries
  4. Compile and verify no behavioral changes
- **Dependencies**: none
- **Verification**: Build succeeds; table structure is documented; intent is clear to a new reader
