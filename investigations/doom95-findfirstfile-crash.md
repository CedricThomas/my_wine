# INV-002: DOOM95 SIGSEGV at FindFirstFileA — Revised Investigation

**Date:** 2026-05-26 (updated after Experiment Round 2)
**Crash:** `SIGSEGV` with `si_addr=0x00000000` on guest stack in DDraw after `SetCooperativeLevel`
**Regression:** Working at `b6717ea`, broken from `10199a8` onward
**Crash site:** **DDraw code path after `SetCooperativeLevel` returns `rb_window=0`** — guest memory corruption passes garbage hwnd `0x280` instead of `0x43`. Exact PC unknown (sigaltstack setup succeeds but handler still gets garbage ucontext).

---

## Crash Output (Ground Truth)

```
GetFileAttributesA('DOOM2.WAD')
...
FindFirstFileA -> 'DOOM1.WAD'
FindNextFileA: host call returned, result=0
FindFirstFileA -> 'DOOM1.WAD'
...
CRASH: SIGSEGV
CRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: fallback ESP=0x0057e5a0, EBP=0x0057e5a0, signal_ret=0x00003042
```

---

## H1 (FS-TLS counter corruption) — DEBUNKED

32-bit TLS uses GS, not FS. `rb_call_on_host_stack` only switches FS. GS never modified. The `g_rb_on_host_stack` counter works correctly.

---

## Crash Analysis

### Confirmed facts

| Fact | Evidence |
|------|----------|
| NULL dereference | `si_addr=0x00000000` |
| On guest stack | `ESP=0x0057e5a0` (range `0x00500000-0x00580000`) |
| After stub calls succeed | Debug prints show `FindNextFileA` returns 0 successfully |
| sigaltstack failed | `ucontext=0x0000006b` (garbage) |
| Crash in guest code | Crash happens AFTER stub functions return |
| Deterministic | Same ESP, same `signal_ret` every run |

### Debunked claims

- **`rb_sdl_window_get_surface_call`** — address/stack/call-chain all inconsistent
- **`wildcard_match_ci`** — `signal_ret` is random guest stack data (EBP=ESP, no valid frame)
- **Mixed allocator corruption** — my_wine heap and glibc heap are fully separated

### Debunked hypothesis: glibc + guest FS

**Experiment:** Route all glibc calls in `FindNextFileA` AND `FindFirstFileA` through `rb_call_on_host_stack` so they execute with host FS instead of guest FS.

**Result:** CRASH STILL HAPPENS. `FindNextFileA` returns `result=0` successfully via the host callback, and the crash happens in guest DOOM95 code afterward.

**Conclusion:** The `dlsym` + guest FS theory is **DEBUNKED**. The crash is in guest code, not in glibc functions.

---

## Leading hypothesis: Guest code path change

The working version doesn't crash because DOOM95 takes a different code path. Something in `rb_init` (commit `10199a8`) changes behavior that causes DOOM95 to loop (`FindFirstFileA` → `FindNextFileA` → `FindFirstFileA` → ... → crash).

Candidates:
- `rb_host_getenv` returning different values than `getenv`
- `rb_host_malloc` returning addresses that differ from stack allocation
- Event pump thread interfering with game timing
- SDL initialization changing system state that the game depends on

---

## Debunked hypotheses (confirmed)

- **H1 (FS-TLS counter)**: GS-based TLS, FS irrelevant. DEBUNKED.
- **H2 (dlsym on non-standard stack)**: Pre-warming dlsym had zero effect. DEBUNKED.
- **H3 (per-TU counter copies)**: Replacing `__thread` with `static volatile` had zero effect. DEBUNKED.
- **H4 (signal stack corruption)**: Magic cookie pristine. DEBUNKED.
- **H5 (16-bit register truncation)**: All 32-bit registers. DEBUNKED.
- **H6 (glibc state from dlsym + guest FS)**: Routing glibc calls through host stack did not fix crash. **DEBUNKED.**

---

