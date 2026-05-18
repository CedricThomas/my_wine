# Subplan 6: System APIs

## Status

Partially complete already, but the original plan is badly overstated.

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
- [ ] Compare that confirmed list against `src/loader/import_table.c` and `src/loader/ordinal_table.c`
- [ ] Trim this plan further if some imports are already fully implemented

### 6.2 Kernel32 delta only
- [ ] Implement only missing kernel32 functions that Doom95 demonstrably imports or reaches at runtime
- [ ] Prioritize:
  - `GetModuleFileNameA`
  - file search/enumeration helpers if imports confirm them
  - TLS functions beyond the current stubbed `TlsGetValue`
  - PE resource helpers if dialogs or string resources require them
- [ ] Avoid creating a new `kernel32_extended.c` if the work fits better into
  existing `src/msvcrt/kernel32_*.c` modules

### 6.3 WINMM minimal layer
- [ ] `timeGetTime`
- [ ] `joyGetNumDevs`
- [ ] `joyGetDevCapsA`
- [ ] `joyGetPosEx`
- [ ] `midiOutGetNumDevs`
- [ ] `midiOutPrepareHeader`
- [ ] `midiOutReset`
- [ ] `midiOutSetVolume`
- [ ] `midiOutUnprepareHeader`
- [ ] `midiStreamClose`
- [ ] `midiStreamOpen`
- [ ] `midiStreamOut`
- [ ] `midiStreamPause`
- [ ] `midiStreamProperty`
- [ ] `midiStreamRestart`

### 6.4 ADVAPI32 minimal registry layer
- [ ] in-memory registry for the Doom95 config path
- [ ] `RegCreateKeyA`
- [ ] `RegOpenKeyA`
- [ ] `RegCloseKey`
- [ ] `RegQueryValueExA`
- [ ] `RegSetValueExA`

### 6.5 DPLAY bootstrap stub
- [ ] add ordinal `#1` / `DPCreate`
- [ ] return a stable no-op object or graceful failure path that does not abort startup

### 6.6 Import-table integration
- [ ] add only the DLL exports actually needed for Doom95 startup
- [ ] keep `ordinal_table.c` changes narrow and explicit

## Scope Reduction

Defer anything not proven necessary by the import audit or first runtime traces,
including:

- broad console API completion
- comprehensive threading beyond current runtime needs
- generic filesystem helpers that Doom95 does not import
- deep kernel32 parity work unrelated to Doom95 boot
