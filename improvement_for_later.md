# Improvements for Later

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

## GLOBAL-CONSOLIDATION — Consolidate Global Variables into Context Structs

**Problem:** The codebase carries ~45 non-static global variables and ~13 large
static arrays/structs scattered across multiple files. No globals are inherently
bad — file-scope state in a single-process loader is acceptable. However, the
current layout has three concrete issues:

1. **Duplicate definitions** — `stubs/msvcrt.c` and `src/msvcrt/crt_globals.c`
   both define `__msvcrt_app_type`, `_commode`, `_fmode`, `_msvcrt_environ`,
   `_acmdln` (static in stubs, non-static in CRT). `crt_32_stub.c` has its own
   32-bit copies. Three copies of the same state.

2. **Scattered kernel object tracking** — `sections[64]`, `views[64]`,
   `events[64]`, `mutexes[64]`, `semaphores[64]`, `threads[32]` plus individual
   `*_count` int globals are spread across `stubs/ntdll.c`, `ntdll_priv.h`,
   `ntdll_memory.c`, `ntdll_objects.c`, and `ntdll_synchronization.c`. All marked
   "SINGLE-THREAD ONLY" with no shared lock.

3. **CRT globals sprawl** — `crt_globals.c` holds `g_crt_ctx` (already a struct)
   alongside 15+ individual scalar globals for app type, commode, fmode, argv,
   envp, cmdline, startup state, and 8+ stub variables.

**Proposed action:**

- **Consolidate ntdll object tracking** into `struct kernel_objects {`
  `wine_section_t sections[64]; int section_count;` `wine_view_t views[64];`
  `int view_count; ... wine_spinlock_t lock; }`. Single definition, shared lock.

- **Consolidate CRT state** into `struct crt_runtime_state { crt_context_t ctx;`
  `int app_type, commode, fmode; char **environ; char *_acmdln; ... }`. One
  global struct instead of 15+ individual globals.

- **Consolidate loader state** into `struct loader_state { void *image_base;`
  `uintptr_t host_gs_base; char pe_path[512]; loaded_module_t modules[MAX_MODULES];`
  `int module_count; }` to group `image_mapper.c` and `module_list.c` globals.

- **Deduplicate CRT globals** across `stubs/msvcrt.c`, `crt_globals.c`, and
  `crt_32_stub.c` by having them all reference a single shared struct definition.

**Trade-offs:**
- Globals aren't inherently bad in a single-process loader — this is about
  organization, correctness, and maintainability, not eliminating globals
  wholesale
- Adding a shared `wine_spinlock_t` to kernel object tracking adds a small
  runtime cost but makes the single-thread assumption explicit (and upgradeable)
- The `loader_state` consolidation is the cleanest win — those globals are
  already logically grouped, just not in one struct
- CRT consolidation has the most payoff since it eliminates the 3-copy
  duplication across stubs, CRT, and 32-bit stub files
- Higher effort than most entries here — requires touching many files
  and updating all access paths

---

## BUILD-WARNINGS — Resolve Remaining Build Warnings

**Problem:** The 32-bit build (`my_wine_32`) produces 16 warnings across 6 files.
The 64-bit build is clean. No `-Werror` is set, so warnings pass silently.

**Categories:**

**Unused functions (4 warnings, 3 files):**
| File | Function | Status |
|---|---|---|
| `src/loader/pe32_entry.c:320` | `coff_lookup_entry` | Dead code? |
| `src/loader/import_table.c:228` | `import_entry_cmp` | Dead code? |
| `src/msvcrt/ntdll_objects.c:61` | `thread_wrapper` | Stub for future? |
| `src/msvcrt/ntdll_synchronization.c:47` | `find_semaphore` | Has `__attribute__((unused))` |

**Comparison always false — `-Wtype-limits` (5 warnings):**
All in `src/loader/teb_peb.c` (lines 141, 190, 235, 246, 400).
Pattern: `if (g_is_32bit && (uintptr_t)x >= ADDR32_LIMIT)` — on 32-bit builds
`uintptr_t` is always `<= ADDR32_LIMIT`, so the check is provably dead.

**Pointer↔int size mismatch — `-Wpointer-to-int-cast`, `-Wint-to-pointer-cast` (7 warnings, 3 files):**
| File | Direction | Example |
|---|---|---|
| `src/msvcrt/kernel32_sync.c` | pointer → `uint64_t` | `(uint64_t)&prev` on 32-bit |
| `src/msvcrt/ntdll_io.c` | `uint64_t` → pointer | `uint64_t`→`void*` on 32-bit |
| `src/msvcrt/ntdll_objects.c` | `uint64_t` → function ptr | `uint64_t`→`void(*)(void*)` on 32-bit |

**Proposed action:**
- **Unused functions:** Remove `coff_lookup_entry` and `import_entry_cmp` if unused.
  Keep `thread_wrapper` and `find_semaphore` with `__attribute__((unused))` if
  they're intentional stubs for future multi-threading support.
- **Type-limits:** Guard with `#if __SIZEOF_POINTER__ == 8` around the
  `g_is_32bit && addr >= ADDR32_LIMIT` checks, since they're dead on 32-bit.
- **Pointer↔int casts:** Use `uintptr_t` instead of `uint64_t` for pointer
  intermediaries in the 32- to 64-bit transition code. The casts work but
  trigger warnings because 32-bit pointers are 32 bits, not 64.
- **Consider adding `-Werror`** once all warnings are resolved to catch future
  regressions at build time.

**Trade-offs:**
- The pointer↔int casts are functionally correct (the code works), just
  noisy — fixing them is cosmetic but improves build hygiene
- The type-limits warnings are genuinely dead code on 32-bit; the fix
  is straightforward (`#if __SIZEOF_POINTER__ == 8`)
- Adding `-Werror` is a good long-term goal but requires all warnings
  to be clean first (including in the musl wrapper, which already uses
  `#pragma GCC diagnostic push/pop`) — this entry addresses the 16
  active warnings; musl wrapper warnings are expected and suppressed

---