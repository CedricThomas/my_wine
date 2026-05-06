# Quality Reports — Handover

> Updated: 2026-05-07
> Source: quality_report.log (git: d0b5f81 → 8307077)

## Summary

| Category | File | Total | ✅ Fixed | ⚠️ Partial | ❌ Open |
|---|---|---|---|---|---|
| Architectural Issues | [architectural_issues.md](./architectural_issues.md) | 5 | 0 | 0 | 5 |
| Code Smells | [code_smells.md](./code_smells.md) | 4 | 1 | 0 | 3 |
| Magic Numbers | [magic_numbers.md](./magic_numbers.md) | 8 | 3 | 1 | 4 |
| Non-Future-Proof | [non_future_proof.md](./non_future_proof.md) | 5 | 0 | 0 | 5 |
| Bad Designs | [bad_designs.md](./bad_designs.md) | 4 | 2 | 2 | 0 |
| **Total** | | **26** | **6** | **3** | **17** |

## What Changed Since Original Report

### Fixed (6)
- **B3** — handle_syscall() removed; dispatcher now uses shared `dispatcher_core()`
- **C2** — CRT BSS `0x30` → `#define CRT_BSS_INITIALIZED 0x30`
- **C3** — DLL base `0x60000000` → `#define DLL_ALLOC_BASE`
- **C6** — Exit codes `139`/`134` → `EXIT_SIGSEGV`/`EXIT_SIGABRT`
- **C7** — `0xC0000005` → `EXIT_SIGSEGV` named constant
- **E2** — Silent mmap failure → warning via `INLINE_SYSCALL_WRITE` + `g_alt_stack_available` flag

### Partially Fixed (3)
- **B1** — `loader_utils.h` exists with shared dll_* funcs, but import_resolve.c and module_list.c still have their own static copies (not using the shared header)
- **C4** — `GENERIC_READ`/`GENERIC_WRITE` defined in nt_constants.h, but ntdll.c still declares local variables with the same values
- **E1** — `g_dll_base_next` now uses `__atomic_*` builtins; other globals (handle_table, g_crt_ctx) still unprotected
- **E3** — `refptr_patch_arg` is now stack-local instead of global, but still depends on `g_crt_ctx` global

### Still Open (17)
- **HIGH**: A1 (god-header), A2 (multipurpose import_resolve.c), B1 (duplication), D1 (no arch guard), D2 (syscall version)
- **MEDIUM**: A3 (main() 230 lines), A4 (ntdll_priv.h globals), A5 (section offset dup), B2 (long function), C1 (bare 4096/4095), C5 (bare error codes), D3 (PAGE_SIZE hardcoded), D4 (mingw-w64 CRT), D5 (PE32 error msg), E1 (thread safety)
- **LOW**: B4 (mixed import_table), C8 (WINE_FILE_SIZE)

## Recommended Priority Order

1. **B1** — Make modules use `loader_utils.h` (high impact, low risk, ~60 lines saved)
2. **C1** — Replace bare 4096/4095 with PAGE_SIZE/PAGE_MASK in stubs/msvcrt
3. **A2** — Split import_resolve.c into 3 files
4. **A5** — Add `get_image_sections()` helper
5. **C4** — Fix remaining hardcoded GENERIC_READ/WRITE in ntdll.c
6. **C5** — Define error code constants in nt_constants.h
7. **C8** — Add `_Static_assert` for WINE_FILE_SIZE
8. **D1** — Add `#error` compile-time arch guard
9. **D5** — Improve PE32 rejection message
10. **A3** — Extract `init_loader()` from main()
11. **B2** — Split `find_symbol_rva_from_file()`
12. **E1** — Document thread-safety limitations
13. **A4** — Add accessor functions for ntdll_priv.h globals
14. **A1** — Split loader_priv.h into per-module headers (biggest refactoring)
15. **D2** — Document syscall version assumptions
16. **D3** — Runtime PAGE_SIZE query
17. **D4** — Document mingw-w64 CRT limitation
18. **B4** — Document/structure import_table[] tiers

---

## Handover Template

Copy the template below, replace `{REPORT}` with the report file, and give it to the agent.

```
You are working on the my_wine PE loader codebase at /home/arzad/Bureau/my_wine/.

Your task: implement the fixes defined in the quality report at:
  quality_reports/{REPORT}.md

Before you start:
1. Read {REPORT}.md in full to understand the findings and implementation plan.
2. Read each source file mentioned in the findings so you understand the current code.
3. Work through the tasks in order (Task 1 → Task 2 → ...), respecting dependencies.

Rules:
- Make minimal, surgical changes. Do not rewrite functions unless the task explicitly says so.
- After each task, compile the project to verify no regressions.
- If a task requires changes to a file that another task also touches, coordinate the edits to avoid conflicts.
- When adding #include directives, use the existing include path conventions in the project.
- Do NOT touch musl_src/, generated files, or test files unless the report explicitly mentions them.

After completing all tasks in the report:
1. Do a final compile and verify the project still works.
2. Report back: what you changed, which files were created/modified, and any issues encountered.
```

