# Samples

This guide covers the sample programs that exercise the my_wine PE loader end-to-end. It catalogs every sample, explains the scenario system, and describes how to add new samples.

---

## What Are Samples?

Samples are **PE executables** (compiled with mingw-w64 via Docker) that test my_wine's loader, dispatcher, and API stubs under realistic conditions. Each sample is a self-contained directory under `samples/` with:

- **Source code** — `.c` files (and optionally `dlls/` for companion DLLs)
- **`sample.info`** — metadata file declaring type, build config, and scenario expectations
- **`applied_inputs.txt`** (optional) — scripted interaction for graphical scenarios

The build system discovers samples by convention rather than a hardcoded registry: every subdirectory of `samples/` that contains sample sources is a candidate. Running `make samples` cross-compiles all samples to PE `.exe` binaries inside a Docker container.

As of this writing, the repository has **47 sample directories** spanning console, graphical, and edge-case scenarios. Many samples have both PE32+ (default) and PE32 (`_32` suffix) variants to validate architecture-specific loader paths.

---

## Sample Categories

### Console Samples

Console samples run inside Docker with the my_wine loader and produce output to stdout/stderr. They are validated by expected exit codes, timeouts, and log output. Run with `make samples SAMPLE=name` or `make run-samples-scenarios SAMPLE=name`.

| Sample | 32-bit variant | What It Tests |
|---|---|---|
| `hello_world` | `hello_world_32` | Minimal PE: `GetStdHandle` + `WriteFile` + `ExitProcess`. The "hello world" of the loader. |
| `file_io` | `file_io_32` | File creation, writing, reading, and deletion via `CreateFileA`, `WriteFile`, `ReadFile`, `DeleteFileA`. |
| `virtual_mem` | `virtual_mem_32` | `VirtualAlloc`, `VirtualFree`, and memory protection changes (`PAGE_READWRITE`, `PAGE_EXECUTE_READWRITE`). |
| `infinite_loop` | — | Infinite loop that exits only on timeout; validates the loader's timeout enforcement. |
| `null_deref` | `null_deref_32` | Deliberate null pointer dereference — expected to crash with exit code 139. Validates exception delivery. |
| `heap_test` | `heap_test_32` | `HeapAlloc`, `HeapFree`, `HeapCreate`, `HeapDestroy`. Validates the heap API stub. |
| `time_test` | `time_test_32` | `GetTickCount`, `QueryPerformanceCounter`, `GetLocalTime`. Validates time-related APIs. |
| `sync_test` | `sync_test_32` | `CreateEvent`, `SetEvent`, `WaitForSingleObject`. Validates event synchronization primitives. |
| `cmdline` | — | `GetCommandLineA`, `GetEnvironmentStringsA`. Validates command-line and environment string delivery. |
| `dll_loader` | `dll_loader_32` | `LoadLibraryA` / `GetProcAddress` / `FreeLibrary`. Tests dynamic DLL loading with a companion `exportlib.dll` built from `dlls/`. |
| `multi_import_32` | — | Many imports across multiple DLLs (`kernel32`, `user32`, `advapi32`, `msvcrt`). Stress-tests import resolution. |
| `multi_syscall` | `multi_syscall_32` | Dense syscall dispatching. Exercises the dispatcher under load. |
| `entry_test_32` | — | PE32 entry point validation. Confirms correct OEP jump for 32-bit binaries. |
| `dispatcher_regs_32` | — | Validates register state preservation across dispatcher calls. |

**Notes:**
- Samples without an explicit `type=` in `sample.info` are treated as console samples by default.
- The `infinite_loop` sample has `exit=124` and `timeout=1` — it is expected to be killed after 1 second.
- The `null_deref` samples have `exit=139` — they are expected to segfault.

### Graphical Samples

Graphical samples create windows and are run under **Xvfb + openbox inside Docker**, with automated interaction via `applied_inputs.txt`. They validate the SDL2, DirectDraw, DirectSound, and window-management stubs.

