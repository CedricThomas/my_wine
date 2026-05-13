# Improvements for Later

> Non-blocking enhancements that can be tackled after the core PE32 path works.

## Metadata

| ID | Item | Priority | Effort | Status |
|----|------|----------|--------|--------|
| ABORTED-WOW64 | WoW64 In-Process Migration | — | — | Aborted |
| LINK-DYNAMIC | my_wine_32: Static → Dynamic Linking | High | Low | Deferred |
| MUSL-MALLOC | musl_malloc_32_compat vs glibc malloc | Med | Med | Deferred |
| CRT-GEN | Generalize CRT Support | Low | High | Deferred |
| WRAPPER-SPLIT | Wrapper Binary: Split my_wine into my_wine + my_wine32/my_wine64 | Med | Med | Deferred |
| DEBUG-TEST | Run All Samples/Tests Under DEBUG | High | Low | Deferred |
| SAMPLE-OUTPUT | Unify Samples Output with Expected Output Comparison | Med | Med | Deferred |

---

## ABORTED-WOW64 — WoW64 In-Process Migration

**Status:** Aborted

The in-process mode-switching approach (GDT setup + `lcall` + dual-stack) was studied
but abandoned. The dual-process fork+exec model is the permanent architecture.

**Description:** Originally explored running 32-bit PE binaries by switching CPU mode
within the same process. This required GDT manipulation and `lcall` transitions
between 64-bit and 32-bit code segments.

**Abort reason:** Too fragile and complex for the project scope. The fork+exec
dual-process model (`my_wine` → `my_wine_32`) is simpler, more reliable, and
avoids the kernel-level mode-switching complexity entirely.

---

## LINK-DYNAMIC — my_wine_32: Switch from Static to Dynamic Linking

**Problem:** `my_wine_32` is built with `-static -no-pie -Wl,--no-dynamic-linker`,
pulling the entire glibc static archive into the binary. Result: 1.2MB binary
(~885KB text is glibc internals that are never called).

**Current (Makefile line 138):**
```make
@$(MY_WINE_32_CC) -static -no-pie -o my_wine_32 $(MY_WINE_32_OBJS) \
	-nostartfiles -Wl,--no-dynamic-linker -lpthread \
	-Wl,--defsym=_DYNAMIC=0
```

**Proposed action:**
```make
@$(MY_WINE_32_CC) -no-pie -o my_wine_32 $(MY_WINE_32_OBJS) -lpthread
```

Remove `-static`, `-Wl,--no-dynamic-linker`, `-Wl,--defsym=_DYNAMIC=0`.

**Expected result:** Binary drops from ~1.2MB to ~200-300KB. The linker
delegates `libc.so.6` and `ld-linux.so.2` resolution to the dynamic loader.

**Safety:** The 32-bit code path always switches FS→TEB as the last step
before guest entry. All glibc calls (`mmap`, `setenv`, `qsort`) happen
while FS still points to glibc TLS. No glibc is called after the switch.

**Trade-offs:**
- Requires `glibc.i686` (Arch: `glibc` multilib provides `/usr/lib32/`)
  on any machine that runs the binary
- Static linking was chosen for portability (distribute `my_wine_32`
  without glibc dependency)
- If you only run on your own machine, dynamic is fine
- Revert to static if cross-machine portability is ever needed

**Prerequisites on Arch:** `/lib/ld-linux.so.2` and `/usr/lib32/libc.so.6`
must exist. Already present on multilib-enabled Arch installs.

---

## MUSL-MALLOC — musl_malloc_32_compat.c vs glibc malloc

**Problem:** The 32-bit build links `musl_malloc_32_compat.c` (a custom
mmap-based allocator), but the `-static` glibc linkage also pulls in
glibc's `malloc` arena machinery. The musl allocator code is compiled
but may never actually be used — glibc's `malloc` is the default
allocator for any glibc function that allocates.

**Proposed action:** If we switch to dynamic linking (see LINK-DYNAMIC),
glibc malloc is available via the shared library and the musl shim
becomes irrelevant unless we actively route `malloc` calls to it.
At that point, the musl shim can be audited and either kept (if
we want to control heap layout for the 32-bit child) or removed.

**Alternative path (high effort):** Replace glibc entirely with musl libc
for the 32-bit build. This gives a small static binary (~100KB) with
no TLS issues. Requires: musl-i686 toolchain, rewriting `#include` paths,
adapting syscall wrappers. Not worth the effort for this project scope.

**Trade-offs:**
- Keeping musl shim with dynamic glibc: extra code that may never execute
- Removing musl shim: simpler build, but lose explicit heap control
- Full musl replacement: smallest binary, but large rewrite cost

---

## CRT-GEN — Generalize CRT Support

