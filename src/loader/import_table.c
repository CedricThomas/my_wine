/*
 * import_table.c — Static import entry definitions
 *
 * Maintains the import_entry_t table mapping DLL/function names
 * to our stub implementations.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <search.h>
#include <stdbool.h>

#include "include/ntdll.h"
#include "include/kernel32.h"
#include "include/msvcrt.h"
#include "include/common.h"
#include "include/ddraw_types.h"
#include "include/dsound_types.h"

#include "loader_priv.h"
#include "src/pe_priv.h"
#include "include/debug.h"

/* Stub function declarations from crt_32_stub.c (available in both 32-bit and 64-bit builds) */
extern int ___lc_codepage_func(void);
extern int ___mb_cur_max_func(void);
extern int *_errno(void);
extern void _lock(int);
extern void _unlock(int);
extern void __getmainargs(int *, char ***, char ***, int, void *);
extern void _initterm(void);
extern void *_initterm_e(const void **, const void **);
extern void *_onexit(void (*)(void));
extern void __setusermatherr(void (*)(void));
extern void __set_app_type(int);
extern void __lconv_init(void);
extern void *__iob_func(void);
extern void *__acrt_iob_func(void);
extern int _m_fprintf(void *, const char *, ...);
extern int _m_fwrite(const void *, size_t, size_t, void *);
extern int _m_vfprintf(void *, const char *, void *);
extern int _m_fputc(int, void *);
extern struct lconv *_m_localeconv(void);
extern char *_m_strerror(int);
extern int _m_atoi(const char *);
extern char *_m_strchr(const char *, int);
extern char *_m_setlocale(int, const char *);
extern int __mb_cur_max;
extern void _m_abort(void);
extern void _m_exit(int);
extern void *_m_malloc(size_t);
extern void _m_free(void *);
extern void *_m_calloc(size_t, size_t);
extern void *_m_realloc(void *, size_t);
extern void *_m_memcpy(void *, const void *, size_t);
extern void *_m_memset(void *, int, size_t);
extern size_t _m_strlen(const char *);
extern int _m_strcmp(const char *, const char *);
extern int _m_strncmp(const char *, const char *, size_t);
extern int _m_memcmp(const void *, const void *, size_t);
extern size_t _m_wcslen(const void *);
extern void *_m_signal(int, void (*)(int));
/* Wrapper function declarations from crt_32_stub.c (32-bit) and crt_globals.c (64-bit) */
extern char *__p__acmdln_func(void);
extern char *__p__fmode_func(void);
extern char *__p__commode_func(void);
extern char **__initenv_func(void);
/* Data symbols needed by import table — already declared in msvcrt.h (e.g. _acmdln, __p__acmdln) */
/* __initenv for 32-bit build — defined in crt_32_stub.c */
#ifdef MY_WINE32
extern char **__initenv;
extern char _acmdln[256];
extern int _commode;
extern int _fmode;
#endif
#ifdef MY_WINE32
/* 32-bit only: these are data symbols (from crt_32_stub.c) */
extern char *__p__commode;
extern char *__p__fmode;
#endif

/* user32 stub function declarations — minimal forward refs (we only need void* addresses).
 * Cannot include user32_types.h here: BOOL/ULONG typedefs conflict with ntdll.h in 64-bit. */
extern void RegisterClassA(void);
extern void AppendMenuA(void);
extern void CreateWindowExA(void);
extern void DialogBoxParamA(void);
extern void DestroyWindow(void);
extern void FindWindowA(void);
extern void ShowWindow(void);
extern void SetWindowPos(void);
extern void MoveWindow(void);
extern void SetWindowTextA(void);
extern void GetWindowPlacement(void);
extern void GetWindowRect(void);
extern void GetClientRect(void);
extern void GetWindowLongA(void);
extern void SetWindowLongA(void);
extern void GetWindowLongPtrA(void);
extern void SetWindowLongPtrA(void);
extern void IsWindow(void);
extern void EnableWindow(void);
extern void GetDesktopWindow(void);
extern void GetSystemMenu(void);
extern void GetActiveWindow(void);
extern void GetLastActivePopup(void);
extern void GetFocus(void);
extern void GetParent(void);
extern void IsIconic(void);
extern void IsWindowVisible(void);
extern void SetFocus(void);
extern void SetForegroundWindow(void);
extern void SetWindowPlacement(void);
extern void UpdateWindow(void);
extern void InvalidateRect(void);
extern void ValidateRect(void);
extern void BeginPaint(void);
extern void EndPaint(void);
extern void MapWindowPoints(void);
extern void GetSystemMetrics(void);
extern void AdjustWindowRect(void);
extern void AdjustWindowRectEx(void);
extern void GetDC(void);
extern void ReleaseDC(void);
extern void GetMessageA(void);
extern void PeekMessageA(void);
extern void DispatchMessageA(void);
extern void TranslateMessage(void);
extern void PostMessageA(void);
extern void PostThreadMessageA(void);
extern void PostQuitMessage(void);
extern void SendMessageA(void);
extern void DefWindowProcA(void);
extern void CallWindowProcA(void);
extern void SetWindowsHookExA(void);
extern void UnhookWindowsHookEx(void);
extern void CallNextHookEx(void);
extern void SystemParametersInfoA(void);
extern void GetAsyncKeyState(void);
extern void LoadCursorA(void);
extern void SetCursor(void);
extern void SetCursorPos(void);
extern void ClipCursor(void);
extern void LoadIconA(void);
extern void wsprintfA(void);
extern void SetRect(void);
extern void CheckDlgButton(void);
extern void CreateDialogParamA(void);
extern void EndDialog(void);
extern void GetDlgItem(void);
extern void GetDlgItemInt(void);
extern void GetDlgItemTextA(void);
extern void GetDlgCtrlID(void);
extern void GetDialogBaseUnits(void);
extern void GetWindowTextA(void);
extern void IsDialogMessageA(void);
extern void IsDlgButtonChecked(void);
extern void LoadStringA(void);
extern void MapVirtualKeyA(void);
extern void MessageBoxA(void);
extern void SendDlgItemMessageA(void);
extern void SetDlgItemInt(void);
extern void SetDlgItemTextA(void);
extern void WinHelpA(void);
extern void CreateDialogIndirectParamA(void);
extern void CopyRect(void);
extern void PropertySheetA(void);
extern void InitCommonControls(void);
extern void GetOpenFileNameA(void);
extern void CommDlgExtendedError(void);
extern void GetSaveFileNameA(void);
extern void CreateDCA(void);
extern void CreateDIBitmap(void);
extern void CreateFontA(void);
extern void CreatePalette(void);
extern void DeleteDC(void);
extern void DeleteObject(void);
extern void GetDeviceCaps(void);
extern void GetObjectA(void);
extern void GetStockObject(void);
extern void GetSystemPaletteEntries(void);
extern void RealizePalette(void);
extern void SelectPalette(void);
extern void SetBkColor(void);
extern void SetTextColor(void);
extern void StretchDIBits(void);
extern void UnrealizeObject(void);
extern void DeviceIoControl(void);
extern void DebugBreak(void);
extern void FindResourceA(void);
extern void GetCurrentThreadId(void);
extern void GetSystemInfo(void);
extern void GetTickCount(void);
extern void RegDeleteKeyA(void);
extern void RegEnumKeyExA(void);
extern void GlobalAlloc(void);
extern void LoadResource(void);
extern void LocalAlloc(void);
extern void LocalFree(void);
extern void LockResource(void);
extern void SearchPathA(void);
extern void SizeofResource(void);
extern void CreateDirectoryA(void);
extern void CreateThread(void);
extern void DosDateTimeToFileTime(void);
extern void ExitThread(void);
extern void FileTimeToDosDateTime(void);
extern void FileTimeToLocalFileTime(void);
extern void FindNextFileA(void);
extern void GetCPInfo(void);
extern void GetConsoleMode(void);
extern void GetCurrentProcessId(void);
extern void GetCurrentThread(void);
extern void GetEnvironmentStrings(void);
extern void GetFileAttributesA(void);
extern void GetFileSize(void);
extern void GetFileTime(void);
extern void GetFileType(void);
extern void IsTNT(void);
extern void GetModuleFileNameA(void);
extern void GetTimeZoneInformation(void);
extern void GetVersion(void);
extern void LocalFileTimeToFileTime(void);
extern void ReadConsoleInputA(void);
extern void SetConsoleMode(void);
extern void SetFilePointer(void);
extern void SetStdHandle(void);
extern void TlsAlloc(void);
extern void TlsFree(void);
extern void TlsSetValue(void);
extern void WriteConsoleA(void);
extern void timeGetTime(void);
extern void joyGetNumDevs(void);
extern void joyGetDevCapsA(void);
extern void joyGetPosEx(void);
extern void midiOutGetNumDevs(void);
extern void midiOutPrepareHeader(void);
extern void midiOutReset(void);
extern void midiOutSetVolume(void);
extern void midiOutUnprepareHeader(void);
extern void midiStreamClose(void);
extern void midiStreamOpen(void);
extern void midiStreamOut(void);
extern void midiStreamPause(void);
extern void midiStreamProperty(void);
extern void midiStreamRestart(void);
extern void RegCloseKey(void);
extern void RegCreateKeyA(void);
extern void RegOpenKeyA(void);
extern void RegQueryValueExA(void);
extern void RegSetValueExA(void);
extern void GetUserNameA(void);
extern void DPCreate(void);
extern void DirectPlayEnumerateA(void);
extern void SetCurrentDirectoryA(void);
extern void GetCurrentDirectoryA(void);
extern void SetErrorMode(void);
extern void SetEnvironmentVariableA(void);
extern void FindClose(void);
extern void FlushFileBuffers(void);
extern void SetEndOfFile(void);
extern void SetHandleCount(void);
extern void GetACP(void);
extern void FileTimeToSystemTime(void);
extern void GetEnvironmentStringsW(void);
extern void FreeEnvironmentStringsW(void);
extern void FreeEnvironmentStringsA(void);
extern void GetOEMCP(void);
extern void SetLastError(void);
extern void CompareStringW(void);
extern void CompareStringA(void);
extern void LCMapStringW(void);
extern void LCMapStringA(void);
extern void GetStringTypeW(void);
extern void GetStringTypeA(void);
extern void FindFirstFileA(void);
extern void IsBadCodePtr(void);
extern void IsBadWritePtr(void);
extern void IsBadReadPtr(void);
extern void RtlUnwind(void);
extern HRESULT KERNEL32_STUB DirectDrawCreate(const GUID *, LPDIRECTDRAW *, void *);
extern HRESULT KERNEL32_STUB DirectSoundCreate(const GUID *, LPDIRECTSOUND *, void *);