### Report Files

| Report | Focus |
|--------|---|
| `code_smells.md` | Deduplicate helpers, split long functions, structure import_table |
| `magic_numbers.md` | Replace bare 4096/4095, define error constants, add _Static_assert |
| `bad_designs.md` | Document thread-safety, harden error paths, improve reentrancy |
| `architectural_issues.md` | **Largest** — split god-headers, refactor import_resolve.c, extract init_loader() |
| `non_future_proof.md` | Add architecture guards, document limitations, improve error messages |

### Recommended Order

1. `magic_numbers.md` (small, safe constant replacements)
2. `bad_designs.md` (documentation + targeted fixes)
3. `code_smells.md` (deduplication, function splitting)
4. `non_future_proof.md` (documentation + defensive checks)
5. `architectural_issues.md` (big structural changes, last so they don't conflict)

---

## Final Verification Phase

> Run this **after all report tasks are complete** to verify the quality report can be closed, no regressions were introduced, and the repo is clean for merge.

Copy the prompt below and paste it directly to the new agent. It is self-contained — the agent does not need to read this file first.

```
You are working on the my_wine PE loader codebase at /home/arzad/Bureau/my_wine/.

You are in the **Final Verification Phase**. All quality report tasks should be complete.
Your job: verify the report can be closed, no regressions were introduced, and the repo is clean for merge.

The quality reports live in quality_reports/ (architectural_issues.md, code_smells.md, magic_numbers.md, non_future_proof.md, bad_designs.md, HANDOVER.md). Read them to understand what was supposed to be done.

---

## Step 1 — Findings Re-Audit

For **every finding** across all 5 report files, re-check against the current codebase:

```bash
# Verify magic numbers

grep -rn '\b4096\b\|4095\b' src/ include/ --include="*.c" --include="*.h" | grep -v 'PAGE_SIZE\|PAGE_MASK\|_cmdline_storage\|//' 

# Verify duplication fixed

grep -rn 'static.*dll_strcasecmp\|static.*dll_copy_str\|static.*dll_memset' src/ --include="*.c"

# Verify GENERIC_READ/WRITE not hardcoded

grep -rn '= 0x80000000\|= 0x40000000' src/ --include="*.c" | grep -v 'GENERIC_READ\|GENERIC_WRITE\|IMAGE_SCN'

# Verify error codes not bare

grep -rn 'return 87\|return 122\|return 139\|return 134' src/ --include="*.c"

# Verify no bare ACCESS_VIOLATION

grep -rn '0xC0000005' src/ --include="*.c" | grep -v 'STATUS_ACCESS_VIOLATION'

# Verify WINE_FILE_SIZE has _Static_assert

grep -n '_Static_assert.*wine_FILE' src/stubs/msvcrt.c

# Verify architecture guard exists

grep -n '#error.*x86_64\|#if.*!.*__x86_64__' include/common.h
```

Update each report file: set `Status:` to ✅ FIXED or document why it remains ⚠️ / ❌.

---

## Step 2 — Clean Build

```bash
make clean && make 2>&1 | tee build_output.txt
```

- **Pass**: zero errors, zero new warnings
- **Fail**: fix all compiler errors and new warnings before proceeding

---

## Step 3 — Regression Testing

```bash
# Run the full test suite (adjust to your actual test command)
./tests/run_tests.sh 2>&1 | tee test_output.txt

# If no test harness, manually verify the loader still works:
./my_wine /path/to/test.exe
```

Verify the loader still:
- **Maps and runs** a basic PE (e.g., hello.exe)
- **Resolves imports** correctly (ntdll, kernel32, msvcrt)
- **Handles crashes** without segfaulting itself
- **Produces the same output** as before the changes

---

## Step 4 — Repo Hygiene

```bash
# Check for uncommitted or untracked files
git status

# Check for leftover debug or TODO markers from the work
grep -rn 'TODO\|FIXME\|HACK\|XXX' src/ include/ --include="*.c" --include="*.h"

# Check no generated files are dirty
git diff --stat HEAD
```

- **Pass**: clean working tree, no leftover debug code
- **Fail**: clean up before merge

---

## Step 5 — Documentation Consistency

- Verify all 5 report files have accurate `Status:` for every finding
- Verify HANDOVER.md summary table matches the actual status across all reports
- If any findings were accepted as **won't fix**, document the rationale in the report

---

## Go / No-Go Decision

| Criteria | Pass | Fail |
|---|---|---|
| All 26 findings addressed (fixed, partial-accepted, or won't-fix) | ✅ | ❌ block merge |
| Clean build, no new warnings | ✅ | ❌ block merge |
| All tests pass, loader runs a PE successfully | ✅ | ❌ block merge |
| No leftover debug code or untracked files | ✅ | ⚠️ fix before merge |
| Reports updated to final state, HANDOVER summary accurate | ✅ | ⚠️ fix before merge |

**On pass**: close the quality report, commit, and merge.
**On fail**: fix remaining items and re-verify.
```