| Sample | 32-bit variant | What It Tests |
|---|---|---|
| `sdl2_window` | `sdl2_window_32` | Basic SDL2 window creation, event loop, and graceful shutdown via SIGINT. |
| `sdl2_window_sigint` | `sdl2_window_sigint_32` | SDL2 window + `HARNESS:READY` log marker + SIGINT delivery. Tests readiness detection. |
| `sdl2_window_closewindow` | `sdl2_window_closewindow_32` | Window close via the close button (`WM_DELETE_WINDOW`). Validates event dispatch. |
| `sdl2_window_altf4` | `sdl2_window_altf4_32` | Window close via Alt+F4 keyboard shortcut. Validates keyboard event dispatch. |
| `sdl2_window_timeout` | `sdl2_window_timeout_32` | Window that only exits on timeout. Validates timeout handling with windowed apps. |
| `sdl2_two_window` | `sdl2_two_window_32` | Two SDL2 windows created sequentially. Tests window management with multiple windows. |
| `sdl2_two_window_close_chain` | `sdl2_two_window_close_chain_32` | Close one window, verify the other survives. Tests independent window lifecycle. |
| `sdl2_nccreate_reject` | `sdl2_nccreate_reject_32` | Window creation that gets rejected during `WM_NCCREATE`. Validates window creation rejection paths. |
| `input_title_echo` | `input_title_echo_32` | Key events update the window title. Validated by `expect_title` in `applied_inputs.txt`. Tests keyboard input + window title changes. |
| `ddraw_sample` | `ddraw_sample_32` | DirectDraw surface creation and rendering. Tests DDraw API stub. |
| `dsound_sample` | `dsound_sample_32` | DirectSound buffer creation and playback. Tests DSound API stub. |
| `doom95` | — | Full DOOM95 game launch. Skipped by default (`skip=true`); used for manual integration testing. |

**Graphical sample conventions:**
- `sample.info` sets `type=graphical`
- `applied_inputs.txt` defines the scripted interaction sequence
- `ready_log` specifies a log marker the harness waits for before running `applied_inputs.txt`
- `ready_only` means the test passes once the readiness marker is seen (no further interaction)
- `timeout` sets a maximum wall-clock time before the sample is terminated

### Edge-Case Samples

Some samples target specific loader edge cases rather than API coverage:

| Sample | What It Tests |
|---|---|
| `null_deref` / `null_deref_32` | Segfault handling and exit code 139 |
| `infinite_loop` | Timeout-based termination (exit 124) |
| `multi_import_32` | Massive import table resolution across many DLLs |
| `dispatcher_regs_32` | Register preservation across the dispatcher boundary |
| `entry_test_32` | PE32 entry point handling |

---

## Running Samples

### Build All Samples

```bash
make samples
```

This cross-compiles all sample `.c` files to PE `.exe` binaries using the `my_wine-samples` Docker image (mingw-w64 toolchain). It also unpacks any registered archives (e.g., `doom95.zip`).

### Build a Single Sample

```bash
make samples SAMPLE=hello_world
make samples SAMPLE=ddraw_sample
```

### Build Graphical Sample Binaries

```bash
make graphical-samples SAMPLE=sdl2_window
```

### Run Console Sample Scenarios

```bash
make run-samples-scenarios SAMPLE=hello_world    # run one
make run-samples-scenarios                         # run all console samples
```

Console samples are run directly inside Docker via `scripts/samples.sh run`. The harness reads `sample.info` for expected `exit=` codes and `timeout=` values.

### Run Graphical Sample Scenarios

```bash
make run-samples-scenarios SAMPLE=sdl2_window     # run one
make run-samples-scenarios                         # run all (console + graphical)
```

Graphical samples run inside Docker with Xvfb + openbox. `scripts/graphical_samples.sh` drives window interaction via `xdotool` using the commands in `applied_inputs.txt`.