/* Name→address table for NT, kernel32 and msvcrt functions */
import_entry_t import_table[] = {
    /* ── ntdll syscall handlers ────────────────────────────────────────
     * Resolved via thunk lookup — each entry maps to a handler_*
     * function that dispatches to the corresponding Linux syscall.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "ntdll.dll", "NtWriteFile", (void*)handler_NtWriteFile },
    { "ntdll.dll", "NtReadFile", (void*)handler_NtReadFile },
    { "ntdll.dll", "NtClose", (void*)handler_NtClose },
    { "ntdll.dll", "NtTerminateProcess", (void*)handler_NtTerminateProcess },
    { "ntdll.dll", "NtCallbackReturn", (void*)handler_NtCallbackReturn },
    { "ntdll.dll", "NtQueryInformationProcess", (void*)handler_NtQueryInformationProcess },
    { "ntdll.dll", "NtAllocateVirtualMemory", (void*)handler_NtAllocateVirtualMemory },
    { "ntdll.dll", "NtFreeVirtualMemory", (void*)handler_NtFreeVirtualMemory },
    { "ntdll.dll", "NtCreateSection", (void*)handler_NtCreateSection },
    { "ntdll.dll", "NtMapViewOfSection", (void*)handler_NtMapViewOfSection },
    { "ntdll.dll", "NtUnmapViewOfSection", (void*)handler_NtUnmapViewOfSection },
    { "ntdll.dll", "NtCreateEvent", (void*)handler_NtCreateEvent },
    { "ntdll.dll", "NtCreateSemaphore", (void*)handler_NtCreateSemaphore },
    { "ntdll.dll", "NtCreateThreadEx", (void*)handler_NtCreateThreadEx },
    { "ntdll.dll", "NtOpenFile", (void*)handler_NtOpenFile },
    { "ntdll.dll", "NtGetContextThread", (void*)handler_NtGetContextThread },
    { "ntdll.dll", "NtSetContextThread", (void*)handler_NtSetContextThread },
    /* ── kernel32 stubs ────────────────────────────────────────────────
     * Our C implementations of common Windows API functions.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "kernel32.dll", "GetStdHandle", (void*)GetStdHandle },
    { "kernel32.dll", "CloseHandle", (void*)CloseHandle },
    { "kernel32.dll", "WriteFile", (void*)WriteFile },
    { "kernel32.dll", "ReadFile", (void*)ReadFile },
    { "kernel32.dll", "CreateFileA", (void*)CreateFileA },
    { "kernel32.dll", "DeleteFileA", (void*)DeleteFileA },
    { "kernel32.dll", "ExitProcess", (void*)ExitProcess },
    { "kernel32.dll", "GetProcAddress", (void*)GetProcAddress },
    { "kernel32.dll", "GetCurrentThreadId", (void*)GetCurrentThreadId },
    { "kernel32.dll", "GetSystemInfo", (void*)GetSystemInfo },
    { "kernel32.dll", "GetTickCount", (void*)GetTickCount },
    { "kernel32.dll", "GlobalAlloc", (void*)GlobalAlloc },
    { "kernel32.dll", "LoadLibraryA", (void*)LoadLibraryA },
    { "kernel32.dll", "LoadResource", (void*)LoadResource },
    { "kernel32.dll", "LocalAlloc", (void*)LocalAlloc },
    { "kernel32.dll", "LocalFree", (void*)LocalFree },
    { "kernel32.dll", "LockResource", (void*)LockResource },
    { "kernel32.dll", "SearchPathA", (void*)SearchPathA },
    { "kernel32.dll", "SizeofResource", (void*)SizeofResource },
    { "kernel32.dll", "GetModuleHandleA", (void*)GetModuleHandleA },
    { "kernel32.dll", "GetModuleHandleW", (void*)GetModuleHandleW },
    { "kernel32.dll", "GetCommandLineA", (void*)GetCommandLineA },
    { "kernel32.dll", "GetEnvironmentStrings", (void*)GetEnvironmentStrings },
    { "kernel32.dll", "GetEnvironmentStringsA", (void*)GetEnvironmentStringsA },
    { "kernel32.dll", "FindResourceA", (void*)FindResourceA },
    { "kernel32.dll", "DeviceIoControl", (void*)DeviceIoControl },
    /* mingw-w64 imports "FreeLibrary" (no 'A' suffix) — alias to FreeLibraryA */
    { "kernel32.dll", "FreeLibrary", (void*)FreeLibraryA },
    { "kernel32.dll", "FreeLibraryA", (void*)FreeLibraryA },
    { "kernel32.dll", "GetProcessHeap", (void*)GetProcessHeap },
    { "kernel32.dll", "lstrlenA", (void*)lstrlenA },
    { "kernel32.dll", "lstrcpyA", (void*)lstrcpyA },
    { "kernel32.dll", "lstrcatA", (void*)lstrcatA },
    { "kernel32.dll", "DeleteCriticalSection", (void*)DeleteCriticalSection },
    { "kernel32.dll", "EnterCriticalSection", (void*)EnterCriticalSection },
    { "kernel32.dll", "GetLastError", (void*)GetLastError },
    { "kernel32.dll", "IsTNT", (void*)IsTNT },
    { "kernel32.dll", "GetStartupInfoA", (void*)GetStartupInfoA },
    { "kernel32.dll", "HeapAlloc", (void*)HeapAlloc },
    { "kernel32.dll", "HeapCreate", (void*)HeapCreate },
    { "kernel32.dll", "HeapDestroy", (void*)HeapDestroy },
    { "kernel32.dll", "HeapFree", (void*)HeapFree },
    { "kernel32.dll", "HeapReAlloc", (void*)HeapReAlloc },
    { "kernel32.dll", "HeapSize", (void*)HeapSize },
    { "kernel32.dll", "InitializeCriticalSection", (void*)InitializeCriticalSection },
    { "kernel32.dll", "LeaveCriticalSection", (void*)LeaveCriticalSection },
    { "kernel32.dll", "SetUnhandledExceptionFilter", (void*)SetUnhandledExceptionFilter },
    { "kernel32.dll", "Sleep", (void*)Sleep },
    { "kernel32.dll", "GetSystemTimeAsFileTime", (void*)GetSystemTimeAsFileTime },
    { "kernel32.dll", "QueryPerformanceCounter", (void*)QueryPerformanceCounter },
    { "kernel32.dll", "QueryPerformanceFrequency", (void*)QueryPerformanceFrequency },
    { "kernel32.dll", "CreateEventA", (void*)CreateEventA },
    { "kernel32.dll", "SetEvent", (void*)SetEvent },
    { "kernel32.dll", "ResetEvent", (void*)ResetEvent },
    { "kernel32.dll", "WaitForSingleObject", (void*)WaitForSingleObject },
    { "kernel32.dll", "CreateMutexA", (void*)CreateMutexA },
    { "kernel32.dll", "ReleaseMutex", (void*)ReleaseMutex },
    { "kernel32.dll", "CreateDirectoryA", (void*)CreateDirectoryA },
    { "kernel32.dll", "CreateThread", (void*)CreateThread },
    { "kernel32.dll", "DosDateTimeToFileTime", (void*)DosDateTimeToFileTime },
    { "kernel32.dll", "ExitThread", (void*)ExitThread },
    { "kernel32.dll", "FileTimeToDosDateTime", (void*)FileTimeToDosDateTime },
    { "kernel32.dll", "FileTimeToLocalFileTime", (void*)FileTimeToLocalFileTime },
    { "kernel32.dll", "FileTimeToSystemTime", (void*)FileTimeToSystemTime },
    { "kernel32.dll", "FindClose", (void*)FindClose },
    { "kernel32.dll", "FindNextFileA", (void*)FindNextFileA },
    { "kernel32.dll", "FlushFileBuffers", (void*)FlushFileBuffers },
    { "kernel32.dll", "GetACP", (void*)GetACP },
    { "kernel32.dll", "GetCPInfo", (void*)GetCPInfo },
    { "kernel32.dll", "GetConsoleMode", (void*)GetConsoleMode },
    { "kernel32.dll", "GetCurrentDirectoryA", (void*)GetCurrentDirectoryA },
    { "kernel32.dll", "GetCurrentProcessId", (void*)GetCurrentProcessId },
    { "kernel32.dll", "GetCurrentThread", (void*)GetCurrentThread },
    { "kernel32.dll", "DebugBreak", (void*)DebugBreak },
    { "kernel32.dll", "GetFileAttributesA", (void*)GetFileAttributesA },
    { "kernel32.dll", "GetFileSize", (void*)GetFileSize },
    { "kernel32.dll", "GetFileTime", (void*)GetFileTime },
    { "kernel32.dll", "GetFileType", (void*)GetFileType },
    { "kernel32.dll", "GetModuleFileNameA", (void*)GetModuleFileNameA },
    { "kernel32.dll", "GetTimeZoneInformation", (void*)GetTimeZoneInformation },
    { "kernel32.dll", "GetVersion", (void*)GetVersion },
    { "kernel32.dll", "GetEnvironmentStringsW", (void*)GetEnvironmentStringsW },
    { "kernel32.dll", "FreeEnvironmentStringsA", (void*)FreeEnvironmentStringsA },
    { "kernel32.dll", "FreeEnvironmentStringsW", (void*)FreeEnvironmentStringsW },
    { "kernel32.dll", "GetOEMCP", (void*)GetOEMCP },
    { "kernel32.dll", "LocalFileTimeToFileTime", (void*)LocalFileTimeToFileTime },
    { "kernel32.dll", "ReadConsoleInputA", (void*)ReadConsoleInputA },
    { "kernel32.dll", "RtlUnwind", (void*)RtlUnwind },
    { "kernel32.dll", "SetCurrentDirectoryA", (void*)SetCurrentDirectoryA },
    { "kernel32.dll", "SetConsoleMode", (void*)SetConsoleMode },
    { "kernel32.dll", "SetEnvironmentVariableA", (void*)SetEnvironmentVariableA },
    { "kernel32.dll", "SetErrorMode", (void*)SetErrorMode },
    { "kernel32.dll", "SetEndOfFile", (void*)SetEndOfFile },
    { "kernel32.dll", "SetFilePointer", (void*)SetFilePointer },
    { "kernel32.dll", "SetHandleCount", (void*)SetHandleCount },
    { "kernel32.dll", "SetLastError", (void*)SetLastError },
    { "kernel32.dll", "SetStdHandle", (void*)SetStdHandle },
    { "kernel32.dll", "CompareStringA", (void*)CompareStringA },
    { "kernel32.dll", "CompareStringW", (void*)CompareStringW },
    { "kernel32.dll", "FindFirstFileA", (void*)FindFirstFileA },
    { "kernel32.dll", "GetStringTypeA", (void*)GetStringTypeA },
    { "kernel32.dll", "GetStringTypeW", (void*)GetStringTypeW },
    { "kernel32.dll", "IsBadCodePtr", (void*)IsBadCodePtr },
    { "kernel32.dll", "IsBadReadPtr", (void*)IsBadReadPtr },
    { "kernel32.dll", "IsBadWritePtr", (void*)IsBadWritePtr },
    { "kernel32.dll", "LCMapStringA", (void*)LCMapStringA },
    { "kernel32.dll", "LCMapStringW", (void*)LCMapStringW },
    { "kernel32.dll", "TlsAlloc", (void*)TlsAlloc },
    { "kernel32.dll", "TlsFree", (void*)TlsFree },
    { "kernel32.dll", "TlsGetValue", (void*)TlsGetValue },
    { "kernel32.dll", "TlsSetValue", (void*)TlsSetValue },
    { "kernel32.dll", "WriteConsoleA", (void*)WriteConsoleA },
    { "kernel32.dll", "VirtualProtect", (void*)VirtualProtect },
    { "kernel32.dll", "VirtualQuery", (void*)VirtualQuery },
    { "kernel32.dll", "VirtualAlloc", (void*)VirtualAlloc },
    { "kernel32.dll", "VirtualFree", (void*)VirtualFree },
    { "kernel32.dll", "IsDBCSLeadByteEx", (void*)IsDBCSLeadByteEx },
    { "kernel32.dll", "MultiByteToWideChar", (void*)MultiByteToWideChar },
    { "kernel32.dll", "WideCharToMultiByte", (void*)WideCharToMultiByte },
    /* ── msvcrt functions (statically known) ────────────────────────────
     * Our C stubs for CRT initialization, I/O, and data variables.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "msvcrt.dll", "__C_specific_handler", (void*)__C_specific_handler },
    /* CRT startup + stdlib — available in both 64-bit and 32-bit builds */
    { "msvcrt.dll", "__getmainargs", (void*)__getmainargs },
    { "msvcrt.dll", "__mb_cur_max", (void*)&__mb_cur_max },
    { "msvcrt.dll", "__iob_func", (void*)__iob_func },
    { "msvcrt.dll", "__acrt_iob_func", (void*)__acrt_iob_func },
    { "msvcrt.dll", "__lconv_init", (void*)__lconv_init },
    { "msvcrt.dll", "__set_app_type", (void*)__set_app_type },
    { "msvcrt.dll", "__setusermatherr", (void*)__setusermatherr },
#ifdef MY_WINE32
    { "msvcrt.dll", "_acmdln", (void*)&_acmdln },
    { "msvcrt.dll", "_commode", (void*)&_commode },
    { "msvcrt.dll", "_fmode", (void*)&_fmode },
#else
    { "msvcrt.dll", "_acmdln", (void*)&g_crt.acmdln },
    { "msvcrt.dll", "_commode", (void*)&g_crt.commode },
    { "msvcrt.dll", "_fmode", (void*)&g_crt.fmode },
#endif
    { "msvcrt.dll", "_amsg_exit", (void*)_amsg_exit },
    { "msvcrt.dll", "_cexit", (void*)_cexit },
    { "msvcrt.dll", "_initterm", (void*)_initterm },
    { "msvcrt.dll", "_onexit", (void*)_onexit },
    { "msvcrt.dll", "___lc_codepage_func", (void*)___lc_codepage_func },
    { "msvcrt.dll", "___mb_cur_max_func", (void*)___mb_cur_max_func },
    { "msvcrt.dll", "_errno", (void*)_errno },
    { "msvcrt.dll", "_lock", (void*)_lock },
    { "msvcrt.dll", "_unlock", (void*)_unlock },
    { "msvcrt.dll", "abort", (void*)_m_abort },
    { "msvcrt.dll", "atoi", (void*)_m_atoi },
    { "msvcrt.dll", "exit", (void*)_m_exit },
    { "msvcrt.dll", "malloc", (void*)_m_malloc },
    { "msvcrt.dll", "free", (void*)_m_free },
    { "msvcrt.dll", "calloc", (void*)_m_calloc },
    { "msvcrt.dll", "realloc", (void*)_m_realloc },
    { "msvcrt.dll", "memcpy", (void*)_m_memcpy },
    { "msvcrt.dll", "memcmp", (void*)_m_memcmp },
    { "msvcrt.dll", "memset", (void*)_m_memset },
    { "msvcrt.dll", "strlen", (void*)_m_strlen },
    { "msvcrt.dll", "strcmp", (void*)_m_strcmp },
    { "msvcrt.dll", "strncmp", (void*)_m_strncmp },
    { "msvcrt.dll", "wcslen", (void*)_m_wcslen },
    { "msvcrt.dll", "signal", (void*)_m_signal },
    { "msvcrt.dll", "setlocale", (void*)_m_setlocale },
    { "msvcrt.dll", "strchr", (void*)_m_strchr },
    { "msvcrt.dll", "fprintf", (void*)_m_fprintf },
    { "msvcrt.dll", "fwrite", (void*)_m_fwrite },
    { "msvcrt.dll", "vfprintf", (void*)_m_vfprintf },
    { "msvcrt.dll", "fputc", (void*)_m_fputc },
    { "msvcrt.dll", "localeconv", (void*)_m_localeconv },
    { "msvcrt.dll", "strerror", (void*)_m_strerror },
#ifdef MY_WINE32
    /* 32-bit: __p__* must be wrapper functions (JMP thunks in PE).
     * __initenv is DATA (PE writes to it, not calls it). */
    { "msvcrt.dll", "__initenv", (void*)&__initenv },
    { "msvcrt.dll", "__p__acmdln", (void*)__p__acmdln_func },
    { "msvcrt.dll", "__p__commode", (void*)__p__commode_func },
    { "msvcrt.dll", "__p__fmode", (void*)__p__fmode_func },
    { "msvcrt.dll", "_iob", (void*)__iob_func },
#else
    /* 64-bit: __p__* must also be functions. __initenv is g_crt.initenv. */
    { "msvcrt.dll", "__initenv", (void*)&g_crt.initenv },
    { "msvcrt.dll", "__p__acmdln", (void*)__p__acmdln_func },
    { "msvcrt.dll", "__p__commode", (void*)__p__commode_func },
    { "msvcrt.dll", "__p__fmode", (void*)__p__fmode_func },
    { "msvcrt.dll", "_iob", (void*)__iob_func },
#endif
    /* ── user32 stubs ───────────────────────────────────────────────── */
    { "user32.dll", "AppendMenuA", (void*)AppendMenuA },
    { "user32.dll", "DialogBoxParamA", (void*)DialogBoxParamA },
    { "user32.dll", "EndDialog", (void*)EndDialog },
    { "user32.dll", "FindWindowA", (void*)FindWindowA },
    { "user32.dll", "GetDialogBaseUnits", (void*)GetDialogBaseUnits },
    { "user32.dll", "GetDlgCtrlID", (void*)GetDlgCtrlID },
    { "user32.dll", "GetDlgItemInt", (void*)GetDlgItemInt },
    { "user32.dll", "GetDlgItemTextA", (void*)GetDlgItemTextA },
    { "user32.dll", "GetParent", (void*)GetParent },
    { "user32.dll", "GetSystemMenu", (void*)GetSystemMenu },
    { "user32.dll", "GetWindowPlacement", (void*)GetWindowPlacement },
    { "user32.dll", "GetWindowTextA", (void*)GetWindowTextA },
    { "user32.dll", "IsIconic", (void*)IsIconic },
    { "user32.dll", "IsWindowEnabled", (void*)EnableWindow },
    { "user32.dll", "IsWindowVisible", (void*)IsWindowVisible },
    { "user32.dll", "RegisterClassA", (void*)RegisterClassA },
    { "user32.dll", "CreateWindowExA", (void*)CreateWindowExA },
    { "user32.dll", "DestroyWindow", (void*)DestroyWindow },
    { "user32.dll", "ShowWindow", (void*)ShowWindow },
    { "user32.dll", "SetWindowPos", (void*)SetWindowPos },
    { "user32.dll", "MoveWindow", (void*)MoveWindow },
    { "user32.dll", "SetWindowTextA", (void*)SetWindowTextA },
    { "user32.dll", "GetWindowRect", (void*)GetWindowRect },
    { "user32.dll", "GetClientRect", (void*)GetClientRect },
    { "user32.dll", "GetWindowLongA", (void*)GetWindowLongA },
    { "user32.dll", "SetWindowLongA", (void*)SetWindowLongA },
    { "user32.dll", "GetWindowLongPtrA", (void*)GetWindowLongPtrA },
    { "user32.dll", "SetWindowLongPtrA", (void*)SetWindowLongPtrA },
    { "user32.dll", "IsWindow", (void*)IsWindow },
    { "user32.dll", "EnableWindow", (void*)EnableWindow },
    { "user32.dll", "GetDesktopWindow", (void*)GetDesktopWindow },
    { "user32.dll", "GetActiveWindow", (void*)GetActiveWindow },
    { "user32.dll", "GetLastActivePopup", (void*)GetLastActivePopup },
    { "user32.dll", "GetFocus", (void*)GetFocus },
    { "user32.dll", "SetFocus", (void*)SetFocus },
    { "user32.dll", "UpdateWindow", (void*)UpdateWindow },
    { "user32.dll", "InvalidateRect", (void*)InvalidateRect },
    { "user32.dll", "ValidateRect", (void*)ValidateRect },
    { "user32.dll", "BeginPaint", (void*)BeginPaint },
    { "user32.dll", "EndPaint", (void*)EndPaint },
    { "user32.dll", "MapWindowPoints", (void*)MapWindowPoints },
    { "user32.dll", "GetSystemMetrics", (void*)GetSystemMetrics },
    { "user32.dll", "AdjustWindowRect", (void*)AdjustWindowRect },
    { "user32.dll", "AdjustWindowRectEx", (void*)AdjustWindowRectEx },
    { "user32.dll", "GetDC", (void*)GetDC },
    { "user32.dll", "ReleaseDC", (void*)ReleaseDC },
    { "user32.dll", "GetMessageA", (void*)GetMessageA },
    { "user32.dll", "PeekMessageA", (void*)PeekMessageA },
    { "user32.dll", "DispatchMessageA", (void*)DispatchMessageA },
    { "user32.dll", "TranslateMessage", (void*)TranslateMessage },
    { "user32.dll", "PostMessageA", (void*)PostMessageA },
    { "user32.dll", "PostThreadMessageA", (void*)PostThreadMessageA },
    { "user32.dll", "PostQuitMessage", (void*)PostQuitMessage },
    { "user32.dll", "SendMessageA", (void*)SendMessageA },
    { "user32.dll", "DefWindowProcA", (void*)DefWindowProcA },
    { "user32.dll", "CallWindowProcA", (void*)CallWindowProcA },
    { "user32.dll", "CheckDlgButton", (void*)CheckDlgButton },
    { "user32.dll", "SetWindowsHookExA", (void*)SetWindowsHookExA },
    { "user32.dll", "UnhookWindowsHookEx", (void*)UnhookWindowsHookEx },
    { "user32.dll", "CallNextHookEx", (void*)CallNextHookEx },
    { "user32.dll", "CopyRect", (void*)CopyRect },
    { "user32.dll", "CreateDialogIndirectParamA", (void*)CreateDialogIndirectParamA },
    { "user32.dll", "CreateDialogParamA", (void*)CreateDialogParamA },
    { "user32.dll", "GetDlgItem", (void*)GetDlgItem },
    { "user32.dll", "IsDialogMessageA", (void*)IsDialogMessageA },
    { "user32.dll", "IsDlgButtonChecked", (void*)IsDlgButtonChecked },
    { "user32.dll", "LoadStringA", (void*)LoadStringA },
    { "user32.dll", "MapVirtualKeyA", (void*)MapVirtualKeyA },
    { "user32.dll", "MessageBoxA", (void*)MessageBoxA },
    { "user32.dll", "SendDlgItemMessageA", (void*)SendDlgItemMessageA },
    { "user32.dll", "SetDlgItemInt", (void*)SetDlgItemInt },
    { "user32.dll", "SetForegroundWindow", (void*)SetForegroundWindow },
    { "user32.dll", "SetDlgItemTextA", (void*)SetDlgItemTextA },
    { "user32.dll", "SetWindowPlacement", (void*)SetWindowPlacement },
    { "user32.dll", "SystemParametersInfoA", (void*)SystemParametersInfoA },
    { "user32.dll", "WinHelpA", (void*)WinHelpA },
    { "user32.dll", "GetAsyncKeyState", (void*)GetAsyncKeyState },
    { "user32.dll", "LoadCursorA", (void*)LoadCursorA },
    { "user32.dll", "SetCursor", (void*)SetCursor },
    { "user32.dll", "SetCursorPos", (void*)SetCursorPos },
    { "user32.dll", "ClipCursor", (void*)ClipCursor },
    { "user32.dll", "LoadIconA", (void*)LoadIconA },
    { "user32.dll", "wsprintfA", (void*)wsprintfA },
    { "user32.dll", "SetRect", (void*)SetRect },
    { "ddraw.dll", "DirectDrawCreate", (void*)DirectDrawCreate },
    { "dsound.dll", "DirectSoundCreate", (void*)DirectSoundCreate },
    { "user32.DLL", "AppendMenuA", (void*)AppendMenuA },
    { "user32.DLL", "DialogBoxParamA", (void*)DialogBoxParamA },
    { "user32.DLL", "EndDialog", (void*)EndDialog },
    { "user32.DLL", "FindWindowA", (void*)FindWindowA },
    { "user32.DLL", "GetDialogBaseUnits", (void*)GetDialogBaseUnits },
    { "user32.DLL", "GetDlgCtrlID", (void*)GetDlgCtrlID },
    { "user32.DLL", "GetDlgItemInt", (void*)GetDlgItemInt },
    { "user32.DLL", "GetDlgItemTextA", (void*)GetDlgItemTextA },
    { "user32.DLL", "GetParent", (void*)GetParent },
    { "user32.DLL", "GetSystemMenu", (void*)GetSystemMenu },
    { "user32.DLL", "GetWindowPlacement", (void*)GetWindowPlacement },
    { "user32.DLL", "GetWindowTextA", (void*)GetWindowTextA },
    { "user32.DLL", "IsIconic", (void*)IsIconic },
    { "user32.DLL", "IsWindowEnabled", (void*)EnableWindow },
    { "user32.DLL", "IsWindowVisible", (void*)IsWindowVisible },
    { "user32.DLL", "RegisterClassA", (void*)RegisterClassA },
    { "user32.DLL", "CreateWindowExA", (void*)CreateWindowExA },
    { "user32.DLL", "DestroyWindow", (void*)DestroyWindow },
    { "user32.DLL", "ShowWindow", (void*)ShowWindow },
    { "user32.DLL", "SetWindowPos", (void*)SetWindowPos },
    { "user32.DLL", "MoveWindow", (void*)MoveWindow },
    { "user32.DLL", "SetWindowTextA", (void*)SetWindowTextA },
    { "user32.DLL", "GetWindowRect", (void*)GetWindowRect },
    { "user32.DLL", "GetClientRect", (void*)GetClientRect },
    { "user32.DLL", "GetWindowLongA", (void*)GetWindowLongA },
    { "user32.DLL", "SetWindowLongA", (void*)SetWindowLongA },
    { "user32.DLL", "GetWindowLongPtrA", (void*)GetWindowLongPtrA },
    { "user32.DLL", "SetWindowLongPtrA", (void*)SetWindowLongPtrA },
    { "user32.DLL", "IsWindow", (void*)IsWindow },
    { "user32.DLL", "EnableWindow", (void*)EnableWindow },
    { "user32.DLL", "GetDesktopWindow", (void*)GetDesktopWindow },
    { "user32.DLL", "GetActiveWindow", (void*)GetActiveWindow },
    { "user32.DLL", "GetLastActivePopup", (void*)GetLastActivePopup },
    { "user32.DLL", "GetFocus", (void*)GetFocus },
    { "user32.DLL", "SetFocus", (void*)SetFocus },
    { "user32.DLL", "UpdateWindow", (void*)UpdateWindow },
    { "user32.DLL", "InvalidateRect", (void*)InvalidateRect },
    { "user32.DLL", "ValidateRect", (void*)ValidateRect },
    { "user32.DLL", "BeginPaint", (void*)BeginPaint },
    { "user32.DLL", "EndPaint", (void*)EndPaint },
    { "user32.DLL", "MapWindowPoints", (void*)MapWindowPoints },
    { "user32.DLL", "GetSystemMetrics", (void*)GetSystemMetrics },
    { "user32.DLL", "AdjustWindowRect", (void*)AdjustWindowRect },
    { "user32.DLL", "AdjustWindowRectEx", (void*)AdjustWindowRectEx },
    { "user32.DLL", "GetDC", (void*)GetDC },
    { "user32.DLL", "ReleaseDC", (void*)ReleaseDC },
    { "user32.DLL", "GetMessageA", (void*)GetMessageA },
    { "user32.DLL", "PeekMessageA", (void*)PeekMessageA },
    { "user32.DLL", "DispatchMessageA", (void*)DispatchMessageA },
    { "user32.DLL", "TranslateMessage", (void*)TranslateMessage },
    { "user32.DLL", "PostMessageA", (void*)PostMessageA },
    { "user32.DLL", "PostThreadMessageA", (void*)PostThreadMessageA },
    { "user32.DLL", "PostQuitMessage", (void*)PostQuitMessage },
    { "user32.DLL", "SendMessageA", (void*)SendMessageA },
    { "user32.DLL", "DefWindowProcA", (void*)DefWindowProcA },
    { "user32.DLL", "CallWindowProcA", (void*)CallWindowProcA },
    { "user32.DLL", "CheckDlgButton", (void*)CheckDlgButton },
    { "user32.DLL", "SetWindowsHookExA", (void*)SetWindowsHookExA },
    { "user32.DLL", "UnhookWindowsHookEx", (void*)UnhookWindowsHookEx },
    { "user32.DLL", "CallNextHookEx", (void*)CallNextHookEx },
    { "user32.DLL", "CopyRect", (void*)CopyRect },
    { "user32.DLL", "CreateDialogIndirectParamA", (void*)CreateDialogIndirectParamA },
    { "user32.DLL", "CreateDialogParamA", (void*)CreateDialogParamA },
    { "user32.DLL", "GetDlgItem", (void*)GetDlgItem },
    { "user32.DLL", "IsDialogMessageA", (void*)IsDialogMessageA },
    { "user32.DLL", "IsDlgButtonChecked", (void*)IsDlgButtonChecked },
    { "user32.DLL", "LoadStringA", (void*)LoadStringA },
    { "user32.DLL", "MapVirtualKeyA", (void*)MapVirtualKeyA },
    { "user32.DLL", "MessageBoxA", (void*)MessageBoxA },
    { "user32.DLL", "SendDlgItemMessageA", (void*)SendDlgItemMessageA },
    { "user32.DLL", "SetDlgItemInt", (void*)SetDlgItemInt },
    { "user32.DLL", "SetForegroundWindow", (void*)SetForegroundWindow },
    { "user32.DLL", "SetDlgItemTextA", (void*)SetDlgItemTextA },
    { "user32.DLL", "SetWindowPlacement", (void*)SetWindowPlacement },
    { "user32.DLL", "SystemParametersInfoA", (void*)SystemParametersInfoA },
    { "user32.DLL", "WinHelpA", (void*)WinHelpA },
    { "user32.DLL", "GetAsyncKeyState", (void*)GetAsyncKeyState },
    { "user32.DLL", "LoadCursorA", (void*)LoadCursorA },
    { "user32.DLL", "SetCursor", (void*)SetCursor },
    { "user32.DLL", "SetCursorPos", (void*)SetCursorPos },
    { "user32.DLL", "ClipCursor", (void*)ClipCursor },
    { "user32.DLL", "LoadIconA", (void*)LoadIconA },
    { "user32.DLL", "wsprintfA", (void*)wsprintfA },
    { "user32.DLL", "SetRect", (void*)SetRect },
    { "comctl32.dll", "InitCommonControls", (void*)InitCommonControls },
    { "comctl32.dll", "PropertySheetA", (void*)PropertySheetA },
    { "comdlg32.dll", "CommDlgExtendedError", (void*)CommDlgExtendedError },
    { "comdlg32.dll", "GetOpenFileNameA", (void*)GetOpenFileNameA },
    { "comdlg32.dll", "GetSaveFileNameA", (void*)GetSaveFileNameA },
    { "gdi32.dll", "CreateDCA", (void*)CreateDCA },
    { "gdi32.dll", "CreateDIBitmap", (void*)CreateDIBitmap },
    { "gdi32.dll", "CreateFontA", (void*)CreateFontA },
    { "gdi32.dll", "CreatePalette", (void*)CreatePalette },
    { "gdi32.dll", "DeleteDC", (void*)DeleteDC },
    { "gdi32.dll", "DeleteObject", (void*)DeleteObject },
    { "gdi32.dll", "GetDeviceCaps", (void*)GetDeviceCaps },
    { "gdi32.dll", "GetObjectA", (void*)GetObjectA },
    { "gdi32.dll", "GetStockObject", (void*)GetStockObject },
    { "gdi32.dll", "GetSystemPaletteEntries", (void*)GetSystemPaletteEntries },
    { "gdi32.dll", "RealizePalette", (void*)RealizePalette },
    { "gdi32.dll", "SelectPalette", (void*)SelectPalette },
    { "gdi32.dll", "SetBkColor", (void*)SetBkColor },
    { "gdi32.dll", "SetTextColor", (void*)SetTextColor },
    { "gdi32.dll", "StretchDIBits", (void*)StretchDIBits },
    { "gdi32.dll", "UnrealizeObject", (void*)UnrealizeObject },
    { "gdi32.DLL", "CreateDCA", (void*)CreateDCA },
    { "gdi32.DLL", "CreateDIBitmap", (void*)CreateDIBitmap },
    { "gdi32.DLL", "CreateFontA", (void*)CreateFontA },
    { "gdi32.DLL", "CreatePalette", (void*)CreatePalette },
    { "gdi32.DLL", "DeleteDC", (void*)DeleteDC },
    { "gdi32.DLL", "DeleteObject", (void*)DeleteObject },
    { "gdi32.DLL", "GetDeviceCaps", (void*)GetDeviceCaps },
    { "gdi32.DLL", "GetObjectA", (void*)GetObjectA },
    { "gdi32.DLL", "GetStockObject", (void*)GetStockObject },
    { "gdi32.DLL", "GetSystemPaletteEntries", (void*)GetSystemPaletteEntries },
    { "gdi32.DLL", "RealizePalette", (void*)RealizePalette },
    { "gdi32.DLL", "SelectPalette", (void*)SelectPalette },
    { "gdi32.DLL", "SetBkColor", (void*)SetBkColor },
    { "gdi32.DLL", "SetTextColor", (void*)SetTextColor },
    { "gdi32.DLL", "StretchDIBits", (void*)StretchDIBits },
    { "gdi32.DLL", "UnrealizeObject", (void*)UnrealizeObject },
    { "winmm.dll", "joyGetDevCapsA", (void*)joyGetDevCapsA },
    { "winmm.dll", "joyGetNumDevs", (void*)joyGetNumDevs },
    { "winmm.dll", "joyGetPosEx", (void*)joyGetPosEx },
    { "winmm.dll", "midiOutGetNumDevs", (void*)midiOutGetNumDevs },
    { "winmm.dll", "midiOutPrepareHeader", (void*)midiOutPrepareHeader },
    { "winmm.dll", "midiOutReset", (void*)midiOutReset },
    { "winmm.dll", "midiOutSetVolume", (void*)midiOutSetVolume },
    { "winmm.dll", "midiOutUnprepareHeader", (void*)midiOutUnprepareHeader },
    { "winmm.dll", "midiStreamClose", (void*)midiStreamClose },
    { "winmm.dll", "midiStreamOpen", (void*)midiStreamOpen },
    { "winmm.dll", "midiStreamOut", (void*)midiStreamOut },
    { "winmm.dll", "midiStreamPause", (void*)midiStreamPause },
    { "winmm.dll", "midiStreamProperty", (void*)midiStreamProperty },
    { "winmm.dll", "midiStreamRestart", (void*)midiStreamRestart },
    { "winmm.dll", "timeGetTime", (void*)timeGetTime },
    { "winmm.DLL", "joyGetDevCapsA", (void*)joyGetDevCapsA },
    { "winmm.DLL", "joyGetNumDevs", (void*)joyGetNumDevs },
    { "winmm.DLL", "joyGetPosEx", (void*)joyGetPosEx },
    { "winmm.DLL", "midiOutGetNumDevs", (void*)midiOutGetNumDevs },
    { "winmm.DLL", "midiOutPrepareHeader", (void*)midiOutPrepareHeader },
    { "winmm.DLL", "midiOutReset", (void*)midiOutReset },
    { "winmm.DLL", "midiOutSetVolume", (void*)midiOutSetVolume },
    { "winmm.DLL", "midiOutUnprepareHeader", (void*)midiOutUnprepareHeader },
    { "winmm.DLL", "midiStreamClose", (void*)midiStreamClose },
    { "winmm.DLL", "midiStreamOpen", (void*)midiStreamOpen },
    { "winmm.DLL", "midiStreamOut", (void*)midiStreamOut },
    { "winmm.DLL", "midiStreamPause", (void*)midiStreamPause },
    { "winmm.DLL", "midiStreamProperty", (void*)midiStreamProperty },
    { "winmm.DLL", "midiStreamRestart", (void*)midiStreamRestart },
    { "winmm.DLL", "timeGetTime", (void*)timeGetTime },
    { "advapi32.dll", "RegCloseKey", (void*)RegCloseKey },
    { "advapi32.dll", "GetUserNameA", (void*)GetUserNameA },
    { "advapi32.dll", "RegCreateKeyA", (void*)RegCreateKeyA },
    { "advapi32.dll", "RegDeleteKeyA", (void*)RegDeleteKeyA },
    { "advapi32.dll", "RegEnumKeyExA", (void*)RegEnumKeyExA },
    { "advapi32.dll", "RegOpenKeyA", (void*)RegOpenKeyA },
    { "advapi32.dll", "RegQueryValueExA", (void*)RegQueryValueExA },
    { "advapi32.dll", "RegSetValueExA", (void*)RegSetValueExA },
    { "advapi32.DLL", "RegCloseKey", (void*)RegCloseKey },
    { "advapi32.DLL", "GetUserNameA", (void*)GetUserNameA },
    { "advapi32.DLL", "RegCreateKeyA", (void*)RegCreateKeyA },
    { "advapi32.DLL", "RegDeleteKeyA", (void*)RegDeleteKeyA },
    { "advapi32.DLL", "RegEnumKeyExA", (void*)RegEnumKeyExA },
    { "advapi32.DLL", "RegOpenKeyA", (void*)RegOpenKeyA },
    { "advapi32.DLL", "RegQueryValueExA", (void*)RegQueryValueExA },
    { "advapi32.DLL", "RegSetValueExA", (void*)RegSetValueExA },
    { "dplay.dll", "DPCreate", (void*)DPCreate },
    { "dplay.dll", "DirectPlayEnumerateA", (void*)DirectPlayEnumerateA },
    { "dplay.DLL", "DPCreate", (void*)DPCreate },
    { "dplay.DLL", "DirectPlayEnumerateA", (void*)DirectPlayEnumerateA },
    { "ddraw.DLL", "DirectDrawCreate", (void*)DirectDrawCreate },
    { "dsound.DLL", "DirectSoundCreate", (void*)DirectSoundCreate },
    { NULL, NULL, NULL }
};

