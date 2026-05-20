# DOOM95 Debug Log

## Scope

This file is a dated runtime progression log for `samples/unpacked/doom95/DOOM95.EXE`.
It is narrower than the phase subplans: the goal here is to record what was
actually executed, what changed, and which issues blocked forward progress.

## 2026-05-20

### Starting state

- `samples/doom95/sample.info` still marks the sample as:
  - `skip=true`
  - `skip_reason=unsupported`
- The Doom95 archive was present locally as `samples/doom95/doom95.zip`.
- The tree already contained Doom95-oriented work in:
  - Watcom CRT bootstrap
  - dialog / GDI shims
  - winmm / advapi32 / dplay support
  - PE32 loader updates

### Step 1: unpack and inspect the sample

- Ran the unpack flow for Doom95.
- Confirmed `samples/unpacked/doom95/DOOM95.EXE` exists and is a `PE32` GUI executable.
- Confirmed the media payload is present, including `DOOM1.WAD`, `DOOM95.MID`,
  `DOOMCFG.DLL`, and `DOOMLNCH.DLL`.

### Step 2: first direct repro inside the sandbox

- `./my_wine samples/unpacked/doom95/DOOM95.EXE`
- `./my_wine32 samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- both exited immediately with status `159`
- no guest-facing crash log was produced

Issue faced:

- this looked like a low-level PE32 backend failure rather than a Doom95-only
  application bug

### Step 3: broaden the repro to known-good PE32 samples

- `./my_wine samples/hello_world_32/hello_world_32.exe`
- `./my_wine32 samples/hello_world_32/hello_world_32.exe`

Observed result:

- known-good PE32 samples also failed with the same `159` exit in the sandbox

Conclusion:

- the immediate blocker was not specific to Doom95
- sandboxed execution was masking real PE32 runtime behavior

### Step 4: rebuild the tree before deeper runtime work

- `make all`

Observed result:

- build failed before runtime analysis could continue

Issues faced:

- `src/msvcrt/launcher_kernel32_stubs.c` failed under `-Werror`
  because of truncation warnings from `strncpy`
- new launcher/resource stubs also exposed test-link failures when building
  helper binaries that do not link the full `user32` / module registry stack

Fixes applied:

- replaced the warning-prone copies in
  [launcher_kernel32_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_kernel32_stubs.c)
  with bounded `snprintf` copies
- added weak fallback `user32` globals in
  [launcher_ui_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_ui_stubs.c)
  so test binaries link cleanly without the real window module
- added a weak `find_module_by_addr()` fallback in
  [resource_win32.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/resource_win32.c)
  so non-loader tests still link

Result:

- `make all` succeeded again

### Step 5: rerun PE32 samples outside the sandbox

- `./my_wine samples/hello_world_32/hello_world_32.exe`

Observed result:

- the sample ran correctly and printed `Hello from 32-bit my_wine!`

Conclusion:

- the earlier `159` failures were environment-related sandbox failures, not
  proof that the rebuilt PE32 backend was broken

### Step 6: rerun Doom95 outside the sandbox

- `./my_wine samples/unpacked/doom95/DOOM95.EXE`
- `env MY_WINE_DEBUG_LEVEL=2 ./my_wine samples/unpacked/doom95/DOOM95.EXE`

Observed result:

- Doom95 no longer died immediately
- SDL initialized
- Watcom runtime compatibility seeding executed
- imports resolved successfully across:
  - `KERNEL32`
  - `ADVAPI32`
  - `WINMM`
  - `GDI32`
  - `USER32`
  - `DPLAY`
  - `DDRAW`
  - `DSOUND`
- `DOOMLNCH.DLL` loaded successfully
- `_Launcher@4` was resolved successfully via `GetProcAddress`
- the process then exited through `ExitProcess` without reaching gameplay

Conclusion:

- PE32 loading and the basic Doom95 startup/import surface are working
- the active blocker moved to the launcher/UI path

### Step 7: investigate the launcher path

Runtime finding:

- `DOOM95.EXE` dynamically loads `DOOMLNCH.DLL`
- it looks up `_Launcher@4`
- the launcher decides whether the main executable continues into game startup

Issue faced:

- the current dialog/property-sheet stubs are sufficient for import exposure,
  but not sufficient to emulate the launcher's real "start game" path
- the real launcher performs side effects beyond returning a simple success code

### Step 8: test direct launcher bypasses

Tried approach:

- temporarily intercepted `_Launcher@4` in `GetProcAddress`
- returned a forced success path from a host shim

Observed result:

- Doom95 progressed further than the clean exit path
- it then crashed with `SIGSEGV`
- gdb showed `EIP=0x00000000`, which means execution eventually jumped through
  a null function pointer or callback slot

Additional finding:

- the incoming launch-data struct passed to `_Launcher@4` was initially zeroed
- forcing only the 3 returned words was not enough
- the real launcher establishes additional global state that the main EXE later
  depends on

Action taken:

- reverted the launcher interception experiment
- kept only the safe build/link fixes described above

### Step 9: fix the false "launcher already running" path

Runtime finding:

- `_Launcher@4` first calls `FindWindowA("Doom for Windows 95 Launcher", NULL)`
- the previous stub ignored both arguments and always returned the active window

Issue faced:

- this made the launcher think an instance already existed
- it took the "focus existing window and return 0" path, which caused the main
  EXE to exit cleanly through `ExitProcess`

Fix applied:

- changed `FindWindowA` to perform a real class/title match across live
  `user32` windows instead of returning the active window unconditionally

Observed result:

- the launcher no longer short-circuits through the fake "already running"
  branch

### Step 10: fix empty-path file attribute handling

Runtime finding:

- the launcher probes `GetFileAttributesA("")` as part of its WAD-selection
  preflight
- the previous `GetFileAttributesA` path helper resolved `""` to the EXE
  directory and reported success because that directory exists

Issue faced:

- Windows does not treat an empty filename probe as a valid existing path here
- the false success made the launcher preflight return `0`, which again caused
  `_Launcher@4` to return early without creating UI

Fix applied:

- changed `GetFileAttributesA` to fail fast for `NULL` or empty strings instead
  of resolving them to the current directory

Observed result:

- the launcher now advances past the WAD preflight stage instead of exiting
  immediately

### Step 11: support dynamic `LoadLibraryA()` for builtin Win32 DLLs

Runtime finding:

- after the preflight fix, the launcher called `LoadLibraryA("user32.dll")`
- the previous loader only succeeded for mapped PE modules on disk, not for the
  builtin stub DLL surface already exposed through import resolution

Issue faced:

- the dynamic `user32.dll` load failed even though the static `USER32` import
  surface had already resolved successfully

Fix applied:

- added synthetic builtin-module handles for the maintained stub DLL set
- taught:
  - `LoadLibraryA`
  - `GetModuleHandleA`
  - `GetProcAddress`
  - `FreeLibraryA`
  to recognize those builtin handles and resolve exports through the existing
  import table

Observed result:

- the launcher now successfully performs late-bound `user32.dll` symbol
  lookups such as `MessageBoxA`, `GetActiveWindow`, and `GetLastActivePopup`

### Step 12: unblock the next runtime crash in the launcher error/UI path

Runtime findings:

- the launcher next reached a VC-runtime/UI path that dynamically queried:
  - `MessageBoxA`
  - `GetActiveWindow`
  - `GetLastActivePopup`
- `GetLastActivePopup` was previously missing from the `user32` surface
- the old `MessageBoxA` stub also assumed the incoming text pointers were safe
  host C strings, which is not guaranteed on this path

Fixes applied:

- added a minimal `GetLastActivePopup()` stub and export mapping
- hardened `MessageBoxA()` to avoid dereferencing guest pointers while logging

Observed result:

- the launcher advances far enough to invoke `MessageBoxA`
- after that it still crashes, but now at a later point:
  - `SIGSEGV`
  - `EIP=0x00000006`

Conclusion:

- the active blocker is no longer the early launcher-return path
- the current failure is a later launcher/runtime UI-path crash after several
  late-bound `user32` calls have already succeeded

### Current status at end of day

What works:

- the tree builds cleanly with `make all`
- PE32 samples run correctly outside the sandbox
- `DOOM95.EXE` now:
  - maps successfully
  - executes through Watcom bootstrap preparation
  - resolves its full current import surface
  - loads `DOOMLNCH.DLL`
  - reaches the launcher contract
  - gets past the fake "already running" branch
  - gets past the empty-path WAD preflight bug
  - performs late-bound builtin `user32.dll` loading and symbol lookup

What is still broken:

- Doom95 still does not transition from launcher/UI setup into a stable
  gameplay startup path
- the current real launcher path now crashes later in the VC-runtime/UI path
  with `EIP=0x00000006`

Primary issues faced:

- sandbox execution returned misleading `159` failures for all PE32 samples
- the branch had compile blockers unrelated to the final Doom95 runtime issue
- several `user32` behaviors were too stubbed to let the real launcher progress
- builtin Win32 DLLs worked for static imports but not late-bound
  `LoadLibraryA`/`GetProcAddress`
- the launcher contract involves side effects, not just a boolean result

## Next useful debugging target

The next high-value fix is still in the launcher/UI path, but it is now later
than before:

- inspect the call chain immediately after the current `MessageBoxA` path
- identify why execution falls through to `EIP=0x00000006`
- decide whether the right fix is:
  - another missing late-bound `user32` export / return contract, or
  - a more faithful launcher/dialog success path that avoids the runtime error
    branch entirely

Most relevant files:

- [user32_dialog.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_dialog.c)
- [launcher_ui_stubs.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/launcher_ui_stubs.c)
- [kernel32_module.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_module.c)
- [kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
- [subplan_07_integration.md](/home/arzad/Playground/projects/my_wine/samples/doom95/docs/subplans/subplan_07_integration.md)

### Step 13: call DLL entry points on dynamic `LoadLibraryA()` loads

Runtime finding:

- `DOOMLNCH.DLL` was being mapped and import-resolved successfully
- but `load_dll()` never invoked the DLL entry point
- the launcher DLL depends on its process-attach path to initialize internal
  runtime state, including TLS-backed CRT state

Issue faced:

- without `DllMain(DLL_PROCESS_ATTACH)`, later launcher CRT code reached:
  - `TlsGetValue(0xffffffff)`
  - Visual C++ runtime abort `R6016`
  - `not enough space for thread data`

Fix applied:

- added DLL entry-point invocation for loaded PE modules on:
  - process attach during `load_dll()`
  - process detach during `FreeLibraryA()`
- wrapped the guest DLL entry invocation so 32-bit guest calls do not leak
  callee-saved register corruption back into host loader code

Observed result:

- `DOOMLNCH.DLL` now performs real TLS initialization
- gdb confirmed:
  - `TlsAlloc()` is called
  - slot `0` is allocated
  - `TlsSetValue(0, ...)` succeeds
- the previous `R6016` launcher/runtime abort path no longer reproduces first

### Step 14: unblock launcher CRT attach by implementing charset conversion stubs

Runtime finding:

- the launcher DLL process-attach path next called:
  - `GetEnvironmentStringsW()`
  - `WideCharToMultiByte()`
- the previous conversion stubs always returned `0`

Issue faced:

- the launcher CRT environment bootstrap treated the failed conversion as
  attach-time initialization failure
- `DllMain(DLL_PROCESS_ATTACH)` returned `FALSE`

Fix applied:

- implemented minimal ANSI/Unicode conversion behavior for:
  - `WideCharToMultiByte()`
  - `MultiByteToWideChar()`
- the implementation is intentionally narrow:
  - ASCII / low-byte pass-through
  - size-query support when destination buffers are `NULL`

Observed result:

- the launcher process-attach path advances past the earlier environment-block
  conversion failure
- `DllMain` now gets further than the previous immediate `FALSE` return path

### Step 15: satisfy the late-bound `kernel32!IsTNT` probe

Runtime finding:

- after the attach-path fixes, the launcher DLL dynamically probes:
  - `GetModuleHandleA("kernel32.dll")`
  - `GetProcAddress(..., "IsTNT")`
- this is an old compatibility probe used by some Win32 modules

Issue faced:

- the launcher expects a non-`NULL` function pointer for that probe path
- returning `NULL` immediately led to a null indirect call and `EIP=0x00000000`

Fix applied:

- exposed a minimal builtin `kernel32!IsTNT()` stub that returns `0`

Observed result:

- `GetProcAddress(..., "IsTNT")` now resolves successfully
- the launcher attach path advances further than the earlier null-function jump

### Current status after Step 15

What newly works:

- dynamic DLL loads now execute real PE `DllMain` attach/detach hooks
- launcher TLS state is initialized instead of using slot `0xffffffff`
- the earlier VC runtime `R6016` thread-data abort is no longer the first
  blocker
- late-bound `kernel32!IsTNT` lookup no longer returns `NULL`

What is still broken:

- Doom95 still crashes during the launcher DLL attach/startup path
- the current crash is earlier than gameplay and later than the old
  launcher-UI error path
- the latest gdb state shows:
  - `EIP=0x00000000`
  - crash occurs inside `DOOMLNCH.DLL` attach-time startup after the `IsTNT`
    probe has already resolved

Most likely next target:

- inspect the indirect-call sites in `DOOMLNCH.DLL` immediately after the
  `IsTNT`-gated attach helper path
- determine which callback/global function slot is still `NULL`
- decide whether the next missing contract is:
  - another optional Win32 callback/export the launcher assumes exists, or
  - a launcher-internal function table/global that is still not initialized

### Step 16: fix `GetCPInfo()` guest ABI layout

Runtime finding:

- after `IsTNT`, the launcher CRT advanced into codepage / lowio startup
- `gdb` trace showed:
  - `GetProcAddress("IsTNT")`
  - `TlsAlloc()`
  - `TlsSetValue()`
  - `GetEnvironmentStringsW()`
  - `WideCharToMultiByte()`
  - `GetStartupInfoA()`
  - `GetStdHandle()`
  - `GetFileType()`
  - `SetHandleCount(32)`
- the next step entered the CRT codepage init helper and then still destabilized

Issue faced:

- the guest-visible `CPINFO` layout in `GetCPInfo()` was wrong
- the stub exposed fields in the wrong order and with the wrong widths, so the
  launcher CRT read garbage while building its byte-classification tables

Fix applied:

- corrected the `CPINFO` struct layout in
  [kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
  to match Win32:
  - `MaxCharSize`
  - `DefaultChar[2]`
  - `LeadByte[12]`

Observed result:

- the codepage helper no longer consumes a malformed `CPINFO` buffer
- attach-time execution progressed far enough to expose a second ABI bug

### Step 17: fix `GetStartupInfoA()` stack overwrite

Runtime finding:

- targeted `gdb` breakpoints around the launcher lowio init showed:
  - `SetHandleCount()` returned to `0x6000dc23`
  - the tail of the lowio init function then reached `0x6000dc2a`
- before the fix, that function returned to `0x00000000`
- stack inspection showed the saved caller return address had been clobbered

Issue faced:

- our `STARTUPINFOA` declaration was not Win32-compatible
- it used the wrong field set/order and had size `72` bytes instead of the
  expected 32-bit Win32 size `68`
- `GetStartupInfoA()` zeroed `sizeof(STARTUPINFOA)`, which overwrote four bytes
  past the launcher's local `0x44`-byte stack buffer

Fix applied:

- corrected the `STARTUPINFOA` definition in
  [kernel32.h](/home/arzad/Playground/projects/my_wine/include/kernel32.h)
  to the real Win32 32-bit layout:
  - `lpReserved`
  - `lpDesktop`
  - `lpTitle`
  - `dwX` / `dwY`
  - `dwXSize` / `dwYSize`
  - `dwXCountChars` / `dwYCountChars`
  - `dwFillAttribute`
  - `dwFlags`
  - `wShowWindow`
  - `cbReserved2`
  - `lpReserved2`
  - `hStdInput` / `hStdOutput` / `hStdError`

Observed result:

- `GetStartupInfoA()` no longer smashes the launcher's stack frame
- the lowio init function now returns to the correct caller address
  (`0x6000acb3`) instead of `0x00000000`
- the earlier attach-time `SIGSEGV` no longer reproduces first

### Current status after Step 17

What newly works:

- the launcher survives the earlier attach/startup crash sequence
- `DOOMLNCH.DLL` now gets through:
  - TLS slot allocation
  - environment-string conversion
  - startup-info probing
  - lowio/codepage initialization that previously corrupted the stack
- bounded reruns no longer terminate immediately with `SIGSEGV`

Observed runtime state:

- `./my_wine samples/unpacked/doom95/DOOM95.EXE` now stays alive instead of
  dying in attach-time startup
- a bounded `timeout` run had to be killed manually rather than crashing first
- this means the active blocker moved again: away from CRT attach corruption and
  toward whatever the launcher does next once its startup path stays resident

What is still broken / unknown:

- Doom95 still is not confirmed to reach gameplay
- the current stable state after the attach fixes is not yet automated:
  - the process remains alive
  - but this log does not yet prove successful launcher completion or game
    startup

Most likely next target:

- inspect what the resident launcher is now waiting on after lowio/env startup
- determine whether the next gap is:
  - message-loop / dialog behavior,
  - file-selection / launcher UI contract,
  - or another late runtime callback path that is now reachable only because
    the prior stack corruption is fixed
