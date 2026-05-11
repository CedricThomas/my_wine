# kernel32.dll — 69 unique functions

(20 from `kernel32.dll` first entry + 49 from `kernel32.DLL` second entry − 10 duplicates)

## Memory (3)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GlobalAlloc` | critical | `malloc()` | Ignore GMEM flags |
| `LocalAlloc` | critical | `malloc()` | Ignore LMEM flags |
| `LocalFree` | critical | `free()` | Trivial |

## File I/O (12)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateFileA` | critical | `fopen()` or `SDL_RWFromFile()` | GENERIC_READ→"rb", GENERIC_WRITE→"wb". Return HANDLE wrapping FILE*. |
| `ReadFile` | critical | `fread()` | Read from FILE* wrapped by HANDLE |
| `WriteFile` | critical | `fwrite()` | Same |
| `SetFilePointer` | critical | `fseek()` | dwMoveMethod maps to SEEK_* |
| `GetFileSize` | critical | `fseek()+ftell()` or `stat()` | File size queries |
| `CloseHandle` | critical | `fclose()` | Check handle type: file→fclose(), event→sem, mutex→unlock |
| `GetFileAttributesA` | critical | `stat()` | Return FILE_ATTRIBUTE_* bits |
| `FindNextFileA` | critical | `readdir()` | Wrap DIR* in HANDLE, fill WIN32_FIND_DATAA |
| `DeleteFileA` | optional | `remove()` | Trivial |
| `CreateDirectoryA` | optional | `mkdir()` | Trivial |
| `GetModuleFileNameA` | critical | Store path at init | Return DOOM95.EXE path (resolved at load time) |
| `GetFileTime` | optional | `stat()` or stub | Fill with mtime or current time |

## Threading / Synchronization (8)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateThread` | critical | `SDL_CreateThread()` | Wrap in HANDLE |
| `ExitThread` | critical | `SDL_ExitThread()` | Trivial |
| `GetCurrentThreadId` | critical | `SDL_ThreadID()` | Direct replacement |
| `CreateEventA` | critical | `SDL_CreateSemaphore()` | Auto-reset→Sem(0), manual-reset→flag+sem |
| `SetEvent` | critical | `SDL_SemPost()` | Signal semaphore |
| `WaitForSingleObject` | critical | `SDL_SemWait()` or `SDL_SemWaitTimeout()` | INFINITE→SemWait(), timeout→SemWaitTimeout() |
| `CreateMutexA` | optional | `SDL_CreateMutex()` | Map to SDL_mutex* |
| `ReleaseMutex` | optional | `SDL_UnlockMutex()` | Trivial |

## Timing (2)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetTickCount` | critical | `SDL_GetTicks()` | Direct replacement (milliseconds) |
| `Sleep` | critical | `SDL_Delay()` | Already implemented |

## Thread-Local Storage (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `TlsAlloc` | critical | Allocate index from pool | 2D array void** tls[slot][thread_id]. Return next free slot. |
| `TlsSetValue` | critical | `tls_array[index][thread_id] = value` | Per-thread per-slot storage |
| `TlsGetValue` | critical | `tls_array[index][thread_id]` | Already implemented |
| `TlsFree` | critical | `tls_array[index][thread_id] = NULL` | Free slot for current thread |

## System Info (5)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetSystemInfo` | critical | `SDL_CPUCount()` + hardcoded | Fill dwNumberOfProcessors, others hardcoded (x86) |
| `GetVersion` | critical | `return 0x80000005` | Hardcoded "Windows 2000". Feature detection. |
| `GetCommandLineA` | critical | `argv` reconstructed | Already implemented |
| `GetEnvironmentStrings` | cosmetic | `environ` or stub | Return NULL or environment block pointer |
| `GetTimeZoneInformation` | optional | `localtime()` or stub | Return TIME_ZONE_ID_UNKNOWN, zeroed bias |

## DLL Loading (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `LoadLibraryA` | critical | Internal DLL resolver | Look up in my_wine's registered DLL table. Return HMODULE=uintptr_t |
| `GetProcAddress` | critical | Lookup in DLL export table | Find by name → return trampoline function pointer |
| `FreeLibrary` | critical | Decrement DLL refcount | Already implemented. Don't actually unload. |
| `GetModuleHandleA` | critical | Lookup by name in DLL table | Already implemented. NULL name → my_wine or DOOM95.EXE |

## Resources (6)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `FindResourceA` | critical | Binary resource table lookup | Parse PE resource directory for (type, name). Return pointer+size. |
| `LoadResource` | critical | Return raw pointer to resource data | Data is in PE image. Return HGLOBAL to resource bytes. |
| `LockResource` | critical | Return raw pointer | Same pointer as LoadResource |
| `SizeofResource` | critical | Return resource size | Stored in PE resource directory |

## Error Handling (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetLastError` | critical | Thread-local errno | Store last error per thread. Set by each API stub on failure. |

## Process (2)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `ExitProcess` | critical | `exit()` or longjmp to host | Clean up SDL2 subsystems before exit |
| `GetCurrentProcessId` | optional | `getpid()` | C stdlib `<unistd.h>` |

## Console (6)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetStdHandle` | critical | Return sentinel HANDLE | Return special HANDLE for stdin/stdout/stderr. Already implemented. |
| `SetStdHandle` | optional | Stub (return TRUE) | No-op |
| `WriteConsoleA` | cosmetic | `printf()` or stub | Debug output |
| `ReadConsoleInputA` | optional | Stub (return FALSE) | Never called in game mode |
| `GetConsoleMode` | optional | Stub (return FALSE) | Console detection |
| `SetConsoleMode` | optional | Stub (return FALSE) | Console config |

## Date/Time Conversion (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `DosDateTimeToFileTime` | optional | Manual conversion | DOS datetime → FILETIME (calendar math) |
| `FileTimeToDosDateTime` | optional | Reverse conversion | FILETIME → DOS datetime |
| `FileTimeToLocalFileTime` | optional | Stub (identity copy) | Timezone offset = 0 |
| `LocalFileTimeToFileTime` | optional | Stub (identity copy) | Same |

## Misc (10)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `DeviceIoControl` | optional | Stub (return FALSE) | Input device I/O; can wire to SDL joystick |
| `SearchPathA` | optional | `strchr()` / path concat | Stub: copy filename to output |
| `GetCPInfo` | cosmetic | Stub (return TRUE) | Fill with CP-1252 info |
| `GetFileType` | optional | Stub (return FILE_TYPE_DISK) | Detect console vs file |
| `GetCurrentThread` | optional | Return pseudo-handle | HANDLE = (void*)-2 (Windows constant) |
| `FindResourceA` | critical | PE resource directory parse | (see Resources above) |
| `LoadResource` | critical | Return HGLOBAL to resource bytes | (see Resources above) |
| `LockResource` | critical | Return raw pointer | (see Resources above) |
| `SizeofResource` | critical | Return resource size | (see Resources above) |
| `FindNextFileA` | critical | `readdir()` wrapped | (see File I/O above) |
