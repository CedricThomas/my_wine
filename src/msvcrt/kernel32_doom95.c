#define _GNU_SOURCE

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <asm/unistd.h>

#include "kernel32_priv.h"
#include "resource_win32.h"
#include "include/handle_manager.h"
#include "../loader/loader_state.h"
#include "../loader/module_list.h"
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL    0x80
#define FILE_TYPE_DISK           1
#define FILE_TYPE_CHAR           2
#define FILE_BEGIN               0
#define FILE_CURRENT             1
#define FILE_END                 2
#define TLS_SLOTS_MAX            64
#define LMEM_FIXED               0x0000
#define GMEM_FIXED               0x0000
#define CP_ACP                   0

typedef struct {
    uint16_t wProcessorArchitecture;
    uint16_t wReserved;
    uint32_t dwPageSize;
    void *lpMinimumApplicationAddress;
    void *lpMaximumApplicationAddress;
    uintptr_t dwActiveProcessorMask;
    uint32_t dwNumberOfProcessors;
    uint32_t dwProcessorType;
    uint32_t dwAllocationGranularity;
    uint16_t wProcessorLevel;
    uint16_t wProcessorRevision;
} SYSTEM_INFO_WINE;

typedef struct {
    uint32_t MaxCharSize;
    char DefaultChar[2];
    char LeadByte[12];
} CPINFO_WINE;

typedef struct {
    int32_t Bias;
    uint16_t StandardName[32];
    uint16_t StandardDate[8];
    int32_t StandardBias;
    uint16_t DaylightName[32];
    uint16_t DaylightDate[8];
    int32_t DaylightBias;
} TIME_ZONE_INFORMATION_WINE;

typedef struct {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} FTIME_WINE;

uint32_t g_tls_bitmap = 0;
void *g_tls_values[TLS_SLOTS_MAX];

static long wine_syscall3(long nr, long a0, long a1, long a2)
{
#if defined(__i386__)
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a0), "c"(a1), "d"(a2)
                     : "cc", "memory");
    return ret;
#else
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "cc", "memory");
    return ret;
#endif
}

static long wine_syscall2(long nr, long a0, long a1)
{
#if defined(__i386__)
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a0), "c"(a1)
                     : "cc", "memory");
    return ret;
#else
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1)
                     : "rcx", "r11", "cc", "memory");
    return ret;
#endif
}

static char g_process_current_directory[1024];

static void wine_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";

    len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void wine_init_process_directory(void)
{
    const char *pe_path;
    const char *slash;
    size_t len;

    if (g_process_current_directory[0] != '\0')
        return;

    pe_path = g_loader.pe_path;
    if (!pe_path || pe_path[0] == '\0') {
        g_process_current_directory[0] = '.';
        g_process_current_directory[1] = '\0';
        return;
    }

    slash = strrchr(pe_path, '/');
    if (!slash) {
        g_process_current_directory[0] = '.';
        g_process_current_directory[1] = '\0';
        return;
    }

    len = (size_t)(slash - pe_path);
    if (len == 0) {
        g_process_current_directory[0] = '/';
        g_process_current_directory[1] = '\0';
        return;
    }

    if (len >= sizeof(g_process_current_directory))
        len = sizeof(g_process_current_directory) - 1;
    memcpy(g_process_current_directory, pe_path, len);
    g_process_current_directory[len] = '\0';
}

const char *wine_get_current_directory(void)
{
    wine_init_process_directory();
    return g_process_current_directory;
}

int wine_resolve_path(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;

    if (!src || !dst || dst_size < 2)
        return 0;

    wine_init_process_directory();

    if (src[0] != '\0' && src[1] == ':') {
        src += 2;
        if (*src == '\\' || *src == '/')
            src++;
        dst[i++] = '/';
    } else if (*src == '/' || *src == '\\') {
        while (*src == '/' || *src == '\\')
            src++;
        dst[i++] = '/';
    } else {
        const char *cwd = wine_get_current_directory();
        while (*cwd && i + 1 < dst_size)
            dst[i++] = *cwd++;
        if (i == 0 || dst[i - 1] != '/')
            dst[i++] = '/';
    }

    while (*src && i + 1 < dst_size) {
        char ch = (*src == '\\') ? '/' : *src;
        if (ch == '/' && i > 0 && dst[i - 1] == '/') {
            src++;
            continue;
        }
        dst[i++] = ch;
        src++;
    }
    dst[i] = '\0';
    return 1;
}

