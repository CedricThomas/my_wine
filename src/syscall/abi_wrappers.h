#ifndef ABI_WRAPPERS_H
#define ABI_WRAPPERS_H
#include <stddef.h>
#include <sys/mman.h>
/* SysV wrappers for libc calls from ms_abi functions */
void *sysv_malloc(size_t);
void *sysv_calloc(size_t, size_t);
void sysv_free(void *);
void *sysv_memcpy(void *, const void *, size_t);
size_t sysv_strlen(const char *);
int sysv_strncmp(const char *, const char *, size_t);
void *sysv_mmap(void *, size_t, int, int, int, off_t);
int sysv_mprotect(void *, size_t, int);
#endif
