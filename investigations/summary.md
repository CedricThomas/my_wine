# DOOM95 Branch Investigation — Consolidated Report

**Branch**: HEAD (`08b3ed0`)  
**Working**: `refs/working` (commit `b6717ea`)  
**Breaking commit**: `10199a8` ("all tests passing")

---

## Gate Results

| Gate | Working | Current | Verdict |
|------|---------|---------|---------|
| Native unit tests (14) | 14/14 pass | 14/14 pass | ✅ PASS |
| Simple PE samples (hello_world, file_io, heap_test, sync_test, time_test) | All pass | All pass | ✅ PASS |
| SDL2 samples (sdl2_window_32, etc.) | CRASH `ucontext=0x0000006b` | CRASH `ucontext=0x0000006b` | ⚠️ Pre-existing (INV-001) |
| DOOM95 rendering loop | **Runs** (enters audio init → rendering) | **CRASH** in DDraw SetCooperativeLevel | ❌ **Regression (INV-002)** |

---

## What Changed Between Working → Current

Four commits between `b6717ea` and `08b3ed0`. The breaking one is `10199a8`:

| Commit | Description | Impact |
|--------|-------------|--------|
| `10199a8` | Major SDL2 backend refactor: `real_malloc` (dlsym), `rb_host_getenv/setenv`, `rb_alloc_32bit`, heap-allocated `backend_info` | **BREAKS DOOM95** |
| `b8df7e9` | Window fullscreen handling fix | Minor |
| `540a97c` | Thread-local `g_rb_on_host_stack` reentrancy counter | Contributes to crash |
| `08b3ed0` | Audio callback tracking | Minor |

### Files changed by 10199a8+ (755 insertions, 272 deletions)

| File | Key Changes |
|------|-------------|
| `rb_sdl2_priv.h` | `real_malloc/free/calloc` (dlsym-based), `rb_alloc_32bit` (mmap MAP_32BIT), `g_rb_on_host_stack` counter, `rb_host_getenv/setenv/unsetenv` wrappers |
| `rb_init.c` | `getenv` → `rb_host_getenv`, `setenv` → `rb_host_setenv`, stack-allocated `backend_info` → heap-allocated via `rb_host_malloc` |
| `rb_surface.c` | `rb_host_malloc` → `rb_alloc_32bit` (mmap), `rb_host_free` → `rb_free_32bit` (munmap) |
| `rb_window.c` | Fullscreen handling: create-destroy-raise → SetWindowFullscreen-resize-show |
| `rb_audio.c` | Audio callback invoked tracking, playing state check |
| `winmm_doom95.c` | `getenv` → `rb_host_getenv` |
| `ddraw_interface.c` | New DDraw 1.x struct types (36-byte DDSURFACEDESC v1) |
| `crt_mingw.c` | Added WinMain/wWinMain entry point names |

---

## INV-002: DOOM95 SIGSEGV in DDraw After SetCooperativeLevel (Critical Regression)

**Updated after Experiment Round 2** — previous leading hypothesis was WRONG.

### Symptom

```
ddraw: DirectDrawCreate guid=(nil) -> 0xf7f12004
ddraw: SetCooperativeLevel hwnd=0x280 flags=0x190 rb_window=0
CRASH: SIGSEGV
CRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: fallback ESP=0x0057e5a0
```