int wine_set_current_directory(const char *path)
{
    char resolved[1024];
    struct stat st;

    if (!wine_resolve_path(path, resolved, sizeof(resolved)))
        return 0;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode))
        return 0;

    wine_copy_cstr(g_process_current_directory, sizeof(g_process_current_directory), resolved);
    return chdir(g_process_current_directory) == 0;
}

static void wine_unix_time_to_filetime(int64_t sec, int64_t nsec, FILETIME *out_ft)
{
    uint64_t value;

    if (!out_ft)
        return;

    value = (uint64_t)sec * 10000000ULL + (uint64_t)(nsec / 100);
    value += 116444736000000000ULL;
    out_ft->dwLowDateTime = (uint32_t)value;
    out_ft->dwHighDateTime = (uint32_t)(value >> 32);
}

static int wine_path_lookup_case_insensitive(const char *path, char *resolved, size_t resolved_size)
{
    char current[1024];
    const char *segment;
    struct stat st;

    if (!path || !resolved || resolved_size == 0)
        return 0;

    if (stat(path, &st) == 0) {
        wine_copy_cstr(resolved, resolved_size, path);
        return 1;
    }

    if (path[0] != '/')
        return 0;

    current[0] = '/';
    current[1] = '\0';
    segment = path + 1;

    while (*segment != '\0') {
        const char *next = segment;
        char wanted[256];
        size_t wanted_len = 0;
        DIR *dir;
        struct dirent *entry;
        const char *match = NULL;
        size_t current_len;

        while (*next != '\0' && *next != '/')
            next++;
        wanted_len = (size_t)(next - segment);
        if (wanted_len == 0) {
            segment = (*next == '/') ? next + 1 : next;
            continue;
        }
        if (wanted_len >= sizeof(wanted))
            return 0;
        memcpy(wanted, segment, wanted_len);
        wanted[wanted_len] = '\0';

        dir = opendir(current);
        if (!dir)
            return 0;

        while ((entry = readdir(dir)) != NULL) {
            if (strcasecmp(entry->d_name, wanted) == 0) {
                match = entry->d_name;
                break;
            }
        }

        if (!match) {
            closedir(dir);
            return 0;
        }

        current_len = strlen(current);
        if (current_len > 1) {
            if (current_len + 1 >= sizeof(current)) {
                closedir(dir);
                return 0;
            }
            current[current_len++] = '/';
            current[current_len] = '\0';
        }
        if (current_len + strlen(match) >= sizeof(current)) {
            closedir(dir);
            return 0;
        }
        memcpy(current + current_len, match, strlen(match) + 1);
        closedir(dir);

        segment = (*next == '/') ? next + 1 : next;
    }

    if (stat(current, &st) != 0)
        return 0;

    wine_copy_cstr(resolved, resolved_size, current);
    return 1;
}

static int wine_build_search_candidate(const char *directory, const char *filename,
                                       const char *extension, char *candidate,
                                       size_t candidate_size)
{
    const char *base = filename ? filename : "";
    int has_extension = 0;
    const char *scan;
    size_t used = 0;

    if (!candidate || candidate_size == 0)
        return 0;

    candidate[0] = '\0';
    if (directory && directory[0] != '\0') {
        used = snprintf(candidate, candidate_size, "%s", directory);
        if (used >= candidate_size)
            return 0;
        if (used > 0 && candidate[used - 1] != '/' && candidate[used - 1] != '\\') {
            if (used + 1 >= candidate_size)
                return 0;
            candidate[used++] = '\\';
            candidate[used] = '\0';
        }
    }

    if (used + strlen(base) >= candidate_size)
        return 0;
    memcpy(candidate + used, base, strlen(base) + 1);

    scan = strrchr(base, '\\');
    if (!scan)
        scan = strrchr(base, '/');
    scan = scan ? scan + 1 : base;
    has_extension = strrchr(scan, '.') != NULL;

    if (!has_extension && extension && extension[0] != '\0') {
        size_t ext_len = strlen(extension);
        if (used + strlen(base) + ext_len >= candidate_size)
            return 0;
        memcpy(candidate + used + strlen(base), extension, ext_len + 1);
    }

    return 1;
}

static int wine_search_existing_path(const char *directory, const char *filename,
                                     const char *extension, char *resolved,
                                     size_t resolved_size)
{
    char candidate[1024];
    char unix_path[1024];

    if (!wine_build_search_candidate(directory, filename, extension, candidate, sizeof(candidate)))
        return 0;
    if (!wine_resolve_path(candidate, unix_path, sizeof(unix_path)))
        return 0;
    return wine_path_lookup_case_insensitive(unix_path, resolved, resolved_size);
}

