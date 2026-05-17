# Graphical Runtime Bug Summary

## Status

Partially fixed on 2026-05-17 for the current sample/runtime matrix.

The graphical harness now runs a lightweight WM alongside `Xvfb` in Docker,
but WM-driven `closewindow` is still an active investigation area rather than a
trusted reference path.

`make run-samples-scenarios` previously passed in this workspace for the
pre-WM matrix:

- console samples: `23 passed, 0 failed, 0 skipped`
- graphical samples: `4 passed, 0 failed, 1 skipped`

## Root Causes That Were Confirmed

### 1. PE32 host-stack bridge clobbered live registers

The PE32 crash was not a sample bug. The i386 `rb_call_on_host_stack()` inline
assembly failed to declare caller-saved register clobbers, so the compiler
could keep live pointers across SDL/glibc calls and reuse corrupted register
state after returning from the host stack.

The practical crash site was `rb_window_refresh_ids()` during
`CreateWindowExA()` window startup.

### 2. USER32 posted messages were routed through SDL's native queue

`PostMessageA()` and `PostQuitMessage()` were pushing Win32 messages through the
SDL event queue. That was the wrong boundary:

- it broke `PeekMessageA()`/`PM_NOREMOVE` behavior
- it made the close path depend on SDL event plumbing instead of USER32 state
- it caused shutdown/message-loop behavior to diverge from Win32 expectations

This is now handled by a USER32-side posted-message queue.

### 3. Graphical scenario shutdown needed an explicit process-interrupt path

For the earlier WM-less `Xvfb` harness, deterministic scenario shutdown was
modeled as an explicit `SIGINT` input action. The graphical samples use that
action where host-driven close semantics are not under test, and the harness
treats `0`, `130`, and `137` as acceptable outcomes for that explicit
interrupt-driven close path.

This is a harness-level operator control path, not a backend-only autoquit hook.

## Implemented Fixes

- fixed the i386 host-stack bridge clobber list in `rb_call_on_host_stack()`
- replaced direct libc string/memory helpers in the USER32 window/message hot
  path with local helpers where needed
- moved USER32 posted-message handling to an internal queue instead of SDL's
  event queue
- kept PE32 startup on the runtime heap path needed by the window stubs
- simplified the graphical harness child-process model so it tracks the actual
  launched runtime process
- added an explicit `sigint` graphical input action and updated the SDL window
  sample scenarios to use it where appropriate
- upgraded the Docker graphical harness to launch `openbox` alongside `Xvfb`
- hardened `closewindow` target selection so the harness prefers the guest
  client window instead of blindly closing the first title match under the WM

## Verification

- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_user32_message_dispatch`
- `docker run --rm -v "$PWD:/project" -w /project my_wine-samples bash scripts/graphical_samples.sh run-container sdl2_window`
- `docker run --rm -v "$PWD:/project" -w /project my_wine-samples bash scripts/graphical_samples.sh run-container sdl2_two_window`
- `make run-samples-scenarios`

## Remaining Notes

- `doom95` remains skipped in the graphical scenario matrix.
- `Alt+F4`, explicit `SIGINT`, and WM-driven `closewindow` cover different
  shutdown paths and should stay distinct in the graphical scenario matrix.
- `closewindow` should still be treated as experimental until Wine and
  `my_wine` agree on a trustworthy WM-driven reference behavior.
- The 64-bit `ExitProcess` hang described in `WIP.md` is still a separate
  runtime problem from the harness upgrade.