size_t import_table_count = sizeof(import_table) / sizeof(import_entry_t) - 1;

void set_import(const char *name, void *address)
{
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, name) == 0) {
            import_table[i].address = address;
            return;
        }
    }
    DEBUG("ERROR: set_import: symbol '%s' not found", name);
}

#ifndef MY_WINE32
static int import_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const import_entry_t *)a)->name,
                  ((const import_entry_t *)b)->name);
}
#endif

/* Standalone 32-bit: can't use strcmp (libc TLS not initialized) */
#if defined(MY_WINE32)
static int import_entry_cmp_nolibc(const char *a, const char *b)
{
    unsigned char ua, ub;
    while (*a && *b) {
        ua = (unsigned char)*a;
        ub = (unsigned char)*b;
        if (ua != ub) return ua - ub;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

int import_cmp_by_name(const void *key, const void *elem)
{
#if defined(MY_WINE32)
    return import_entry_cmp_nolibc((const char *)key, ((const import_entry_t *)elem)->name);
#else
    return strcmp((const char *)key, ((const import_entry_t *)elem)->name);
#endif
}

/**
 * Sort import_table by name for bsearch.
 */
void init_import_table(void)
{
    /* Sort import_table by name for bsearch. Exclude the sentinel entry. */
    size_t count = sizeof(import_table) / sizeof(import_entry_t) - 1;
#if defined(MY_WINE32)
    /* Standalone 32-bit: can't use qsort/strcmp (libc TLS not initialized).
     * Use insertion sort with local strcmp that doesn't need libc. */
    for (size_t i = 1; i < count; i++) {
        import_entry_t key = import_table[i];
        size_t j = i;
        while (j > 0 && import_entry_cmp_nolibc(import_table[j-1].name, key.name) > 0) {
            import_table[j] = import_table[j-1];
            j--;
        }
        import_table[j] = key;
    }
#else
    qsort(import_table, count, sizeof(import_entry_t), import_entry_cmp);
#endif
}

/**
 * Build a flat array of (ILT value, resolved address, func name) from
 * all import descriptors, in DLL order.
 * Returns the number of flat entries created.
 */
int build_flat_import_array(void *base, IMAGE_NT_HEADERS *nt,
                            struct import_flat flat[])
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) return 0;
    if (imp_dir.VirtualAddress == 0 ||
        !pe_rva_range_is_valid(imp_dir.VirtualAddress, imp_dir.Size,
                               pe_size_of_image(nt))) {
        return 0;
    }
    uint32_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc_start = pe_rva_to_ptr(base, nt, import_rva,
                                                        sizeof(IMAGE_IMPORT_DESCRIPTOR));
    if (desc_start == NULL) return 0;

    bool is32 = pe_is_pe32(nt);
    int num_flat = 0;
    IMAGE_IMPORT_DESCRIPTOR *desc = desc_start;
    uint32_t desc_offset = 0;
    while (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
           desc->Name != 0 && num_flat < MAX_FLAT_IMPORTS) {
        if (!pe_rva_range_is_valid(desc->Name, 1, pe_size_of_image(nt)))
            break;
        const char *dll_name = (const char *)base + desc->Name;

        if (is32) {
            uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                               ? desc->u1.OriginalFirstThunk
                               : desc->FirstThunk;
            IMAGE_THUNK_DATA32 *orig_thunks = pe_rva_to_ptr(base, nt, ilt_rva,
                                                            sizeof(IMAGE_THUNK_DATA32));
            IMAGE_THUNK_DATA32 *iath = pe_rva_to_ptr(base, nt, desc->FirstThunk,
                                                     sizeof(IMAGE_THUNK_DATA32));
            if (orig_thunks == NULL || iath == NULL)
                break;

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                size_t thunk_off = (size_t)i * sizeof(IMAGE_THUNK_DATA32);
                if (thunk_off / sizeof(IMAGE_THUNK_DATA32) != (size_t)i ||
                    thunk_off > SIZE_MAX - sizeof(IMAGE_THUNK_DATA32) ||
                    !pe_rva_range_is_valid(ilt_rva, thunk_off + sizeof(IMAGE_THUNK_DATA32),
                                           pe_size_of_image(nt)) ||
                    !pe_rva_range_is_valid(desc->FirstThunk, thunk_off + sizeof(IMAGE_THUNK_DATA32),
                                           pe_size_of_image(nt))) {
                    return num_flat;
                }
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = (uint64_t)(uint32_t)iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x80000000) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = pe_rva_to_ptr(base, nt,
                        orig_thunks[i].AddressOfData, sizeof(IMAGE_IMPORT_BY_NAME));
                    if (imp_name == NULL)
                        return num_flat;
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        } else {
            uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                               ? desc->u1.OriginalFirstThunk
                               : desc->FirstThunk;
            IMAGE_THUNK_DATA64 *orig_thunks = pe_rva_to_ptr(base, nt, ilt_rva,
                                                            sizeof(IMAGE_THUNK_DATA64));
            IMAGE_THUNK_DATA64 *iath = pe_rva_to_ptr(base, nt, desc->FirstThunk,
                                                     sizeof(IMAGE_THUNK_DATA64));
            if (orig_thunks == NULL || iath == NULL)
                break;

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                size_t thunk_off = (size_t)i * sizeof(IMAGE_THUNK_DATA64);
                if (thunk_off / sizeof(IMAGE_THUNK_DATA64) != (size_t)i ||
                    thunk_off > SIZE_MAX - sizeof(IMAGE_THUNK_DATA64) ||
                    !pe_rva_range_is_valid(ilt_rva, thunk_off + sizeof(IMAGE_THUNK_DATA64),
                                           pe_size_of_image(nt)) ||
                    !pe_rva_range_is_valid(desc->FirstThunk, thunk_off + sizeof(IMAGE_THUNK_DATA64),
                                           pe_size_of_image(nt))) {
                    return num_flat;
                }
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    if (orig_thunks[i].AddressOfData > UINT32_MAX)
                        return num_flat;
                    IMAGE_IMPORT_BY_NAME *imp_name = pe_rva_to_ptr(base, nt,
                        (uint32_t)orig_thunks[i].AddressOfData, sizeof(IMAGE_IMPORT_BY_NAME));
                    if (imp_name == NULL)
                        return num_flat;
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        }
        desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
        desc = pe_rva_to_ptr(base, nt, import_rva + desc_offset,
                             sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if (desc == NULL)
            break;
    }

    return num_flat;
}

