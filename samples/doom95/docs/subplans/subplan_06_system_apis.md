# Subplan 6: System APIs

**Goal**: Complete all remaining kernel32 functions + winmm + advapi32 + dplay.

**Outcome**: DOOM95 init completes without crashing (file I/O, memory, threading, timing, registry, MIDI, joystick all work).

---

## Tasks

### 6.1 File I/O (kernel32_extended.c)
- [ ] `CreateFileA` → `fopen()` → wrap `FILE*` in handle manager
  - Parse `dwDesiredAccess`: `GENERIC_READ` → "rb", `GENERIC_WRITE` → "wb"
  - Parse `dwCreationDisposition`: `OPEN_EXISTING`, `CREATE_ALWAYS`, `CREATE_NEW`
- [ ] `CloseHandle` → extend existing: check handle type → `fclose()` for files, `free()` for other types
- [ ] `ReadFile` → `fread()` on wrapped `FILE*`
- [ ] `WriteFile` → `fwrite()` on wrapped `FILE*`
- [ ] `SetFilePointer` → `fseek()` (map `FILE_BEGIN`→`SEEK_SET`, `FILE_CURRENT`→`SEEK_CUR`, `FILE_END`→`SEEK_END`)
- [ ] `GetFileSize` → `fseek(0,SEEK_END)` + `ftell()`
- [ ] `GetFileAttributesA` → `stat()` → return `FILE_ATTRIBUTE_*` bits
- [ ] `FindFirstFileA` / `FindNextFileA` → `opendir()`/`readdir()` wrapped in `HANDLE`
- [ ] `DeleteFileA` → `remove()`
- [ ] `CreateDirectoryA` → `mkdir()`
- [ ] `GetModuleFileNameA` → return pre-stored DOOM95.EXE path (from loader)
- [ ] `SearchPathA` → `strchr()` / path concat; copy filename to output

### 6.2 Memory (kernel32_extended.c)
- [ ] `GlobalAlloc` → `malloc()` (ignore flags: `GMEM_MOVEABLE`, `GMEM_FIXED`)
- [ ] `LocalAlloc` → `malloc()` (ignore flags: `LMEM_MOVEABLE`, `LMEM_FIXED`)
- [ ] `LocalFree` → `free()`

### 6.3 Threading (kernel32_extended.c)
- [ ] `CreateThread` → `SDL_CreateThread()` → wrap in handle manager
- [ ] `ExitThread` → `SDL_ExitThread()`
- [ ] `GetCurrentThreadId` → `SDL_ThreadID()`
- [ ] `CreateEventA` → `SDL_CreateSemaphore()` (auto-reset: value 0; manual-reset: flag)
- [ ] `SetEvent` → `SDL_SemPost()`
- [ ] `WaitForSingleObject` → `SDL_SemWait()` or `SDL_SemWaitTimeout()` (map `INFINITE` → `SDL_SemWait()`)
- [ ] `CreateMutexA` → `SDL_CreateMutex()`
- [ ] `ReleaseMutex` → `SDL_UnlockMutex()`

### 6.4 Timing (kernel32_extended.c)
- [ ] `GetTickCount` → `rb_timer_get_ticks()`
- [ ] `GetVersion` → `return 0x80000005` (Windows 2000)
- [ ] `GetSystemInfo` → `SDL_CPUCount()` + hardcoded x86 fields
- [ ] `GetEnvironmentStrings` → return `environ` pointer
- [ ] `GetTimeZoneInformation` → return `TIME_ZONE_ID_UNKNOWN` with zeroed bias
- [ ] `DosDateTimeToFileTime` → DOS datetime → `FILETIME` (calendar math)
- [ ] `FileTimeToDosDateTime` → `FILETIME` → DOS datetime (reverse)
- [ ] `FileTimeToLocalFileTime` → identity copy (timezone offset = 0)
- [ ] `LocalFileTimeToFileTime` → identity copy
- [ ] `GetFileTime` → `stat()` → `FILETIME` or `return FALSE`

### 6.5 TLS (kernel32_extended.c)
- [ ] `TlsAlloc` → allocate index from pool, return next free slot
- [ ] `TlsSetValue` → `tls_array[slot][SDL_ThreadID()] = value`
- [ ] `TlsGetValue` → `return tls_array[slot][SDL_ThreadID()]` (already exists)
- [ ] `TlsFree` → `tls_array[slot][SDL_ThreadID()] = NULL`

