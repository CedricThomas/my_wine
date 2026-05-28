#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "handle_manager.h"

static int g_failures = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static void test_std_handles(void)
{
    T(wine_handle_get_type(0) == HANDLE_TYPE_FILE, "stdin slot type mismatch");
    T(wine_handle_get_type(1) == HANDLE_TYPE_FILE, "stdout slot type mismatch");
    T(wine_handle_get_type(2) == HANDLE_TYPE_FILE, "stderr slot type mismatch");
    T((int)(uintptr_t)wine_handle_get(0) == STDIN_FILENO, "stdin slot object mismatch");
    T((int)(uintptr_t)wine_handle_get(1) == STDOUT_FILENO, "stdout slot object mismatch");
    T((int)(uintptr_t)wine_handle_get(2) == STDERR_FILENO, "stderr slot object mismatch");

    wine_handle_free(0);
    wine_handle_free(1);
    wine_handle_free(2);
    T((int)(uintptr_t)wine_handle_get(0) == STDIN_FILENO, "stdin slot should survive free");
    T((int)(uintptr_t)wine_handle_get(1) == STDOUT_FILENO, "stdout slot should survive free");
    T((int)(uintptr_t)wine_handle_get(2) == STDERR_FILENO, "stderr slot should survive free");
}

static void test_refcount_and_reuse(void)
{
    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_EVENT, (void *)(uintptr_t)0x1234u);

    T(handle >= 3, "allocated handle should not use reserved std slots");
    T(wine_handle_get_type((uint32_t)handle) == HANDLE_TYPE_EVENT,
      "allocated handle type mismatch");
    T((uintptr_t)wine_handle_get((uint32_t)handle) == (uintptr_t)0x1234u,
      "allocated handle object mismatch");

    wine_handle_add_ref((uint32_t)handle);
    wine_handle_free((uint32_t)handle);
    T(wine_handle_get_type((uint32_t)handle) == HANDLE_TYPE_EVENT,
      "handle should remain live after one free with extra ref");

    wine_handle_free((uint32_t)handle);
    T(wine_handle_get((uint32_t)handle) == NULL, "handle object should clear on final free");
    T(wine_handle_get_type((uint32_t)handle) == 0, "handle type should clear on final free");

    uint64_t reused = wine_handle_alloc(HANDLE_TYPE_MUTEX, (void *)(uintptr_t)0x5678u);
    T(reused == handle, "freed handle slot should be reused");
    T(wine_handle_get_type((uint32_t)reused) == HANDLE_TYPE_MUTEX,
      "reused handle type mismatch");
    T((uintptr_t)wine_handle_get((uint32_t)reused) == (uintptr_t)0x5678u,
      "reused handle object mismatch");
    wine_handle_free((uint32_t)reused);
}

static void test_invalid_handles(void)
{
    T(wine_handle_get(HANDLE_TABLE_SIZE) == NULL, "out-of-range get should return NULL");
    T(wine_handle_get_type(HANDLE_TABLE_SIZE) == 0, "out-of-range type should return zero");
    wine_handle_add_ref(HANDLE_TABLE_SIZE);
    wine_handle_free(HANDLE_TABLE_SIZE);
}

int main(void)
{
    wine_handle_manager_init();

    test_std_handles();
    test_refcount_and_reuse();
    test_invalid_handles();

    if (g_failures == 0)
        printf("PASS: handle manager\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