/**
 * Strategy 1: target overlaps with a resolved import address.
 * Only check the flat entry whose iat_addr matches the target IAT entry.
 * Does NOT write -- the value is already correct (from pass 1 IAT).
 */
bool strategy_resolved_overlap(uint64_t current_val, void *target_ptr,
                               struct import_flat *flat, int num_flat)
{
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr == current_val) {
            return true;
        }
    }
    return false;
}

/**
 * Strategy 2: ILT entry value equals a resolved address.
 * Only match the flat entry whose iat_addr matches the target IAT entry.
 * If current_val matches an ilt_value whose resolved_addr is set,
 * write the resolved_addr to the target location.
 */
bool strategy_ilt_value_match(void *target_ptr, uint64_t current_val,
                              uint64_t target, size_t thunk_size,
                              struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr &&
            flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }
    return false;
}

/**
 * Strategy 3: Direct IAT address lookup via per-descriptor iat_addr.
 * Find the flat entry whose iat_addr matches the target IAT entry address.
 * This provides exact per-descriptor IAT range awareness -- each entry
 * knows which DLL's IAT it belongs to by its iat_addr.
 */
bool strategy_ilt_offset_match(void *target_ptr, uint64_t target,
                               uint64_t current_val, size_t thunk_size,
                               struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;

    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }

    return false;
}