### Unified Runner

`make run-samples-scenarios` (with no `SAMPLE=` arg) runs **all** console and graphical sample scenarios in sequence. Console samples run first, then graphical. Any failure causes a non-zero exit code.

---

## sample.info Format

Every sample directory contains a `sample.info` file. It is a simple key=value format (one per line). The harness parses it convention-driven — unknown keys are silently ignored.

### Fields

| Key | Required | Description |
|---|---|---|
| `type` | no | `graphical` for windowed samples. Omit or leave empty for console. |
| `arch` | no | `32` for PE32-only samples. Defaults to 64-bit (PE32+) if omitted. |
| `exit` | no | Expected exit code. Omit for exit code 0. `124` for timeout kill, `139` for segfault. |
| `timeout` | no | Maximum seconds before the sample is terminated. |
| `window_title` | graphical | Window title the harness searches for (via `xdotool`). |
| `window_width` | graphical | Expected window width. |
| `window_height` | graphical | Expected window height. |
| `ready_log` | no | Log marker (`HARNESS:READY ...`) the harness waits for before running `applied_inputs.txt`. |
| `ready_window_timeout` | no | Seconds to wait for the window to appear (for `ready_only` samples). |
| `ready_only` | no | If `true`, the test passes as soon as `ready_log` is seen (no `applied_inputs.txt` replay). |
| `expect_title_timeout` | no | Seconds to wait for `expect_title` assertions to match. |
| `close_timeout` | no | Seconds to wait for clean window close after interaction. |
| `archive` | no | Archive name (e.g., `doom95.zip`) to unpack from `samples/unpacked/`. |
| `libs` | no | Comma-separated library names required at link time (e.g., `ddraw`). |
| `skip` | no | `true` or `1` to skip the sample in automated runs. |
| `skip_reason` | no | Human-readable reason for skipping. |

### Examples

**Minimal console sample (`hello_world/sample.info`):**
```
(no content needed — defaults apply)
```

**Graphical sample with readiness detection (`sdl2_window/sample.info`):**
```
type=graphical
window_title=Test
window_width=320
window_height=200
ready_log=HARNESS:READY window
```

**Graphical sample with interaction (`sdl2_window_altf4/sample.info`):**
```
type=graphical
window_title=AltF4 Window
window_width=320
window_height=200
```
With `applied_inputs.txt`:
```
status initial
focus
sleep 100
altf4
```

**Edge case sample (`null_deref/sample.info`):**
```
exit=139
```

**Skipped sample (`doom95/sample.info`):**
```
type=graphical
archive=doom95.zip
window_title=DOOM95
timeout=60
skip=true
skip_reason=out of scope — not a standard sample
```

---

## Scenario System

The scenario system provides deterministic, reproducible interaction with graphical samples through `applied_inputs.txt` files.

### File Location

`samples/<sample_name>/applied_inputs.txt`

### Command Reference

| Command | Syntax | Description |
|---|---|---|
| `sleep` | `sleep MS` | Wait for MS milliseconds |
| `focus` | `focus` | Focus the target window |
| `key` | `key KEY` | Simulate a key press (e.g., `key w`, `key BackSpace`, `key Alt`) |
| `type` | `type TEXT` | Type a string of text |
| `click` | `click X Y` | Left-click at window-relative coordinates |
| `mousemove` | `mousemove X Y` | Move mouse to window-relative coordinates |
| `expect_title` | `expect_title TEXT` | Assert the window title matches `TEXT`. Used by `input_title_echo` to verify key event processing. |
| `altf4` | `altf4` | Send Alt+F4 (window close via keyboard) |
| `sigint` | `sigint` | Send SIGINT to the sample process |
| `closewindow` | `closewindow` | Simulate clicking the window close button (via `WM_DELETE_WINDOW`) |
| `status` | `status LABEL` | Emit a status label for logging purposes. No action; just marks a point in the scenario. |