KERNEL32_STUB
uint32_t GetTickCount(void)
{
    struct timespec ts;

    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

KERNEL32_STUB
uint32_t GetCurrentThreadId(void)
{
    return 1;
}

KERNEL32_STUB
uint32_t GetCurrentProcessId(void)
{
    return 1;
}

KERNEL32_STUB
void *GetCurrentThread(void)
{
    return FORCE_PTR_RETURN((void *)(uintptr_t)0xfffffffeu);
}

KERNEL32_STUB
void GetSystemInfo(SYSTEM_INFO_WINE *info)
{
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->dwPageSize = PAGE_SIZE;
    info->lpMinimumApplicationAddress = (void *)(uintptr_t)0x10000;
    info->lpMaximumApplicationAddress = (void *)(uintptr_t)0x7ffeffffu;
    info->dwActiveProcessorMask = 1;
    info->dwNumberOfProcessors = 1;
    info->dwProcessorType = 586;
    info->dwAllocationGranularity = 65536;
    info->wProcessorLevel = 5;
}

KERNEL32_STUB
uint32_t GetVersion(void)
{
    return 0x80000004u;
}

KERNEL32_STUB
uint32_t GetTimeZoneInformation(TIME_ZONE_INFORMATION_WINE *tz)
{
    if (!tz)
        return 0xffffffffu;
    memset(tz, 0, sizeof(*tz));
    return 0;
}

KERNEL32_STUB
uint32_t GetCPInfo(uint32_t code_page, CPINFO_WINE *info)
{
    if (code_page != CP_ACP && code_page != 1252)
        return 0;
    if (!info)
        return 0;
    memset(info, 0, sizeof(*info));
    info->MaxCharSize = 1;
    info->DefaultChar[0] = '?';
    return 1;
}

KERNEL32_STUB
char *GetEnvironmentStrings(void)
{
    return GetEnvironmentStringsA();
}

KERNEL32_STUB
uint32_t TlsAlloc(void)
{
    uint32_t i;

    for (i = 0; i < TLS_SLOTS_MAX; i++) {
        uint32_t mask = 1u << i;
        if ((g_tls_bitmap & mask) == 0) {
            g_tls_bitmap |= mask;
            g_tls_values[i] = NULL;
            return i;
        }
    }
    return 0xffffffffu;
}

KERNEL32_STUB
int TlsFree(uint32_t dwTlsIndex)
{
    if (dwTlsIndex >= TLS_SLOTS_MAX)
        return 0;
    g_tls_bitmap &= ~(1u << dwTlsIndex);
    g_tls_values[dwTlsIndex] = NULL;
    return 1;
}

KERNEL32_STUB
int TlsSetValue(uint32_t dwTlsIndex, void *lpTlsValue)
{
    if (dwTlsIndex >= TLS_SLOTS_MAX || (g_tls_bitmap & (1u << dwTlsIndex)) == 0)
        return 0;
    g_tls_values[dwTlsIndex] = lpTlsValue;
    return 1;
}

KERNEL32_STUB
uint32_t GetModuleFileNameA(void *hModule, char *lpFilename, uint32_t nSize)
{
    const char *src = NULL;
    loaded_module_t *mod;
    uint32_t len = 0;

    if (!lpFilename || nSize == 0)
        return 0;

    if (!hModule || hModule == g_loader.image_base)
        src = g_loader.pe_path;
    else {
        mod = find_module_by_addr(hModule);
        if (mod)
            src = mod->name;
    }

    if (!src)
        src = g_loader.pe_path;

    while (src[len] && len + 1 < nSize) {
        lpFilename[len] = src[len];
        len++;
    }
    lpFilename[len] = '\0';
    return len;
}

KERNEL32_STUB
uint32_t GetFileAttributesA(const char *lpFileName)
{
    char path[1024];
    char resolved[1024];
    struct stat st;
    int rc;
    if (!lpFileName || lpFileName[0] == '\0')
        return 0xffffffffu;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "GetFileAttributesA('%s')\n", lpFileName);

    if (!wine_resolve_path(lpFileName, path, sizeof(path)))
        return 0xffffffffu;

    if (!wine_path_lookup_case_insensitive(path, resolved, sizeof(resolved)))
        return 0xffffffffu;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "GetFileAttributesA -> '%s'\n", resolved);

    rc = stat(resolved, &st);
    if (rc != 0)
        return 0xffffffffu;
    if (S_ISDIR(st.st_mode))
        return FILE_ATTRIBUTE_DIRECTORY;
    return FILE_ATTRIBUTE_NORMAL;
}

