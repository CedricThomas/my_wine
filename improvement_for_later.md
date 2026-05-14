# Improvements for Later

## MUSL-MALLOC — musl_malloc_32_compat.c vs glibc malloc

**Status:** With dynamic linking now in place (LINK-DYNAMIC completed),
`musl_malloc_32_compat.c` is actively used for `MAP_32BIT` allocations
in the 32-bit child. The musl shim provides a dedicated low-memory
allocator for guest heap regions that must reside below the 2GB boundary,
while glibc `malloc` (via the shared `libc.so.6`) handles all other
allocation needs.

**Current state:**
- Dynamic glibc is linked, so glibc `malloc` is available through `libc.so.6`
- `musl_malloc_32_compat.c` is deliberately kept and used for `MAP_32BIT`
  allocations where the guest needs memory below 2GB
- The two allocators serve distinct purposes: musl for constrained 32-bit
  guest regions, glibc for loader-internal bookkeeping

**Alternative path (high effort):** Replace glibc entirely with musl libc
for the 32-bit build. This gives a small static binary (~100KB) with
no TLS issues. Requires: musl-i686 toolchain, rewriting `#include` paths,
adapting syscall wrappers. Not worth the effort for this project scope.

**Trade-offs:**
- Musl shim + dynamic glibc gives both low-memory control and standard libc
- Full musl replacement: smallest binary, but large rewrite cost

---

## SAMPLE-OUTPUT — Unify Samples Output with Expected Output Comparison

**Status:** ✅ DONE

**Description:** Currently, sample runs only check the exit code against the
`exit=` field in `sample.info`. The actual stdout/stderr output from each
sample is either suppressed (non-debug mode) or passed through verbatim.
There is no mechanism to verify that a sample produces the expected output.

**Implemented approach:** `expected_output.txt` file alongside each sample
(directory-based, no schema changes to `sample.info`). The runner checks
for the file and performs byte-for-byte comparison if present, falling back
to exit-code-only if absent. Non-deterministic output handled via
`expected_output_regex.txt` variant (one regex per line, auto-anchored).

**Files created:**
- `expected_output.txt`: hello_world, hello_world_32, multi_syscall,
  multi_syscall_32, multi_import_32, dispatcher_regs_32
- `expected_output_regex.txt`: dll_loader, file_io, heap_test, sync_test,
  virtual_mem

**Result:** `bash scripts/samples.sh run` shows clean PASS/FAIL per sample
with a consolidated summary. 22 passed, 1 failed (pre-existing crash),
1 skipped.

**Trade-offs:**
- Adding expected output to ~23 sample.info files is manual work (one file,
  `doom95`, uses only `archive=` with no `exit=` field and would need special handling)
- Some samples produce non-deterministic output (timestamps, memory addresses)
  which are handled via regex matching in `expected_output_regex.txt`
- The current exit-code-only check remains as fallback for samples without
  expected output files
- A unified summary output improves readability when running all samples


## TEST-OUTPUT
**Status:** ✅ DONE

**Description:** Currently, tests are really noisy and I would like a single
summary for all tests => List of PASS | FAIL + Summary for everything on
the bottom. Output can be kept if necessary or only when DEBUG flag is on.

**Implemented approach:** `scripts/run_tests.sh` refactored with a `run_test()`
helper that captures each test's output, checks for `Failed: 0` in the
standard test summary line, and shows per-test PASS/FAIL with a consolidated
summary. In `--debug` mode, full verbose output is shown before each PASS/FAIL
line.

**Result:** `bash scripts/run_tests.sh` shows 8 clean PASS/FAIL lines followed
by "8 passed, 0 failed, 0 skipped out of 8 tests". `bash scripts/run_tests.sh --debug`
shows full output with PASS/FAIL per test and summary at the end.