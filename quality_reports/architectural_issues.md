# Architectural Issues

> From quality_report.log, generated 2026-05-06, updated 2026-05-07
> Status verified against current codebase.

---

## Findings

### [FINDING A1] — Monolithic loader_priv.h — shared state sprawl
- **Status**: ❌ OPEN
- **Severity**: HIGH
- **Files**: src/loader/loader_priv.h (176 lines, 50+ symbols)
- **Description**: loader_priv.h declares 50+ symbols spanning 8+ modules (image_mapper, import_resolve, import_table, ordinal_table, teb_peb, crash_handlers, entry, guest_setup, gs_base, module_list, export_table, peb_ldr). This creates a god-header: any loader module can access any other's globals and functions. The file has no module boundaries — everything is one flat namespace.

  Specific shared globals that blur module boundaries:
    - g_image_base, g_pe_path (image_mapper) accessible from import_resolve
    - g_stack_base, g_stack_size (teb_peb) accessible from entry
    - import_table[] (import_table) accessible from import_resolve
    - g_peb_ldr (peb_ldr) accessible from multiple modules

- **Suggested Fix**: Split into per-module headers (e.g., loader/image_mapper.h, loader/import_resolve.h) that only export what's needed. Keep loader_priv.h as the internal aggregator that #includes those.

---

### [FINDING A2] — import_resolve.c is a multipurpose file (import resolution + DLL loading + path finding)
- **Status**: ❌ OPEN
- **Severity**: HIGH
- **Files**: src/loader/import_resolve.c (594 lines)
- **Description**: This file handles three distinct responsibilities:
  1. Import name resolution (pass 1 & pass 2 thunk patching) — lines 1–370
  2. DLL path searching (find_dll_path, hand-rolled string helpers) — lines 370–508
  3. DLL loading (load_dll with map/relocate/register/resolve chain) — lines 510–597

  This violates Single Responsibility. DLL loading should be in its own module (e.g., dll_loader.c).

- **Suggested Fix**:
  - Extract DLL path finding → src/loader/dll_path.c
  - Extract DLL loading → src/loader/dll_loader.c
  - Keep import_resolve.c focused on IAT resolution only

---

### [FINDING A3] — main() is the process orchestrator — excessive responsibility
- **Status**: ❌ OPEN
- **Severity**: MEDIUM
- **Files**: src/main.c (230 lines)
- **Description**: main() performs 10+ sequential steps: parse env, map image, init imports, patch refptrs, resolve imports, setup TEB/PEB, setup stack, zero .data, pre-seed BSS, build guest argv, lookup symbol, and run. Each step represents a different subsystem. The function is 230 lines and contains inline logic for section finding and mprotect.

- **Suggested Fix**: Wrap the 10 steps into a single `init_loader()` function so main() becomes a ~10-line entry point. This enables cleaner testing and potential future restart/reload paths.

---

### [FINDING A4] — ntdll_priv.h declares globals shared across all handler modules
- **Status**: ❌ OPEN
- **Severity**: MEDIUM
- **Files**: src/msvcrt/ntdll_priv.h (12 extern declarations)
- **Description**: This header declares arrays (handle_table, sections, views, events, mutexes, threads) and their counters as external globals. Every ntdll handler file that includes this header can read/write all of them. No synchronization mechanism (mutex, atomic) is used to protect concurrent access.

- **Suggested Fix**: Use accessor functions (get_handle_table(), alloc_section(), etc.) with internal locks. Or at minimum, document that these are single-thread globals.

---

### [FINDING A5] — PE section table offset computation duplicated in 5 files
- **Status**: ❌ OPEN
- **Severity**: MEDIUM
- **Files**: src/main.c:133-134, src/loader/guest_setup.c:190-191, src/loader/import_resolve.c:221-222, src/loader/image_mapper.c:107-108,151-152
- **Description**: The expression to compute section table offset from image base:
  ```c
  pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
  nt.FileHeader.SizeOfOptionalHeader
  ```
  appears verbatim (or nearly so) in 5 source files. pe_priv.h provides compute_section_table_offset() but it takes (dos, nt) params and works with file offsets, not image offsets.

- **Suggested Fix**: Add a function to pe_priv.h:
  ```c
  static inline IMAGE_SECTION_HEADER *get_image_sections(void *image_base,
                                                          IMAGE_NT_HEADERS64 *nt);
  ```
  And use it everywhere instead of the inline calculation.

---

## Implementation Plan

Tasks ordered by priority (impact vs effort). Each task is independently implementable.

### Task 1: Split loader_priv.h into per-module headers
- **Related Finding(s)**: A1
- **Impact**: Eliminates the god-header; creates clear module boundaries so each loader module only exposes what it needs
- **Files to create**:
  - src/loader/image_mapper.h
  - src/loader/import_resolve.h
  - src/loader/import_table.h
  - src/loader/teb_peb.h
  - src/loader/crash_handlers.h
  - src/loader/guest_setup.h
  - src/loader/gs_base.h
  - src/loader/module_list.h
  - src/loader/peb_ldr.h