KERNEL32_STUB
uint32_t GetFileType(void *hFile)
{
    uint64_t handle = (uint64_t)(uintptr_t)hFile;

    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE || handle <= 2)
        return FILE_TYPE_CHAR;
    return FILE_TYPE_DISK;
}

KERNEL32_STUB
uint32_t GetFileSize(void *hFile, uint32_t *lpFileSizeHigh)
{
    struct stat st;
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);

    if (fd < 0 || INLINE_SYSCALL_FSTAT(fd, &st) != 0)
        return 0xffffffffu;

    if (lpFileSizeHigh)
        *lpFileSizeHigh = (uint32_t)(((uint64_t)st.st_size) >> 32);
    return (uint32_t)st.st_size;
}

KERNEL32_STUB
int GetFileTime(void *hFile, FILETIME *creation, FILETIME *access, FILETIME *write)
{
    struct stat st;
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);

    if (fd < 0 || INLINE_SYSCALL_FSTAT(fd, &st) != 0)
        return 0;

    if (creation)
        wine_unix_time_to_filetime(st.st_ctim.tv_sec, st.st_ctim.tv_nsec, creation);
    if (access)
        wine_unix_time_to_filetime(st.st_atim.tv_sec, st.st_atim.tv_nsec, access);
    if (write)
        wine_unix_time_to_filetime(st.st_mtim.tv_sec, st.st_mtim.tv_nsec, write);
    return 1;
}

KERNEL32_STUB
uint32_t SetFilePointer(void *hFile, int32_t lDistanceToMove, int32_t *lpDistanceToMoveHigh,
                        uint32_t dwMoveMethod)
{
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);
    int whence = SEEK_SET;
    long long distance = (uint32_t)lDistanceToMove;
    long long pos;

    if (lpDistanceToMoveHigh)
        distance |= ((long long)*lpDistanceToMoveHigh) << 32;

    if (fd < 0)
        return 0xffffffffu;

    if (dwMoveMethod == FILE_CURRENT)
        whence = SEEK_CUR;
    else if (dwMoveMethod == FILE_END)
        whence = SEEK_END;

    pos = wine_syscall3(__NR_lseek, fd, (long)distance, whence);
    if (pos < 0)
        return 0xffffffffu;

    if (lpDistanceToMoveHigh)
        *lpDistanceToMoveHigh = (int32_t)(((uint64_t)pos) >> 32);
    return (uint32_t)pos;
}

KERNEL32_STUB
int CreateDirectoryA(const char *lpPathName, void *lpSecurityAttributes)
{
    char path[1024];
    (void)lpSecurityAttributes;

    if (!wine_resolve_path(lpPathName, path, sizeof(path)))
        return 0;
    return wine_syscall2(__NR_mkdir, (long)path, 0755) == 0;
}

KERNEL32_STUB
void *LocalAlloc(uint32_t uFlags, uintptr_t uBytes)
{
    (void)uFlags;
    return FORCE_PTR_RETURN(HeapAlloc(GetProcessHeap(), LMEM_FIXED, uBytes));
}

KERNEL32_STUB
void *GlobalAlloc(uint32_t uFlags, uintptr_t uBytes)
{
    (void)uFlags;
    return FORCE_PTR_RETURN(HeapAlloc(GetProcessHeap(), GMEM_FIXED, uBytes));
}

KERNEL32_STUB
void *LocalFree(void *hMem)
{
    if (!hMem)
        return FORCE_PTR_RETURN(NULL);
    return FORCE_PTR_RETURN(HeapFree(GetProcessHeap(), 0, hMem) ? NULL : hMem);
}

KERNEL32_STUB
void *FindResourceA(void *hModule, const char *lpName, const char *lpType)
{
    return FORCE_PTR_RETURN(wine_resource_find(hModule, lpType, lpName, NULL));
}

KERNEL32_STUB
uint32_t SizeofResource(void *hModule, void *hResInfo)
{
    (void)hModule;
    return wine_resource_size(hResInfo);
}