- `ucontext=0x0000006b` — garbage (sigaltstack setup succeeds but handler doesn't use alt stack)
- `hwnd=0x280` — garbage (DOOM95 reads wrong value from memory, should be `0x43`)
- `rb_window=0` — window not found for hwnd 0x280
- NULL dereference in DDraw code path after SetCooperativeLevel returns rb_window=0

### Previous Leading Hypothesis — **DEBUNKED**

The prior analysis claimed the crash was in `FindFirstFileA`/`FindNextFileA` loop, and that guest code path changes from commit `10199a8` caused DOOM95 to loop and crash.

**Finding: The crash is NOT in FindFirstFileA.** With detailed tracing, the crash occurs in `ddraw_SetCooperativeLevel` when DOOM95 passes a garbage hwnd (`0x280` instead of `0x43`). The window entry is not found, `dd->rb_window=0`, and subsequent DDraw calls (`SetDisplayMode` or `CreateSurface`) dereference `dd->rb_window` → NULL → SIGSEGV.

### What the Working Version Does

```
ddraw: DirectDrawCreate guid=(nil) -> 0xf7f1e004
ddraw: SetCooperativeLevel hwnd=0x43 flags=0x55 rb_window=25
ddraw: SetCooperativeLevel hwnd=0x43 flags=0x51 rb_window=25
ddraw: SetDisplayMode 640x400x8 exclusive=1 rb_window=25
[... rendering works, flip loop, clean ExitProcess ...]
```

The working version passes the correct hwnd (`0x43`), gets the correct window entry (`rb_window=25`), and enters the rendering loop.

### Key Differences: Working vs Current

| | Working | Current |
|---|---|---|
| hwnd passed to SetCooperativeLevel | `0x43` (correct) | `0x280` (garbage) |
| flags | `0x55` / `0x51` | `0x190` |
| rb_window result | `25` (valid) | `0` (not found) |
| Window destroyed before DDraw? | No | Yes (DestroyWindow called) |
| Event pump effect | N/A | None (crash with MY_WINE_NO_EVENT_PUMP=1) |

### Confirmed Facts

- **NULL dereference** (`si_addr=0x00000000`) in DDraw code after `SetCooperativeLevel` returns with `rb_window=0`
- **On guest stack** (`ESP=0x0057e5a0`)
- **sigaltstack setup succeeds** (`rc=0`, mmap at `0x00800000` works) but handler still gets garbage ucontext
- **Event pump thread has NO effect** — crash happens identically with and without `MY_WINE_NO_EVENT_PUMP=1`
- **Guest memory is corrupted** — DOOM95 reads wrong hwnd from memory (`0x280` instead of `0x43`)
- **Correlation with commit 10199a8** — introduces `real_malloc` via `dlsym`, `rb_host_malloc` for `backend_info`, `rb_host_getenv`

### Debunked hypotheses

- H1 (FS-TLS counter): GS-based TLS. DEBUNKED.
- H2 (dlsym on non-standard stack): Pre-warming had zero effect. DEBUNKED.
- H3 (per-TU counter copies): Replacing `__thread` had zero effect. DEBUNKED.
- H4 (signal stack corruption): Magic cookie pristine. DEBUNKED.
- H5 (16-bit register truncation): All 32-bit. DEBUNKED.
- H6 (glibc state from dlsym + guest FS): Routing glibc through host stack didn't fix crash. DEBUNKED.
- **Crash in FindFirstFileA loop**: CRASH IS IN DDraw after SetCooperativeLevel, not in FindFirstFileA. DEBUNKED.

### New Root Cause (updated after Experiment Round 3)

DOOM95 passes a garbage hwnd (`0x280`) to DDraw `SetCooperativeLevel` instead of the correct hwnd (`0x43`). The `get_window_entry(0x280)` returns NULL, so `dd->rb_window=0`. Subsequent DDraw operations dereference `dd->rb_window` → NULL → SIGSEGV.

**Why is the hwnd wrong?** The guest data section (`0x00400000-0x00500000`) is being corrupted by an unknown mechanism. Key findings:

- **EBP misuse by guest:** EBP=`0x1` at `SetCooperativeLevel` entry, EBP=`0x43` at exit — guest code uses EBP as a general-purpose register, not as a frame pointer. my_wine's signal handler "fixes" this, but it's evidence of unusual guest register usage.
- **Heap separation confirmed:** `rb_host_malloc` allocations are at `0x8000000+` (glibc heap). NO overlap with guest memory regions.
- **Crash ESP differs from SetCooperativeLevel ESP:** Crash at `ESP=0x0057e5a0` vs SetCooperativeLevel at `ESP=0x0057ec50` — approximately 4.3KB of guest code runs between them before the crash.
- **Fixed heap at 0x00580000 (MAP_FIXED_NOREPLACE) broke startup:** Forcing the host heap into the guest region caused an earlier crash — proves the host heap was not the problem, but the guest data section itself is being corrupted.
- **Identical DestroyWindow sequence:** Both working and broken versions have the same `DestroyWindow` call pattern — this is not the cause of the different behavior.
- **Resilient DDraw changes implemented:** `SetCooperativeLevel` now has fallback (active/last-created window) and flags normalization. `SetDisplayMode` has similar resilience. dlsym is pre-warmed with global pointers. **The crash persists** — it's a guest code crash, not a stub crash.

**Correlation with commit 10199a8** — introduces `real_malloc` via `dlsym`, heap-allocated `backend_info` (was stack-allocated), and `rb_host_getenv`. The exact mechanism of corruption is still unknown.

**Remaining unknown:** What writes to the guest data section between `CreateWindowExA` (when `hwnd=0x43` is stored correctly) and `SetCooperativeLevel` (when `hwnd=0x280` is read)? The write is happening in ~4.3KB of guest code execution between these two points.

---

## INV-001: SDL2 Samples SIGSEV (Pre-existing)

Identical `ucontext=0x0000006b` crash in both working and current. Not a regression. Likely:
- Signal stack at `0x00800000` conflicts with SDL2 memory allocations
- Or 32-bit signal frame incompatibility with this kernel/glibc

**Not investigated further** — DOOM95 is the primary target, and DOOM95 in the working version avoids this path.

---

## Fix Recommendations (REVISED after Experiment Round 3)

**Priority 1: Resilient DDraw (already implemented, crash persists).** `ddraw_SetCooperativeLevel` now has fallback logic (active/last-created window) and flags normalization. `ddraw_SetDisplayMode` has similar resilience. dlsym pre-warming with global pointers is in place. **However, the crash continues** — it's a guest code crash (corrupted hwnd passed to DDraw), not a stub crash (the stubs now handle bad input gracefully). The resilient stubs prevent the NULL deref cascade, but the root memory corruption remains.

**Priority 2: Detect the corrupting write with mprotect.** Apply `mprotect(PROT_READ | PROT_EXEC)` to the guest data section (`0x00400000-0x00500000`). The first write to the corrupted hwnd location will trigger SIGSEGV with a valid PC, revealing the exact instruction doing the corrupting write. This is the most direct path to root cause.

**Priority 3: Investigate FS/TLS interaction.** Although H1 (FS-TLS counter corruption) was debunked for the counter mechanism, the broader question remains: could `real_malloc` via `dlsym` or heap-allocated `backend_info` be writing into the TLS region, which then bleeds into the adjacent data section? The guest uses FS for TLS in 32-bit mode — if any glibc TLS access overlaps with the guest data section, writes could corrupt the hwnd. Use `mprotect` on the TLS region separately to test.

**Priority 4: Fix sigaltstack signal delivery.** Setup succeeds (`rc=0`, mmap at `0x00800000` works), but the signal handler still gets garbage ucontext (`0x0000006b`). The kernel may not be delivering 32-bit signals on the alt stack correctly, or the 32-bit signal frame format is incompatible with this kernel/glibc version. Debug by checking `/proc/self/stack` after sigaltstack and inspecting the kernel's 32-bit compat signal delivery path.

**Secondary:** The event pump thread has no effect on this crash (verified with `MY_WINE_NO_EVENT_PUMP=1`). The fixed heap experiment (`MAP_FIXED_NOREPLACE` at `0x00580000`) ruled out host heap overlap as the cause.
