- read `audit/cleanup-backlog.md` first
- read the target file or current top-goal file
- choose the highest architectural-payoff behavior-preserving move
- prefer one architecture-significant seam per pass
- update the audit docs
- use fast targeted compile/test checks while editing
- run the full verification loop once before stopping
- stop after one verified architectural move unless explicitly asked to continue

Project goals:

- reduce global state
- isolate Doom95-specific code from generic runtime code
- keep PE32 vs PE32+ boundaries explicit
- virtualize or contain host-process state
- split oversized files and mixed-responsibility helpers only when that clarifies ownership
- tighten ownership and subsystem contracts
- improve architecture clarity without changing behavior

Execution rules:

1. Coverage first, refactor second, docs third.
2. Prefer architectural leverage over local neatness.
3. Tackle global state, host/guest boundary leakage, and subsystem ownership before cosmetic file splits.
4. Treat `src/` as source of truth; historical markdown may be stale.
5. Keep `audit/cleanup-backlog.md` short and forward-looking; do not append per-pass verification logs there.
6. Use git history and the final work report for exact command results instead of repeating them in backlog docs.
7. Update other `audit/` docs if ownership, sequencing, or architecture understanding changes.
8. During editing, prefer targeted verification for the touched boundary, then run the full verification loop once after the architectural move is complete.
9. Keep going autonomously until one architectural move is implemented and verified, or a real blocker is reached.

Verification loop:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Expected Doom95 result:

- reach WAD discovery/startup
- then time out in the expected loop with exit status `124`
- ALSA / fluidsynth warnings are expected environment noise and not failures by themselves

Working style:

- start by deciding whether the named file still exposes a high-payoff architectural seam
- if not, move to the next hotspot from `audit/cleanup-backlog.md`
- explain briefly what you are changing before editing
- keep host/guest, syscall/libc, and PE32/PE32+ boundaries explicit
- prefer state containment, contract cleanup, or policy extraction over directory reshuffles
- accept moderate local churn when it materially improves ownership or boundary clarity

Output expectations:

- make the code changes
- report what moved and what stayed
- report which architectural risk was reduced
- report exact verification results
- report the next best cleanup target
- do not duplicate that report into `audit/cleanup-backlog.md`