KERNEL32_STUB
void *LoadResource(void *hModule, void *hResInfo)
{
    uint32_t handle = (uint32_t)(uintptr_t)hResInfo;
    (void)hModule;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_HRSRC)
        return FORCE_PTR_RETURN(NULL);
    if (!wine_handle_get(handle))
        return FORCE_PTR_RETURN(NULL);
    wine_handle_add_ref(handle);
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

KERNEL32_STUB
void *LockResource(void *hResData)
{
    return FORCE_PTR_RETURN((void *)wine_resource_lock(hResData));
}

KERNEL32_STUB
uint32_t SearchPathA(const char *lpPath, const char *lpFileName, const char *lpExtension,
                     uint32_t nBufferLength, char *lpBuffer, char **lpFilePart)
{
    char resolved[1024];
    char path_list[1024];
    const char *src = resolved;
    uint32_t len = 0;
    int found = 0;

    if (!lpFileName)
        return 0;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "SearchPathA(path='%s', file='%s', ext='%s')\n",
                lpPath ? lpPath : "", lpFileName, lpExtension ? lpExtension : "");

    if (strchr(lpFileName, '\\') || strchr(lpFileName, '/') ||
        (lpFileName[0] != '\0' && lpFileName[1] == ':')) {
        found = wine_search_existing_path(NULL, lpFileName, lpExtension, resolved, sizeof(resolved));
    } else {
        const char *segment;

        if (lpPath && lpPath[0] != '\0') {
            wine_copy_cstr(path_list, sizeof(path_list), lpPath);
            segment = path_list;
            while (*segment != '\0' && !found) {
                char *end = (char *)strchr(segment, ';');
                char saved = '\0';

                if (end) {
                    saved = *end;
                    *end = '\0';
                }
                if (segment[0] != '\0')
                    found = wine_search_existing_path(segment, lpFileName, lpExtension, resolved, sizeof(resolved));
                if (!end)
                    break;
                *end = saved;
                segment = end + 1;
            }
        }

        if (!found)
            found = wine_search_existing_path(NULL, lpFileName, lpExtension, resolved, sizeof(resolved));
    }

    if (!found)
        return 0;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "SearchPathA -> '%s'\n", resolved);

    while (src[len] != '\0')
        len++;

    if (!lpBuffer || nBufferLength == 0)
        return len;

    if (len + 1 > nBufferLength)
        len = nBufferLength - 1;
    memcpy(lpBuffer, src, len);
    lpBuffer[len] = '\0';
    if (lpFilePart) {
        char *slash = strrchr(lpBuffer, '/');
        *lpFilePart = slash ? slash + 1 : lpBuffer;
    }
    return len;
}

KERNEL32_STUB
int DeviceIoControl(void *hDevice, uint32_t dwIoControlCode, void *lpInBuffer,
                    uint32_t nInBufferSize, void *lpOutBuffer, uint32_t nOutBufferSize,
                    uint32_t *lpBytesReturned, void *lpOverlapped)
{
    (void)hDevice;
    (void)dwIoControlCode;
    (void)lpInBuffer;
    (void)nInBufferSize;
    (void)lpOutBuffer;
    (void)nOutBufferSize;
    (void)lpOverlapped;
    if (lpBytesReturned)
        *lpBytesReturned = 0;
    return 0;
}

KERNEL32_STUB
void *CreateThread(void *lpThreadAttributes, uintptr_t dwStackSize, void *lpStartAddress,
                   void *lpParameter, uint32_t dwCreationFlags, uint32_t *lpThreadId)
{
    uint32_t handle;
    (void)lpThreadAttributes;
    (void)dwStackSize;
    (void)lpStartAddress;
    (void)lpParameter;
    (void)dwCreationFlags;

    handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_THREAD, NULL);
    if (lpThreadId)
        *lpThreadId = handle;
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

KERNEL32_STUB
__attribute__((noreturn)) void ExitThread(uint32_t dwExitCode)
{
#ifdef MY_WINE32
    INLINE_SYSCALL_EXIT((int)dwExitCode);
#else
    _exit((int)dwExitCode);
#endif
}

KERNEL32_STUB
int DosDateTimeToFileTime(uint16_t wFatDate, uint16_t wFatTime, FILETIME *lpFileTime)
{
    uint32_t dos_time;
    uint64_t value;

    if (!lpFileTime)
        return 0;

    dos_time = ((uint32_t)wFatDate << 16) | wFatTime;
    value = 116444736000000000ULL + (uint64_t)dos_time * 10000000ULL;
    lpFileTime->dwLowDateTime = (uint32_t)value;
    lpFileTime->dwHighDateTime = (uint32_t)(value >> 32);
    return 1;
}