## Experiment Round 2: Crash Site Identified — DDraw SetCooperativeLevel (2026-05-26)

### Finding 1: sigaltstack setup SUCCEEDS

Contrary to prior belief that sigaltstack "failed", the setup actually works:

```
DEBUG: sigaltstack: stack_t={sp=0x00800000, size=65536, flags=0, sizeof=12}
DEBUG: sigaltstack: mmap returned 0x00800000
DEBUG: sigaltstack: rc=0
```

- `mmap` at `0x00800000` succeeds
- `sigaltstack()` syscall returns `rc=0` (success)
- Stack is correctly configured with `ss_flags=0` (SS_ONSTACK not set — stack is available)

**But at crash time, ucontext is STILL garbage** (`ucontext=0x0000006b`). Despite successful setup, the signal handler is not using the alt stack. This is a 32-bit compatibility signal delivery issue on this kernel — the alt stack is registered but the kernel delivers the signal on the guest stack anyway, or the 32-bit signal frame is incompatible.

**Implication:** We still cannot get a reliable crash PC/ucontext. All crash analysis must rely on debug prints and guest memory inspection, not signal handler context.

### Finding 2: Crash is NOT at FindFirstFileA — it's at DDraw SetCooperativeLevel

The title of this investigation ("crash at FindFirstFileA") was based on incomplete data. With enhanced debug logging in the DDraw and DSound stubs, the **actual crash site is `ddraw_SetCooperativeLevel`** when `rb_window=0`.

#### Working version trace:
```
user32: CreateWindowExA success hwnd=0x6
user32: CreateWindowExA success hwnd=0x14
user32: CreateWindowExA success hwnd=0x43
dsound: SetCooperativeLevel hwnd=0x43 level=0x2
ddraw: DirectDrawCreate guid=(nil) -> 0xf7f1e004
ddraw: SetCooperativeLevel hwnd=0x43 flags=0x55 rb_window=25
ddraw: SetCooperativeLevel hwnd=0x43 flags=0x51 rb_window=25
ddraw: SetDisplayMode 640x400x8 exclusive=1 rb_window=25
[... rendering works, flip loop, clean ExitProcess ...]
```

#### Current version trace (baseline):
```
user32: CreateWindowExA success hwnd=0x6
user32: CreateWindowExA success hwnd=0x14
user32: CreateWindowExA success hwnd=0x43
dsound: SetCooperativeLevel hwnd=0x43 level=0x2
[... DSound buffer creation ...]
user32: DestroyWindow begin
rb_window: destroy
user32: DestroyWindow end
ddraw: DirectDrawCreate guid=(nil) -> 0xf7f12004
ddraw: SetCooperativeLevel hwnd=0x280 flags=0x190 rb_window=0
CRASH: SIGSEGV
CRASH: si_addr=0x00000000, ucontext=0x0000006b
CRASH: fallback ESP=0x0057e5a0
```

#### Current version trace (MY_WINE_NO_EVENT_PUMP=1):
Identical crash pattern — the event pump thread has **no effect** on this crash.

### Key differences explained:

| Aspect | Working | Current | Impact |
|--------|---------|---------|--------|
| Window lifecycle | 3 windows created, none destroyed before DDraw | Window `0x43` destroyed before DDraw calls | DDraw can't find the window |
| SetCooperativeLevel hwnd | `0x43` (valid, created window) | `0x280` (garbage, no such window) | `get_window_entry` returns NULL → `rb_window=0` |
| SetCooperativeLevel flags | `0x55` (DDSCL_NORMAL) then `0x51` (DDSCL_EXCLUSIVE \| DDSCL_FULLSCREEN) | `0x190` (garbage) | Invalid cooperative level |
| rb_window result | `25` (valid rb_window struct) | `0` (NULL) | NULL deref on subsequent DDraw calls |

### Finding 3: Guest memory is corrupted

DOOM95 is reading `hwnd=0x280` from guest memory where `hwnd=0x43` should be. The window `0x43` was correctly created (both versions show `CreateWindowExA success hwnd=0x43`), but:

