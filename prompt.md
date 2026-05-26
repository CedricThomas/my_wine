# Cleanup Workflow Prompt

Use this workflow for `my_wine` cleanup and refactor work.

## Prompt

Work on the current `my_wine` codebase cleanup as far as you can in one pass.

Goals:

- reduce global state
- isolate Doom95-specific code from generic runtime code
- remove x64/x32 tricks where safe, or at least make the boundary explicit
- split oversized files
- split functions with multiple responsibilities
- tighten ownership and subsystem contracts
- remove code smells, inconsistent naming, and mixed responsibilities
- improve architecture clarity without changing behavior

Execution rules:

1. Coverage first, refactor second, docs third.
2. Prefer small behavior-preserving extractions over broad rewrites.
3. Update the audit docs as you go, not only at the end.
4. After every meaningful code change, run the full verification loop.
5. Keep going autonomously until you hit a real blocker.
6. Do not use git operations for comparison or rollback. If comparison is needed, use plain `diff` against `refs/working`.
7. Treat code in `src/` as source of truth; markdown docs may be stale unless updated in the same pass.

Verification loop after every meaningful change:

- `make run-tests`
- `./my_wine32 samples/hello_world_32/hello_world_32.exe`
- `./my_wine32 samples/entry_test_32/entry_test_32.exe`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Expected Doom95 result in the agent environment:

- it should reach WAD discovery/startup
- it should then time out in the expected loop
- ALSA / fluidsynth warnings are expected environment noise and are not failures by themselves

Working style:

- start by reading the current hotspot file and the audit backlog
- choose the safest high-value extraction or cleanup
- explain briefly what you are changing before editing
- keep `pe32`, `pe32+`, host/guest, and syscall/libc boundaries explicit
- avoid directory reshuffles until file-level ownership is cleaner

Required documentation updates:

- update `audit/cleanup-backlog.md` whenever a milestone step advances
- update any other `audit/` document if the architecture understanding changed
- note what was verified after the change

Output expectations:

- make the code changes
- report what changed
- report verification results
- report the next best cleanup target