- **Files to modify**:
  - src/loader/loader_priv.h — convert to aggregator that #includes the per-module headers; remove inline declarations
  - Each module's .c file — adjust includes to use its own header where appropriate
- **Steps**:
  1. Audit all symbols in loader_priv.h and group by module of ownership
  2. Create one header per module with only that module's extern declarations and function prototypes
  3. Convert loader_priv.h into an aggregator: `#include "image_mapper.h"` etc.
  4. Update all .c files that include loader_priv.h to use specific module headers where possible
  5. Compile and verify no new warnings
- **Dependencies**: none
- **Verification**: Build succeeds; each module header compiles independently; grep confirms no cross-module global access

---

### Task 2: Split import_resolve.c into 3 focused files
- **Related Finding(s)**: A2
- **Impact**: Reduces a 594-line multipurpose file into three single-responsibility modules (~200 lines each); improves readability and enables independent testing
- **Files to create**:
  - src/loader/dll_path.c
  - src/loader/dll_path.h
  - src/loader/dll_loader.c
  - src/loader/dll_loader.h
- **Files to modify**:
  - src/loader/import_resolve.c — strip DLL path search and DLL loading; keep only IAT resolution
  - src/loader/loader_priv.h — add #include for new headers
- **Steps**:
  1. Identify the exact boundary lines for import resolution, path finding, and DLL loading
  2. Extract path finding code + string helpers into dll_path.c/dll_path.h
  3. Extract DLL loading code into dll_loader.c/dll_loader.h
  4. Remove extracted code from import_resolve.c; add #include for new headers
  5. Update Makefile to compile new .c files
  6. Compile and run tests
- **Dependencies**: none (independent from Task 1, though they can be merged)
- **Verification**: Build succeeds; import_resolve.c is under 400 lines; new headers are properly included

---

### Task 3: Extract init_loader() from main()
- **Related Finding(s)**: A3
- **Impact**: Reduces main() to ~10 lines; the full startup sequence is encapsulated in a testable function; enables future restart/reload paths
- **Files to create**: (none)
- **Files to modify**:
  - src/main.c — extract startup steps into init_loader(); main() calls init_loader() and exits
- **Steps**:
  1. Identify the sequential steps in main()
  2. Wrap the body of main() (everything after argument parsing) into `static int init_loader(int argc, char **argv)`
  3. Simplify main() to: parse args → call init_loader() → return
  4. Remove numbered step comments; replace with inline comments in init_loader()
  5. Compile and verify behavior is identical
- **Dependencies**: none
- **Verification**: Build succeeds; main() is under 20 lines; program behavior unchanged

---

### Task 4: Add accessor functions to ntdll_priv.h globals
- **Related Finding(s)**: A4
- **Impact**: Provides a controlled interface to shared handler state; documents single-thread limitation; prepares for future synchronization
- **Files to create**: (none)
- **Files to modify**:
  - src/msvcrt/ntdll_priv.h — add accessor function declarations and documentation
  - src/msvcrt/ntdll_handler.c (or new file) — implement accessor functions
- **Steps**:
  1. Add `// Single-thread globals — not safe for concurrent access` comment to ntdll_priv.h
  2. Declare accessor functions: `get_handle_table()`, `alloc_section()`, `alloc_event()`, `alloc_mutex()`, `alloc_thread()`
  3. Implement in a shared ntdll_handler.c (or in the first file that defines the globals)
  4. Update callers to use accessors where feasible
  5. Compile and verify
- **Dependencies**: none
- **Verification**: Build succeeds; no new warnings; accessors are used at least for handle_table

---

### Task 5: Add get_image_sections() helper to pe_priv.h
- **Related Finding(s)**: A5
- **Impact**: Eliminates duplicated section table offset computation across 5 files; single source of truth for in-memory section lookup
- **Files to create**: (none)
- **Files to modify**:
  - include/pe_priv.h — add `get_image_sections()` inline function
  - src/main.c — replace inline calculation with get_image_sections()
  - src/loader/guest_setup.c — replace inline calculation
  - src/loader/import_resolve.c — replace inline calculation
  - src/loader/image_mapper.c — replace inline calculation
- **Steps**:
  1. Implement `static inline IMAGE_SECTION_HEADER *get_image_sections(void *image_base, IMAGE_NT_HEADERS64 *nt)` in pe_priv.h
  2. Find all 5 locations of the inline calculation
  3. Replace each with a call to get_image_sections()
  4. Compile and verify behavior is identical
- **Dependencies**: none
- **Verification**: Build succeeds; grep confirms no remaining inline section offset calculations; program behavior unchanged