### How It Works

1. The graphical harness starts the sample inside Docker (Xvfb + openbox)
2. If `ready_log` is set, the harness waits for that log line before proceeding
3. If `ready_only` is `true`, the test passes immediately after `ready_log` is seen
4. Otherwise, `applied_inputs.txt` is replayed line-by-line using `xdotool`
5. After all commands execute, the harness waits for the process to exit (or times out)
6. Exit code is checked against `sample.info`

### Example: `input_title_echo/applied_inputs.txt`

```
# Verify key events by observing deterministic window title updates.
focus
sleep 100
key a
key b
key c
expect_title Input Echo: ABC
key BackSpace
expect_title Input Echo: AB
key z
key 9
expect_title Input Echo: ABZ9
altf4
```

This scenario:
1. Focuses the window
2. Types "abc" and verifies the title updates to "Input Echo: ABC"
3. Presses BackSpace and verifies "Input Echo: AB"
4. Types "z9" and verifies "Input Echo: ABZ9"
5. Closes with Alt+F4

---

## Adding a New Sample

### Console Sample

1. **Create the directory:**
   ```bash
   mkdir samples/my_sample
   ```

2. **Write the C source:**
   ```c
   #include <windows.h>
   int main(void) {
       /* Use Windows APIs via the loader */
       ExitProcess(0);
       return 0;
   }
   ```

3. **Create `sample.info`** (optional for console samples):
   ```
   # Leave empty for defaults (exit=0, type=console)
   # Or specify non-zero exit / timeout:
   exit=0
   timeout=10
   ```

4. **Build and run:**
   ```bash
   make samples SAMPLE=my_sample
   make run-samples-scenarios SAMPLE=my_sample
   ```

### Graphical Sample

1. **Create the directory:**
   ```bash
   mkdir samples/my_graphical_sample
   ```

2. **Write the C source** (must include SDL2 initialization + `HARNESS:READY` log):
   ```c
   #include <SDL2/SDL.h>
   #include <stdio.h>
   int main(void) {
       SDL_Init(SDL_INIT_VIDEO);
       SDL_Window *win = SDL_CreateWindow("My Title", 100, 100, 320, 200, 0);
       printf("HARNESS:READY my-sample\n");
       /* ... event loop ... */
       SDL_DestroyWindow(win);
       SDL_Quit();
       return 0;
   }
   ```

3. **Create `sample.info`:**
   ```
   type=graphical
   window_title=My Title
   window_width=320
   window_height=200
   ready_log=HARNESS:READY my-sample
   ```

4. **Create `applied_inputs.txt`:**
   ```
   status initial
   focus
   sleep 100
   sigint
   ```

5. **Build and run:**
   ```bash
   make graphical-samples SAMPLE=my_graphical_sample
   make run-samples-scenarios SAMPLE=my_graphical_sample
   ```

### Sample with Companion DLLs

If your sample loads a DLL at runtime (via `LoadLibraryA`):

1. **Create the sample directory with a `dlls/` subdirectory:**
   ```
   samples/my_dll_sample/
   ├── my_dll_sample.c
   ├── sample.info
   └── dlls/
       ├── exportlib.c
       └── exportlib.def
   ```

2. **The build system** automatically compiles everything in `dlls/` as shared libraries and copies the resulting `.dll` files to the parent sample directory alongside the `.exe`.

3. **See `dll_loader/` and `dll_loader_32/`** for working examples.

### PE32 Variant

To add a 32-bit variant of an existing sample:

1. **Create the `_32` directory** (e.g., `samples/my_sample_32/`)
2. **Copy the `.c` source** (or create a new one if architecture-specific code is needed)
3. **Create `sample.info`** with `arch=32`:
   ```
   arch=32
   ```
   (Copy any other fields from the 64-bit `sample.info` if they differ.)
