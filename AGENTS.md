# my_wine — Project Context for AI Agents

---

## Docs Status

**⚠️ Most documentation in `docs/`, `audit/`, `samples/doom95/docs/` is stale.** Treat markdown files as reference only — actual implementation in `src/` and the live branch history (`git log`) are the source of truth.

---

## Working Reference

**`refs/working`** — git worktree pinned to branch `feat/doom95-system-apio-and-integration` (commit `0c77ba0`, "rendering doom95 works"). Use for direct file comparison with the current branch:

```bash
diff refs/working/src/file.c src/file.c          # single file
diff -rq refs/working/src/backend/sdl2/ src/backend/sdl2/  # directory
```

No `git` commands allowed, no commit, no push — plain `diff` on two directories. No branch confusion.

---

## Running DOOM95

### Launch (requires real display + audio)

```bash
./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE
```

The game enters a rendering loop at ~100% CPU. SDL auto-selects Wayland→X11 for video and PipeWire for audio.

### Launch with suppressed audio (no sound card)

```bash
env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE
```

### Unpacking Samples

```bash
./scripts/unpack_samples.sh                  # unpack all registered samples
./scripts/unpack_samples.sh doom95           # unpack a specific one
```

Samples live under `samples/<name>/sample.info` which declares the `archive=` ZIP file. Unpacked output goes to `samples/unpacked/<name>/`.

---

## Agent Environment Restrictions

When running from the agent harness, these differences apply vs. your terminal:

| Restriction | Impact |
|---|---|
| **No ALSA sound card** | All audio init floods `cannot find card '0'` errors; use `SDL_AUDIODRIVER=dummy` to silence |
| **Missing 32-bit ALSA modules** | `libasound_module_pcm_pulse.so`, `jack.so`, `oss.so`, etc. absent from `/usr/lib32/alsa-lib/` |
| **Fluidsynth → PulseAudio fallback** | `Using PulseAudio driver` + `warning: Failed to set high priority` |
| **DISPLAY=:1 (real display)** | Spawns GUI windows on your desktop — uncontrolled from the agent; no Xvfb for headless |
| **Separate mount namespace** | `mnt:[4026534261]` vs. your `mnt:[4026531835]` — project cwd is still accessible |

The PE loader and game loop work identically — the blockers are only around audio hardware and display isolation.

---

## Build & Run

```bash
make my_wine my_wine32 my_wine64        # build loader backends
make run-tests                           # native unit tests
make samples SAMPLE=hello_world          # cross-compile one sample via Docker
./my_wine32 samples/hello_world_32/hello_world_32.exe
```

### Key Directories

| Path | Purpose |
|---|---|
| `src/` | Loader source (dispatcher, stubs, syscalls, TEB/PEB setup) |
| `scripts/` | Unpack, run-samples, screenshot, test harness, dispatcher codegen |
| `samples/` | Cross-compiled PE sample binaries + `unpacked/` for game archives |
| `build32/` | 32-bit build output |
| `docs/` | Stale reference docs (see above) |
