# Graphical Harness WIP

## Current State

We added new graphical sample coverage for these cases on both 64-bit and 32-bit:

- close on external `Alt+F4`
- close on external `SIGINT`
- close on sleep timeout
- two windows `A` and `B` then close `A` followed by `B`
- close on external close-window event

We also added shutdown breadcrumbs in the window closing path so we can debug without going straight to `gdb`.

Current close-path breadcrumbs:

- `rb_event: Alt+F4 -> WM_CLOSE`
- `user32: DispatchMessageA close-path`
- `user32: DefWindowProcA WM_CLOSE`
- `user32: DestroyWindow begin`
- `user32: PostQuitMessage`
- `rb_window: destroy`
- `user32: DestroyWindow end`
- `kernel32: ExitProcess`

## Validation Summary

## Runtime Matrix

Status as of 2026-05-17.

| Scenario | Wine 64 | Wine 32 | my_wine 64 | my_wine 32 |
|---|---|---|---|---|
| `sdl2_window_altf4` | pass | pass | pass | pass |
| `sdl2_window_sigint` | pass | pass | unknown | pass |
| `sdl2_window_timeout` | pass | pass | pass | pass |
| `sdl2_window_closewindow` | fail | fail | unknown | unknown |
| `sdl2_two_window_close_chain` | fail | fail | unknown | unknown |

Interpretation:

- `Wine` now has stable reference coverage for `Alt+F4`, `SIGINT`, and self-close timeout on both 64-bit and 32-bit.
- `Wine` still fails the external `closewindow` and two-window close-chain scenarios on both 64-bit and 32-bit.
- `my_wine32` is in better shape for the currently validated close paths.
- `my_wine64` no longer reproduces the post-`ExitProcess` hang on the validated keyboard/self-close paths.
- `my_wine64` `SIGINT` has not been revalidated in this pass.
- `my_wine` `closewindow` and `two_window_close_chain` should not be treated as meaningful targets until the `Wine` reference problem is understood.

### Wine reference

These now pass under `Wine`:

- `sdl2_window_altf4`
- `sdl2_window_altf4_32`
- `sdl2_window_sigint`
- `sdl2_window_sigint_32`
- `sdl2_window_timeout`
- `sdl2_window_timeout_32`

These still fail under the upgraded Docker/Xvfb/`openbox` environment:

- `sdl2_window_closewindow`
- `sdl2_window_closewindow_32`
- `sdl2_two_window_close_chain`
- `sdl2_two_window_close_chain_32`

Observed behavior:

- they time out under `Wine`
- the harness now runs `Xvfb` plus `openbox`
- after `closewindow`, the visible Wine window disappears
- `openbox` remains alive
- the Wine process itself remains running until the outer harness timeout

This means the old "no real WM" explanation is no longer sufficient. The
remaining reference issue is now specifically about process lifetime after a
WM-driven close request.

### my_wine

`my_wine32`:

- `Alt+F4` passes
- sleep-timeout self close passes
- `SIGINT` passes

`my_wine64`:

- `Alt+F4` now passes
- sleep-timeout self close now passes
- the previous hang after `kernel32: ExitProcess` was fixed by making
  `NtTerminateProcess(HANDLE_CURRENT_PROCESS, status)` terminate the whole
  process with `exit_group`, not just the current thread with `exit`

This means the main remaining runtime issue is not the `my_wine64`
post-`ExitProcess` path anymore. The remaining unresolved area is the
`Wine` reference behavior for WM-driven close requests.

## Why The Docker Environment Needs A WM

The current graphical harness is running under `Xvfb` without a lightweight window manager.

That is enough for:

- showing windows
- querying them with `xdotool`
- sending focus and key events

That is not enough for a reliable reference test of:

- host/window-manager close button behavior
- ICCCM/EWMH style close requests
- multi-window close ordering driven by external window close actions

Without a WM, the "close on closewindow event" scenario is not a strong reference against `Wine`, which means it is also not a strong reference against `my_wine`.

## Proposed Upgrade

Upgrade the Docker graphical environment so the harness launches:

- `Xvfb`
- a lightweight window manager
- then the guest process

Likely candidates:

- `openbox`
- `fluxbox`
- `twm` as a minimal fallback

Preferred direction:

- start with `openbox` if package size and startup behavior are reasonable
- fall back to `twm` only if we want the smallest possible dependency set

## Progress Update On 2026-05-17

Completed in code:

1. Extended the Docker image used by `scripts/graphical_samples.sh` to install `openbox`.
2. Updated the harness startup so it launches the WM after `Xvfb` and waits until it is ready.

Re-validation result under the upgraded harness with `GRAPHICAL_RUNTIME=wine`:

- `sdl2_window_closewindow`: still times out
- `sdl2_window_closewindow_32`: still times out
- `sdl2_two_window_close_chain`: still times out
- `sdl2_two_window_close_chain_32`: still times out

Important new observation from verbose validation:

- after `closewindow`, the guest-visible Wine window disappears
- `openbox` remains alive
- the Wine process itself remains running until the outer harness timeout

That means the WM upgrade is in place and the old "WM-less Xvfb" limitation is
no longer the blocker. The remaining reference problem is now narrower:

- WM-driven close requests are reaching the window system strongly enough to
  remove the visible window
- but the process does not terminate cleanly afterward under `Wine` in this
  Docker/Xvfb/WM environment

## Handoff For The Next Agent

Treat the work as two separate tracks and do not mix them prematurely.

