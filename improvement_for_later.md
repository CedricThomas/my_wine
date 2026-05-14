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

**Description:** Currently, sample runs only check the exit code against the
`exit=` field in `sample.info`. The actual stdout/stderr output from each
sample is either suppressed (non-debug mode) or passed through verbatim.
There is no mechanism to verify that a sample produces the expected output.
The end goal would be to have clean ouput of only PASS | FAIL lines + sumarry at the end. Purely cosmetic but clean command approach

**Proposed action:**
- Add an `output=` or `expected=` field to `sample.info` files that contains
  the expected stdout (one line, or a path to a file with multi-line output)
- After running a sample, capture stdout and compare against the expected output
  (byte-for-byte or line-by-line, configurable)
- Unify the sample run output into a single consolidated test report showing
  all results (pass/fail/skip per sample with reason) in one summary block,
  rather than interleaved per-sample output
- Consider adding `archive=` field support (already present in some sample.info
  files) for samples that come from external archives

**Trade-offs:**
- Adding expected output to ~23 sample.info files is manual work (one file, `doom95`, uses only `archive=` with no `exit=` field and would need special handling)
- Some samples produce non-deterministic output (timestamps, memory addresses)
  which would need to be handled (regex matching or selective comparison)
- The current exit-code-only check is simple and effective for most samples
- A unified summary output improves readability when running all samples
  but adds complexity to `samples.sh`
- An `expected_output.txt` file alongside each sample binary is a clean approach:
  no schema changes to `sample.info`, easy to update, and git-tracked per sample.
  The runner checks for the file and performs byte-for-byte comparison if present,
  falling back to exit-code-only if absent. Non-deterministic output (timestamps,
  memory addresses) can be handled via regex patterns in the file or a separate
  `expected_output_regex.txt` variant. => Choosed approach. 


## TEST-OUTPUT
**Description:** Currently, test are really noisy and I would like a single summary for all test => List of PASS | FAIL + Summary for everything on the bottom. Output can be keep if necessary or only keep when DEBUG flag is on. Purely cosmetic but clean command approach