**Description:** The current CRT stubs and entry path are optimized for MinGW-w64
and specifically Doom95. For broader PE compatibility, the CRT layer needs
to detect and handle different runtime conventions.

**Proposed action:**
- Design a pluggable CRT layer: each CRT variant (MinGW, Watcom, MSVC, Doom-specific)
  gets a dedicated module with its own initialization and entry strategy
- The loader auto-detects the CRT type from PE headers or import table and loads
  the matching module at runtime
- Implement minimal `HeapAlloc`/`HeapCreate` stubs so Win32 heap APIs work
  beyond the custom allocator
- Add `LoadLibraryA`/`GetProcAddress` stubs for dynamic DLL loading at runtime
- Implement `GetModuleHandle` to return the PE base address
- The Doom95-specific CRT quirks (custom entry, CRT offsets, refptr patching)
  are encapsulated in their own module — adding a new CRT type means adding
  one new file without touching the core loader

**Trade-offs:**
- Requires upfront investment in the pluggable architecture
- Increases the surface area of stubs that need maintenance and testing
- The current MinGW-only path works well for the project's scope;
  this is a "nice to have" for broader PE loader coverage
---

## WRAPPER-SPLIT — Wrapper Binary: Split my_wine into my_wine + my_wine32/my_wine64

**Description:** Currently there is a single `my_wine` binary that handles both
PE32 and PE32+ binaries (dispatching to `my_wine_32` for 32-bit via fork+exec).
The build also produces `my_wine_32` as a standalone 32-bit ELF binary.

**Proposed action:** Introduce a thin wrapper binary `my_wine` that delegates
to architecture-specific backends:

- `my_wine` — wrapper/dispatcher: parses args, detects PE architecture,
  selects the appropriate backend binary
- `my_wine64` — the current `my_wine` PE32+ loader (renamed)
- `my_wine32` — the current `my_wine_32` PE32 loader (renamed)

All three share the same CLI interface, environment variable behavior,
and argument parsing conventions.

**Benefits:**
- Cleaner mental model: each binary has a single responsibility
- Easier to debug: run `my_wine64` directly without the fork+exec indirection
- Simpler test automation: CI can target `my_wine64` or `my_wine32` directly
- The wrapper can log which backend was selected

**Trade-offs:**
- The current `my_wine` already does architecture detection and dispatch
  internally — this just makes it explicit at the binary level
- Adds one more build artifact and one more file to deploy
- The fork+exec path in the wrapper duplicates some of the current
  `my_wine` main() logic (which could be shared via a common file)
- Minimal user-facing benefit — the current model works correctly
- Future-proofing: this structure lays the groundwork for adding `my_wineserver`
  (a Wine-like server process for process/thread management) as a fourth binary
  in the architecture, without refactoring the dispatch logic later

---

## DEBUG-TEST — Run All Samples/Tests Under DEBUG

**Description:** Debug output is controlled by the `MY_WINE_DEBUG` environment
variable. The `DEBUG()` macro in `include/debug.h` gates all output through
`debug_is_enabled()`, which checks a weak function pointer set by `common.c`
based on the presence of `MY_WINE_DEBUG` in the environment. Currently,
samples are run with stderr suppressed (`2>/dev/null`) unless the `DEBUG`
env var is set in the outer shell.

**Proposed action:**
- Add a CI-style run that executes all samples with `MY_WINE_DEBUG=1`
  to verify the debug path doesn't crash or corrupt output
- Run all test binaries (`test_parse`, `test_import_resolution`, etc.)
  with `MY_WINE_DEBUG=1` as well
- Integrate `MY_WINE_DEBUG=1` into the AI-assisted development workflow
  so that debug output is available during interactive debugging sessions
- Verify that `debug_check_fn` weak pointer behavior is safe in all
  compilation contexts (main binary, test binaries, 32-bit child)

**Trade-offs:**
- Debug output adds overhead (fprintf to stderr) — may affect timing-sensitive samples
- `MY_WINE_DEBUG` is already functional; this is about verification coverage, not new code
- Running with debug on every sample validates the debug path is dead-code-safe
  (the weak pointer default, the no-op when disabled)
- A `make test-debug` or `make samples-debug` Makefile target would provide a
  quick way to run all tests/samples with `MY_WINE_DEBUG=1` without manually
  setting the env var each time — ideal for validation before commits and
  during AI-assisted development 

---

## SAMPLE-OUTPUT — Unify Samples Output with Expected Output Comparison

**Description:** Currently, sample runs only check the exit code against the
`exit=` field in `sample.info`. The actual stdout/stderr output from each
sample is either suppressed (non-debug mode) or passed through verbatim.
There is no mechanism to verify that a sample produces the expected output.

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
  `expected_output_regex.txt` variant.

---