### Track 1: `Wine` reference hangs after WM-driven close

Goal:

- explain why `closewindow` removes the visible window under `Wine` but leaves
  the process alive until harness timeout

What is already known:

- the harness now launches `openbox` and waits for WM readiness
- this reproduces on both 64-bit and 32-bit Wine sample variants
- the failure affects both `sdl2_window_closewindow*` and
  `sdl2_two_window_close_chain*`
- this is no longer attributable to WM-less `Xvfb`

Suggested first steps:

1. Run the failing `Wine` closewindow scenarios with `GRAPHICAL_VERBOSE=1`.
2. Inspect post-close process state inside the container:
   - `ps`
   - Wine process tree shape
   - whether the main process is stuck, zombie-free, or waiting on a child
3. Determine whether the harness should track a different Wine process boundary
   for graphical completion, or whether Wine itself is the unstable reference.
4. Re-check whether `xdotool windowclose` is the right WM-level stimulus for
   the reference behavior we want, now that a WM exists.

Decision rule:

- do not use these `Wine` failures to justify `my_wine` runtime changes until
  the reference model is trustworthy

### Additional Findings From 2026-05-17 Follow-Up

Harness fixes completed during follow-up:

- `GRAPHICAL_VERBOSE=1` is now forwarded into the Docker container, so verbose
  runs actually emit status snapshots instead of silently downgrading to
  non-verbose behavior.
- the launched graphical runtime now runs under its own session with `setsid`
  and the harness logs the tracked process group on verbose/failure paths
- cleanup now targets the whole launched process group instead of a single PID

Observed with `GRAPHICAL_VERBOSE=1` under `GRAPHICAL_RUNTIME=wine`:

- `sdl2_window_closewindow`:
  - after `closewindow`, the guest-visible titled window disappears
  - only the X root window and `Openbox` remain visible
  - the tracked `.exe` process is still alive
  - `explorer.exe /desktop` is also still alive in the same process group
- `sdl2_two_window_close_chain`:
  - after `closewindow`, `Window A` disappears
  - `Window B` remains visible
  - the tracked `.exe` process and `explorer.exe /desktop` both stay alive

Observed with `GRAPHICAL_VERBOSE=1` under `GRAPHICAL_RUNTIME=my_wine`:

- `sdl2_window_closewindow` shows the same high-level shape as Wine:
  - the titled host window disappears
  - only the X root window and `Openbox` remain visible
  - `my_wine64` itself remains alive until harness timeout
- no close-path breadcrumbs are emitted on stderr for this path:
  - no `rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE`
  - no `user32: DispatchMessageA close-path`
  - no `user32: DefWindowProcA WM_CLOSE`

Interpretation:

- for `my_wine`, the current runtime gap is earlier than `DestroyWindow()` or
  `ExitProcess()`: the external WM-close action is not reaching the guest
  close/message path at all
- for `Wine`, the two-window case also shows that `xdotool windowclose` is not
  yet a trustworthy reference for guest-visible multi-window `WM_CLOSE`
  semantics in the current desktop/WM setup
- the harness diagnostics are now good enough to distinguish:
  - process still alive with no visible guest window
  - process still alive with secondary guest windows remaining
  - whether helper processes such as `explorer.exe /desktop` are still in scope

### Track 2: `my_wine64` termination hang after `ExitProcess`

Status:

- resolved on 2026-05-17 for the validated repro cases
- verified with:
  - `GRAPHICAL_RUNTIME=my_wine bash scripts/graphical_samples.sh run sdl2_window_altf4`
  - `GRAPHICAL_RUNTIME=my_wine bash scripts/graphical_samples.sh run sdl2_window_timeout`

Goal:

- keep the fix stable and revalidate any remaining `my_wine64` close paths as needed

What is already known:

- the root cause was syscall semantics, not message routing
- `ExitProcess` called `NtTerminateProcess(HANDLE_CURRENT_PROCESS, status)`
- the 64-bit handler used `sys_exit`, which only kills the current thread
- SDL/host runtime helper threads could keep the process alive after the main
  guest thread exited
- switching that handler to `exit_group` fixed the observed harness hang

Suggested first steps:

1. Revalidate `sdl2_window_sigint` on `my_wine64`.
2. If future shutdown hangs appear, check for any other code paths that still
   use `INLINE_SYSCALL_EXIT` where Windows semantics require process-wide
   termination.

### Do Not Regress

- keep the close-path breadcrumbs in place
- do not remove the `openbox` harness upgrade
- do not blur the distinction between operator-driven `sigint`, guest-visible
  `Alt+F4`, and WM-driven `closewindow`
- do not assume a `my_wine` bug from a `Wine` reference case that is still not
  understood

## Success Criteria

The Docker graphical harness implementation should be considered upgraded when all of the following are true:

- `GRAPHICAL_RUNTIME=wine` passes `Alt+F4`, `SIGINT`, and sleep-timeout as before
- `GRAPHICAL_RUNTIME=wine` also passes the external close-window case
- `GRAPHICAL_RUNTIME=wine` can exercise the two-window close-order scenario in a reproducible way
- the harness no longer depends on WM-less `Xvfb` behavior for external close semantics

## Notes For The Next Pass

- Keep the close-path breadcrumbs in place for now.
- Avoid reintroducing formatted debug logging directly inside guest-facing shutdown code unless we are sure the path is host-safe.
- The 64-bit `ExitProcess` hang remains a separate runtime problem and should be debugged after the WM upgrade, not mixed into the `closewindow` reference work.
