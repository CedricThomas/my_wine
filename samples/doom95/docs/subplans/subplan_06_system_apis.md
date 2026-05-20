# Subplan 6: System APIs

## Status

Implemented for the current import surface.

Existing code already covers part of what this subplan used to own, including
pieces of:

- `CreateFileA` / `ReadFile` / `WriteFile` / `DeleteFileA`
- `LoadLibraryA` / `GetProcAddress` / `GetModuleHandleA` / `FreeLibrary`
- `CreateEventA` / `SetEvent` / `ResetEvent` / `WaitForSingleObject`
- `CreateMutexA` / `ReleaseMutex`
- `VirtualAlloc` / `VirtualFree`
- `GetEnvironmentStringsA`

So this subplan should no longer be treated as “implement all remaining
kernel32”.

This pass added the remaining Doom95 startup-facing import coverage in the
current tree:

- `kernel32`: module/file/TLS/resource/time/process helpers used by the EXE's
  real import table
- `winmm`: timer, joystick, and no-op MIDI stream stubs
- `advapi32`: in-memory registry shim for the Doom95 config path
- `dplay`: ordinal `#1` / `DPCreate` graceful stub
- import-table and ordinal-table wiring for those exports

## Goal

Fill only the Doom95-specific system API gaps that remain after the current
runtime and import table are accounted for.

## Remaining Work

### 6.1 Re-audit Doom95 imports first
- [x] Unpack Doom95 and record the actual imported DLL/function set
- [x] Confirmed `DOOM95.EXE` imports:
  - `KERNEL32.dll` / `KERNEL32.DLL`
  - `ADVAPI32.dll`
  - `WINMM.dll`
  - `GDI32.dll`
  - `USER32.dll` / `USER32.DLL`
  - `DPLAY.dll` (ordinal `#1`)
  - `DDRAW.dll`
  - `DSOUND.dll`
- [x] Confirmed kernel32 imports include the functions previously called out as
  likely gaps, including `CreateThread`, `DosDateTimeToFileTime`,
  `FileTimeToDosDateTime`, `FileTimeToLocalFileTime`,
  `LocalFileTimeToFileTime`, `FindNextFileA`, `GetCPInfo`,
  `GetEnvironmentStrings`, `GetFileAttributesA`, `GetFileSize`,
  `GetFileTime`, `GetModuleFileNameA`, `GetTimeZoneInformation`,
  `GetVersion`, `SetFilePointer`, `TlsAlloc`, `TlsFree`, and `TlsSetValue`.
- [x] Compare that confirmed list against `src/loader/import_table.c` and `src/loader/ordinal_table.c`
- [x] Trim this plan further if some imports are already fully implemented

### 6.2 Kernel32 delta only
- [x] Implement only missing kernel32 functions that Doom95 demonstrably imports or reaches at runtime
- [x] Prioritize:
  - `GetModuleFileNameA`
  - file search/enumeration helpers if imports confirm them
  - TLS functions beyond the current stubbed `TlsGetValue`
  - PE resource helpers if dialogs or string resources require them
- [x] Avoid creating a new `kernel32_extended.c` if the work fits better into
  existing `src/msvcrt/kernel32_*.c` modules

### 6.3 WINMM minimal layer
- [x] `timeGetTime`
- [x] `joyGetNumDevs`
- [x] `joyGetDevCapsA`
- [x] `joyGetPosEx`
- [x] `midiOutGetNumDevs`
- [x] `midiOutPrepareHeader`
- [x] `midiOutReset`
- [x] `midiOutSetVolume`
- [x] `midiOutUnprepareHeader`
- [x] `midiStreamClose`
- [x] `midiStreamOpen`
- [x] `midiStreamOut`
- [x] `midiStreamPause`
- [x] `midiStreamProperty`
- [x] `midiStreamRestart`

### 6.4 ADVAPI32 minimal registry layer
- [x] in-memory registry for the Doom95 config path
- [x] `RegCreateKeyA`
- [x] `RegOpenKeyA`
- [x] `RegCloseKey`
- [x] `RegQueryValueExA`
- [x] `RegSetValueExA`

### 6.5 DPLAY bootstrap stub
- [x] add ordinal `#1` / `DPCreate`
- [x] return a stable no-op object or graceful failure path that does not abort startup

### 6.6 Import-table integration
- [x] add only the DLL exports actually needed for Doom95 startup
- [x] keep `ordinal_table.c` changes narrow and explicit

## Result

The Doom95 import set now resolves through the maintained runtime instead of
failing on missing `kernel32`/`winmm`/`advapi32`/`dplay` coverage. The next
blocker is no longer missing system API exposure; it is the real guest runtime
crash captured in Subplan 7.

## Scope Reduction

Defer anything not proven necessary by the import audit or first runtime traces,
including:

- broad console API completion
- comprehensive threading beyond current runtime needs
- generic filesystem helpers that Doom95 does not import
- deep kernel32 parity work unrelated to Doom95 boot