4. **Copy `applied_inputs.txt`** from the 64-bit variant if applicable

### Sample with Archive (e.g., unpacked game)

For samples that require external files (like DOOM95):

1. **Register the archive** in `sample.info`:
   ```
   type=graphical
   archive=doom95.zip
   window_title=DOOM95
   timeout=60
   ```

2. **The unpack script** (`scripts/unpack_samples.sh`) reads `samples/<name>/sample.info` and extracts `samples/<name>/<archive>` to `samples/unpacked/<name>/` when `make samples` runs.

---

## Sample Inventory

The complete list of sample directories:

```
cmdline
ddraw_sample          ddraw_sample_32
dispatcher_regs_32
dll_loader            dll_loader_32
doom95
dsound_sample         dsound_sample_32
entry_test_32
file_io               file_io_32
heap_test             heap_test_32
hello_world           hello_world_32
infinite_loop
input_title_echo      input_title_echo_32
multi_import_32
multi_syscall         multi_syscall_32
null_deref            null_deref_32
sdl2_nccreate_reject  sdl2_nccreate_reject_32
sdl2_two_window       sdl2_two_window_32
sdl2_two_window_close_chain  sdl2_two_window_close_chain_32
sdl2_window           sdl2_window_32
sdl2_window_altf4     sdl2_window_altf4_32
sdl2_window_closewindow  sdl2_window_closewindow_32
sdl2_window_sigint    sdl2_window_sigint_32
sdl2_window_timeout   sdl2_window_timeout_32
sync_test             sync_test_32
time_test             time_test_32
virtual_mem           virtual_mem_32
unpacked
```

### Quick Stats

| Category | Count |
|---|---|
| Total sample directories | 47 |
| Console samples | ~21 |
| Graphical samples (type=graphical) | 21 |
| PE32-only (`_32` suffix) | 23 |
| Skipped | 1 (`doom95`) |

### PE32/PE32+ Pairs

Samples with matching 32-bit and 64-bit variants exercise both loader paths. The `_32` suffix convention marks PE32-only builds:

| Base | PE32 Variant |
|---|---|
| hello_world | hello_world_32 |
| file_io | file_io_32 |
| virtual_mem | virtual_mem_32 |
| null_deref | null_deref_32 |
| heap_test | heap_test_32 |
| time_test | time_test_32 |
| sync_test | sync_test_32 |
| dll_loader | dll_loader_32 |
| sdl2_window | sdl2_window_32 |
| sdl2_window_sigint | sdl2_window_sigint_32 |
| sdl2_window_closewindow | sdl2_window_closewindow_32 |
| sdl2_window_altf4 | sdl2_window_altf4_32 |
| sdl2_window_timeout | sdl2_window_timeout_32 |
| sdl2_two_window | sdl2_two_window_32 |
| sdl2_two_window_close_chain | sdl2_two_window_close_chain_32 |
| sdl2_nccreate_reject | sdl2_nccreate_reject_32 |
| ddraw_sample | ddraw_sample_32 |
| dsound_sample | dsound_sample_32 |
| input_title_echo | input_title_echo_32 |
| multi_syscall | multi_syscall_32 |

---

## Troubleshooting

### "Docker build failed"

Rebuild the Docker image explicitly: `make build-docker-image`

### Graphical sample hangs

- Check that `ready_log` in `sample.info` matches the actual log output (case-sensitive)
- Verify `window_title` matches the window title exactly (used by `xdotool search`)
- Increase `timeout` if the sample needs more wall-clock time

### Console sample fails with unexpected exit code

- Check `sample.info` for the expected `exit=` value
- For segfault tests, use `exit=139`; for timeout kills, use `exit=124`

### New sample not building

- Ensure the `.c` file is in the sample directory (not a subdirectory)
- For DLL-producing samples, put `.c` and `.def` files in `dlls/` subdirectory
- Run `make samples SAMPLE=my_sample` to isolate build errors for one sample
