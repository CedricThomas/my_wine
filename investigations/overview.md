# DOOM95 Branch Investigation — Master Index

**Current branch**: HEAD (`08b3ed0` "all passing but no doom")  
**Working reference**: `refs/working` (commit `b6717ea` "working")  
**Date**: 2026-05-26

---

## Gate Results

| Gate | Working (b6717ea) | Current (08b3ed0) | Status |
|------|-------------------|-------------------|--------|
| Native unit tests (14) | 14/14 pass | 14/14 pass | ✅ PASS |
| Simple PE samples (hello_world, file_io, heap_test, sync_test, time_test) | All pass | All pass | ✅ PASS |
| SDL2 samples (sdl2_window_32, sdl2_nccreate_reject_32) | **CRASH** `ucontext=0x0000006b` | **CRASH** `ucontext=0x0000006b` | ⚠️ Pre-existing |
| DOOM95 rendering loop | **Runs** → audio init → rendering | **CRASH** at FindFirstFileA `ucontext=0x0000006b` | ❌ **REGRESSION** |

---

## Issue Index

| ID | File | Title | Severity | Status |
|----|------|-------|----------|--------|
| INV-001 | [sdl2-sigsegv-assert.md](sdl2-sigsegv-assert.md) | SDL2 samples: SIGSEV with corrupted ucontext (pre-existing) | Medium | pre-existing |
| INV-002 | [doom95-findfirstfile-crash.md](doom95-findfirstfile-crash.md) | DOOM95: NULL deref on guest stack after `FindFirstFileA`/`FindNextFileA` stub calls return successfully. Crash is in **guest DOOM95 code**, not in my_wine stubs. glibc+guest-FS hypothesis debunked by experiment. | **Critical** | **crash in guest code — sigaltstack fix needed for identification** |

---

## Consolidated Report

See [summary.md](summary.md) for the full analysis, diff summary, root cause, and fix recommendations.

## Breaking Commit

The crash is a NULL deref on the guest stack (ESP=0x0057e5a0) that happens AFTER `FindFirstFileA`/`FindNextFileA` stub calls return successfully. Debug tracing confirms the crash is in **guest DOOM95 code**, not in my_wine stubs. All glibc-related hypotheses (H1-H6) have been debunked. Leading theory: `rb_init` from commit `10199a8` changes behavior that causes DOOM95 to enter an infinite `FindFirstFileA`/`FindNextFileA` loop that eventually crashes. sigaltstack fix is the highest priority to enable proper crash site identification.
