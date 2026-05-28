- read `audit/cleanup-backlog.md` first
- read the target file or current top-goal file
- choose the safest high-value behavior-preserving extraction
- prefer one small extraction only
- update the audit docs
- use fast targeted compile/test checks while editing
- run the full verification loop once before stopping
- stop after one verified extraction unless explicitly asked to continue

Project goals:

- reduce global state
- isolate Doom95-specific code from generic runtime code
- keep PE32 vs PE32+ boundaries explicit
- split oversized files and mixed-responsibility helpers
- tighten ownership and subsystem contracts
- improve architecture clarity without changing behavior

Execution rules:

1. Coverage first, refactor second, docs third.
2. Prefer narrow helper/file extraction over broad rewrites.
3. Treat `src/` as source of truth; historical markdown may be stale.
4. Keep `audit/cleanup-backlog.md` short and forward-looking; do not append per-pass verification logs there.
5. Use git history and the final work report for exact command results instead of repeating them in backlog docs.
6. Update other `audit/` docs if ownership, sequencing, or architecture understanding changes.
7. During editing, prefer narrow verification such as targeted object builds or focused tests; run the full verification loop once after the extraction is complete.
8. Keep going autonomously until one extraction is implemented and verified, or a real blocker is reached.

Verification loop:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Expected Doom95 result:

- reach WAD discovery/startup
- then time out in the expected loop with exit status `124`
- ALSA / fluidsynth warnings are expected environment noise and not failures by themselves

Working style:

- start by deciding whether the named file still has a narrow safe seam
- if not, move to the next hotspot from `audit/cleanup-backlog.md`
- explain briefly what you are changing before editing
- keep host/guest, syscall/libc, and PE32/PE32+ boundaries explicit
- avoid directory reshuffles until file ownership is cleaner

Output expectations:

- make the code changes
- report what moved and what stayed
- report exact verification results
- report the next best cleanup target
- do not duplicate that report into `audit/cleanup-backlog.md`