### 6.6 Misc (kernel32_extended.c)
- [ ] `GetCPInfo` → fill with CP-1252 info; `return TRUE`
- [ ] `GetStdHandle` → already exists
- [ ] `SetStdHandle` → `return TRUE`
- [ ] `WriteConsoleA` → `printf()` or `return FALSE`
- [ ] `ReadConsoleInputA` → `return FALSE`
- [ ] `GetConsoleMode` → `return FALSE`
- [ ] `SetConsoleMode` → `return FALSE`
- [ ] `GetCurrentProcessId` → `return getpid()`
- [ ] `GetCurrentThread` → `return (HANDLE)(uintptr_t)-2`
- [ ] `GetFileType` → `return FILE_TYPE_DISK`
- [ ] `DeviceIoControl` → `return FALSE`
- [ ] `GetModuleHandleA` → already exists
- [ ] `LoadLibraryA` → already exists (extend for new DLLs: user32, gdi32, ddraw, dsound, winmm, advapi32)
- [ ] `GetProcAddress` → already exists (extend for new DLL functions)
- [ ] `FreeLibrary` → already exists
- [ ] `GetLastError` → already exists
- [ ] `FindResourceA` → search PE `.rsrc` directory for `(type, name)`
- [ ] `LoadResource` → return pointer to resource data in PE image
- [ ] `LockResource` → same pointer as `LoadResource`
- [ ] `SizeofResource` → return resource size from PE resource directory

### 6.7 WINMM Timer (winmm_timer.c)
- [ ] `timeGetTime` → `rb_timer_get_ticks()`

### 6.8 WINMM Joystick (winmm_joystick.c)
- [ ] `joyGetNumDevs` → `rb_joy_count()`
- [ ] `joyGetDevCapsA` → `rb_joy_get_caps()` → fill `JOYCAPSA`
- [ ] `joyGetPosEx` → `rb_joy_get_state()` → fill `JOYINFOEX`
  - Map axis: SDL -32768..32767 → WINMM 0..65535

### 6.9 WINMM MIDI (winmm_midi.c) — All Stubs
- [ ] `midiStreamOpen` → return mock `HMIDISTREAM` handle
- [ ] `midiStreamOut` → no-op (silent music)
- [ ] `midiStreamClose` → free handle
- [ ] `midiStreamPause` → no-op
- [ ] `midiStreamRestart` → no-op
- [ ] `midiStreamProperty` → return `MMSYSERR_NOERROR`
- [ ] `midiOutGetNumDevs` → return 1
- [ ] `midiOutPrepareHeader` → return `MMSYSERR_NOERROR`
- [ ] `midiOutUnprepareHeader` → return `MMSYSERR_NOERROR`
- [ ] `midiOutReset` → return `MMSYSERR_NOERROR`
- [ ] `midiOutSetVolume` → return `MMSYSERR_NOERROR`

### 6.10 ADVAPI32 Registry (advapi32_registry.c)
- [ ] In-memory registry hashmap: `path/key` → `HKEY` node
- [ ] `RegCreateKeyA` → create node, return mock `HKEY`
- [ ] `RegOpenKeyA` → lookup node, return mock `HKEY` or `ERROR_FILE_NOT_FOUND`
- [ ] `RegCloseKey` → decrement refcount on mock `HKEY`
- [ ] `RegQueryValueExA` → lookup value under key, return `ERROR_SUCCESS` + data or `ERROR_FILE_NOT_FOUND`
- [ ] `RegSetValueExA` → store value under key, return `ERROR_SUCCESS`

### 6.11 DPLAY Stub (dplay_stub.c)
- [ ] `DPCreate` (ordinal #1) → return mock `IDirectPlay` with no-op methods
- [ ] All vtable methods → return `DP_OK` or `DPERR_NOTINITIALIZED`

### 6.12 Import Tables
- [ ] Add all new `kernel32.dll` / `kernel32.DLL` entries to `import_table.c` (~50 new)
- [ ] Add `winmm.dll` entries (15 functions) to `import_table.c`
- [ ] Add `advapi32.dll` entries (5 functions) to `import_table.c`
- [ ] Add `dplay.dll` ordinal #1 to `ordinal_table.c`

### 6.13 Test
- [ ] Launch DOOM95.EXE → init completes without crash
- [ ] Confirm: file I/O works (WAD files readable), memory allocation works, events work, timing works, registry returns `ERROR_SUCCESS`, MIDI returns success (silent)

---

## Files
| File | Action |
|------|--------|
| `src/stubs/kernel32_extended.c` | **New** (~600 lines) |
| `src/stubs/winmm_timer.c` | **New** (~10 lines) |
| `src/stubs/winmm_joystick.c` | **New** (~100 lines) |
| `src/stubs/winmm_midi.c` | **New** (~100 lines) |
| `src/stubs/advapi32_registry.c` | **New** (~120 lines) |
| `src/stubs/dplay_stub.c` | **New** (~50 lines) |
| `src/loader/import_table.c` | Edit: add ~70 entries |
| `src/loader/ordinal_table.c` | Edit: add DPLAY ordinal #1 |

**~1,080 lines, ~4-5 days**
