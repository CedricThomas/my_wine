#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "include/syscall_safe_utils.h"

int g_debug_level = 0;

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static void check(const char *label, int condition)
{
    total_tests++;
    if (condition) {
        printf("  PASS: %s\n", label);
        passed_tests++;
    } else {
        printf("  FAIL: %s\n", label);
        failed_tests++;
    }
}

static int test_summary(void)
{
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    return failed_tests == 0 ? 0 : 1;
}

static void test_strings(void)
{
    char buf[5];

    check("strlen empty", syscall_safe_strlen("") == 0);
    check("strlen normal", syscall_safe_strlen("wine") == 4);
    check("strcmp equal", syscall_safe_strcmp("abc", "abc") == 0);
    check("strcmp orders", syscall_safe_strcmp("abc", "abd") < 0);
    check("strcasecmp ignores ASCII case",
          syscall_safe_strcasecmp("Kernel32.DLL", "kernel32.dll") == 0);
    check("strncmp zero length", syscall_safe_strncmp("a", "b", 0) == 0);
    check("strchr finds terminator", syscall_safe_strchr("abc", '\0') != NULL);
    check("strrchr finds last", syscall_safe_strrchr("abca", 'a') == "abca" + 3);

    syscall_safe_copy_str(buf, "abcdef", sizeof(buf));
    check("copy_str truncates", strcmp(buf, "abcd") == 0);
    syscall_safe_copy_str(buf, "xy", sizeof(buf));
    check("copy_str terminates", strcmp(buf, "xy") == 0);
}

static void test_memory_and_ranges(void)
{
    unsigned char src[4] = {1, 2, 3, 4};
    unsigned char dst[4] = {0, 0, 0, 0};
    size_t out = 0;

    check("memcpy returns destination", syscall_safe_memcpy(dst, src, sizeof(src)) == dst);
    check("memcpy copies bytes", memcmp(dst, src, sizeof(src)) == 0);
    syscall_safe_memset(dst, 0x5a, sizeof(dst));
    check("memset writes bytes", dst[0] == 0x5a && dst[3] == 0x5a);

    check("add overflow rejects", syscall_safe_add_overflow_size((size_t)-1, 1, &out) != 0);
    check("add overflow accepts", syscall_safe_add_overflow_size(7, 5, &out) == 0 && out == 12);
    check("range valid accepts end", syscall_safe_range_valid_size(7, 3, 10));
    check("range valid rejects overflow", !syscall_safe_range_valid_size(8, 3, 10));
}

static void test_format_and_guest_writes(void)
{
    char hex[9];
    unsigned char area[16] = {0};

    syscall_safe_format_hex(hex, 0x1a2b3c4d, 8);
    hex[8] = '\0';
    check("format_hex fixed width", strcmp(hex, "1a2b3c4d") == 0);

    syscall_safe_guest_write_ptr(area, 0, (void *)(uintptr_t)0x1122334455667788ULL, 1);
    check("guest_write_ptr 32-bit truncates", *(uint32_t *)area == 0x55667788U);

    syscall_safe_guest_write_ptr(area, 0, (void *)(uintptr_t)0x1122334455667788ULL, 0);
    check("guest_write_ptr 64-bit writes full", *(uint64_t *)area == 0x1122334455667788ULL);

    syscall_safe_guest_write_u8(area, 9, 0xab);
    check("guest_write_u8 writes byte", area[9] == 0xab);
}

int main(void)
{
    test_strings();
    test_memory_and_ranges();
    test_format_and_guest_writes();
    return test_summary();
}