1. In the working version, `hwnd=0x43` persists and is correctly passed to DDraw
2. In the current version, the window is destroyed (`user32: DestroyWindow`) before DDraw tries to use it, AND the hwnd read by DDraw is `0x280` — a completely different value

This suggests **guest memory corruption**: DOOM95 reads the wrong hwnd from its own memory. The value `0x280` is not the original `0x43`.

### Finding 4: NULL dereference is in DDraw code path after SetCooperativeLevel

When `ddraw_SetCooperativeLevel` returns with `dd->rb_window=0` (because `get_window_entry(0x280)` returns NULL), subsequent DDraw calls (`SetDisplayMode`, `CreateSurface`, etc.) dereference `dd->rb_window` → NULL dereference → SIGSEGV.

### Correlation with commit 10199a8

The breaking commit `10199a8` introduced:
- `real_malloc` via `dlsym` (heap allocation through dynamically-loaded libc)
- `rb_host_malloc` for `backend_info` (heap-allocated vs stack-allocated)
- `rb_host_getenv` (different env lookup path)

**Hypothesis:** The `real_malloc`/`dlsym` or heap-allocated `backend_info` is corrupting guest memory that DOOM95 relies on for its window handle. The heap layout change or memory interleaving between the real_malloc heap and the guest heap is overwriting the hwnd stored by DOOM95.

---

## Experiment Round 3: Guest Stack Analysis & Memory Corruption Deep Dive (2026-05-26)

### Key Findings

#### 1. EBP corruption is guest behavior, not host corruption

At `SetCooperativeLevel` entry: `EBP=0x1` — clearly wrong, the guest is using EBP as a general-purpose register rather than as a frame pointer. At exit: `EBP=0x43`. my_wine's "fix" of EBP was actually just restoring the guest's own value.

**Conclusion:** The guest code itself uses EBP as a general-purpose register. my_wine's handling of it is incidental, not a root cause.

#### 2. rb_host_malloc allocations — NO overlap with guest memory

All `rb_host_malloc` allocations land in the glibc heap region at `0x8000000+`. This is completely outside the guest address space:

| Region | Range | Purpose |
|--------|-------|---------|  
| Guest code+data | `0x00400000 - 0x00500000` | DOOM95 image (code, data, BSS) |
| Guest heap/stack | `0x00500000 - 0x00580000` | Guest stack at `0x0057e5a0`, heap below |
| Host heap (glibc) | `0x00800000+` | `rb_host_malloc` allocations via `real_malloc` |

**NO overlap.** The host heap cannot directly overwrite guest memory via `rb_host_malloc`.

#### 3. Guest data section is being corrupted by unknown mechanism

The guest data section (`0x00400000 - 0x00500000`) contains DOOM95's global variables. The hwnd `0x43` (stored correctly after `CreateWindowExA`) is being overwritten to `0x280` by the time `SetCooperativeLevel` reads it. Something between `CreateWindowExA` returning and `SetCooperativeLevel` is writing to guest data memory.

#### 4. Crash ESP vs SetCooperativeLevel ESP — ~4.3KB of guest code runs

- `SetCooperativeLevel` ESP: `0x0057ec50`
- Crash ESP: `0x0057e5a0`
- Difference: `0x6b0` (~4.3KB of guest stack consumed)

This means **~4.3KB of guest code executes** between the end of `SetCooperativeLevel` stub and the crash. The crash is not inside the stub itself — it's in guest DOOM95 code that runs *after* the stub returns.

#### 5. Fixed heap at 0x00580000 (MAP_FIXED_NOREPLACE) — broke guest startup

Attempted to force host allocations into the region *above* the guest stack using `MAP_FIXED_NOREPLACE` at `0x00580000`. This caused a **much earlier crash** — the guest failed during startup before reaching DDraw. This suggests the guest may be writing into this region during initialization, or the allocation interfered with guest memory layout expectations.

#### 6. Working version has identical DestroyWindow call sequence

