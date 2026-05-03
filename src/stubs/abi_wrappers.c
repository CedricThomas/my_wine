#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
__attribute__((sysv_abi)) void *sysv_malloc(size_t s) { return malloc(s); }
__attribute__((sysv_abi)) void *sysv_calloc(size_t n, size_t s) { return calloc(n, s); }
__attribute__((sysv_abi)) void sysv_free(void *p) { free(p); }
__attribute__((sysv_abi)) void *sysv_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
__attribute__((sysv_abi)) size_t sysv_strlen(const char *s) { return strlen(s); }
__attribute__((sysv_abi)) int sysv_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
__attribute__((sysv_abi)) void *sysv_mmap(void *a, size_t l, int p, int f, int d, off_t o) { return mmap(a, l, p, f, d, o); }
__attribute__((sysv_abi)) int sysv_mprotect(void *a, size_t l, int p) { return mprotect(a, l, p); }