KERNEL32_STUB
int FileTimeToDosDateTime(const FILETIME *lpFileTime, uint16_t *lpFatDate, uint16_t *lpFatTime)
{
    if (!lpFileTime || !lpFatDate || !lpFatTime)
        return 0;
    *lpFatDate = 0;
    *lpFatTime = 0;
    return 1;
}

KERNEL32_STUB
int FileTimeToLocalFileTime(const FILETIME *lpFileTime, FILETIME *lpLocalFileTime)
{
    if (!lpFileTime || !lpLocalFileTime)
        return 0;
    *lpLocalFileTime = *lpFileTime;
    return 1;
}

KERNEL32_STUB
int LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, FILETIME *lpFileTime)
{
    if (!lpLocalFileTime || !lpFileTime)
        return 0;
    *lpFileTime = *lpLocalFileTime;
    return 1;
}

KERNEL32_STUB
int GetConsoleMode(void *hConsoleHandle, uint32_t *lpMode)
{
    (void)hConsoleHandle;
    if (lpMode)
        *lpMode = 0;
    return 1;
}

KERNEL32_STUB
int SetConsoleMode(void *hConsoleHandle, uint32_t dwMode)
{
    (void)hConsoleHandle;
    (void)dwMode;
    return 1;
}

KERNEL32_STUB
int SetStdHandle(uint32_t nStdHandle, void *hHandle)
{
    (void)nStdHandle;
    (void)hHandle;
    return 1;
}

KERNEL32_STUB
int WriteConsoleA(void *hConsoleOutput, const void *lpBuffer, uint32_t nNumberOfCharsToWrite,
                  uint32_t *lpNumberOfCharsWritten, void *lpReserved)
{
    return WriteFile(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite,
                     lpNumberOfCharsWritten, lpReserved);
}

KERNEL32_STUB
int ReadConsoleInputA(void *hConsoleInput, void *lpBuffer, uint32_t nLength,
                      uint32_t *lpNumberOfEventsRead)
{
    (void)hConsoleInput;
    (void)lpBuffer;
    (void)nLength;
    if (lpNumberOfEventsRead)
        *lpNumberOfEventsRead = 0;
    return 1;
}

KERNEL32_STUB
int FindNextFileA(void *hFindFile, void *lpFindFileData)
{
    wine_find_handle *find;
    DIR *dir;
    struct dirent *entry;
    char full_path[1400];
    struct stat st;
    uint32_t handle = (uint32_t)(uintptr_t)hFindFile;
    WIN32_FIND_DATAA_WINE *data = (WIN32_FIND_DATAA_WINE *)lpFindFileData;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_HGLOBAL || data == NULL)
        return 0;

    find = (wine_find_handle *)wine_handle_get(handle);
    if (find == NULL || find->dir == NULL)
        return 0;

    dir = (DIR *)find->dir;
    while ((entry = readdir(dir)) != NULL) {
        const char *pattern = find->pattern;
        const char *text = entry->d_name;
        int star = 0;
        int matched = 1;

        while (*pattern != '\0') {
            if (*pattern == '*') {
                star = 1;
                pattern++;
                if (*pattern == '\0') {
                    matched = 1;
                    break;
                }
                while (*text != '\0' &&
                       (((unsigned char)*pattern | 32) != ((unsigned char)*text | 32))) {
                    text++;
                }
                continue;
            }
            if (*text == '\0') {
                matched = 0;
                break;
            }
            if (*pattern != '?' &&
                (((unsigned char)*pattern | 32) != ((unsigned char)*text | 32))) {
                matched = 0;
                break;
            }
            pattern++;
            text++;
        }
        if (!matched || (!star && *text != '\0'))
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", find->directory, entry->d_name);
        if (stat(full_path, &st) != 0)
            continue;
        memset(data, 0, sizeof(*data));
        data->dwFileAttributes = S_ISDIR(st.st_mode) ?
            FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        data->nFileSizeLow = (uint32_t)st.st_size;
        data->nFileSizeHigh = (uint32_t)(((uint64_t)st.st_size) >> 32);
        strncpy(data->cFileName, entry->d_name, sizeof(data->cFileName) - 1);
        if (strcasestr(find->pattern, "wad"))
            fprintf(stderr, "FindNextFileA('%s') -> '%s'\n", find->pattern, data->cFileName);
        return 1;
    }

    return 0;
}