Comparing detailed traces between working and current: **both versions call `DestroyWindow`** in the same sequence. The timing difference (window destroyed before DDraw in current) is not due to different call patterns. This means the DestroyWindow timing itself is **not the cause** — it's the *consequence* of guest memory state being different.

### Code Changes Implemented

**Resilient `ddraw_SetCooperativeLevel`:**
- When `get_window_entry(hwnd)` returns NULL, fall back to the last-created or active window entry
- Normalize flags to known valid values if garbage is detected
- Prevents the NULL `rb_window=0` cascade

**Resilient `ddraw_SetDisplayMode`:**
- Same fallback pattern — validates `dd->rb_window` before dereferencing
- Returns error gracefully if window context is invalid

**dlsym pre-warming with global pointers:**
- Cache `dlsym` results in global function pointers at init time
- Avoids repeated `dlsym` calls during game execution
- Eliminates potential FS/TLS interaction during hot paths

### Result After Changes

The crash **still occurs**, but with different parameters. The resilient DDraw changes prevent the immediate NULL deref cascade, but the underlying guest memory corruption persists. The crash moves to a different location in guest code — confirming this is a **guest code crash due to memory corruption**, not a stub implementation bug.

---

## Next Steps

### Remaining Unknown: What corrupts guest data section?

The guest data section (`0x00400000 - 0x00500000`) stores DOOM95's global variables including the window handle. Between `CreateWindowExA` returning `hwnd=0x43` and `SetCooperativeLevel` reading it as `0x280`, something writes the wrong value. Host allocations (`rb_host_malloc`) are at `0x8000000+` — no overlap.

**Candidates:**
- **FS/TLS interaction:** Even though FS switching is controlled, guest TLS writes via `FS:[offset]` could corrupt adjacent data if TLS region overlaps with data section
- **Buffer overflow in guest code:** The working version may have different data layout (different commit's binary) that happens not to overlap with a write that the current binary does
- **Write through dispatcher:** Some intercepted API call may be writing to the wrong offset in guest memory due to a parameter offset mismatch

**Recommended approach:** Use `mprotect` on the guest data section to trigger a SIGSEGV on any write, then trace which instruction is doing the corrupting write. This is the most direct way to identify the source of memory corruption.

### Priority 1: Fix sigaltstack signal delivery
Setup succeeds (`rc=0`) but handler still gets garbage ucontext. Investigate 32-bit compat signal delivery on this kernel — is the `SA_ONSTACK` flag being set correctly? Is the kernel ignoring the alt stack for compat mode?

### Priority 2: Understand the DestroyWindow timing
In the working version, window `0x43` persists. In the current version, it's destroyed before DDraw. Why is DOOM95 calling `DestroyWindow` on its own window? Is this a side effect of `rb_host_getenv` returning different values?

### Priority 3: mprotect guest data section for write detection
Apply `mprotect(PROT_READ | PROT_EXEC)` to the guest data section (`0x00400000 - 0x00500000`) temporarily. The first write will trigger SIGSEGV with a valid PC, revealing the exact instruction corrupting the hwnd.

---

## Files Changed Between Working and HEAD

| Commit | Files changed |
|--------|--------------|
| `b6717ea → 10199a8` | `rb_sdl2_priv.h` (added real_malloc/free/calloc, g_rb_on_host_stack, rb_call_on_host_stack rework, rb_host_getenv/setenv/unsetenv) |
| `10199a8 → b8df7e9` | `rb_init.c` (getenv→rb_host_getenv, stack→heap backend_info), `rb_surface.c` (rb_host_malloc→rb_alloc_32bit) |
| `b8df7e9 → 540a97c` | `rb_window.c` (fullscreen rewrite), `rb_audio.c` (callback tracking) |
| `540a97c → 08b3ed0` | `winmm_doom95.c` (getenv→rb_host_getenv), `ddraw_interface.c`, `crt_mingw.c` |

**The breaking change is in commit `10199a8`** — the introduction of `rb_call_on_host_stack()` with `real_malloc()` via `dlsym`.
