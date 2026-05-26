#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_failures = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static int run_my_wine32(const char *pe_path, int use_env_fallback,
                         char *output, size_t output_size)
{
    int pipefd[2];
    pid_t pid;
    int status;
    size_t used = 0;

    if (pipe(pipefd) != 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        char *const argv_direct[] = { "./my_wine32", (char *)pe_path, NULL };
        char *const argv_env[] = { "./my_wine32", NULL };

        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (use_env_fallback) {
            setenv("WINE32_PE_PATH", pe_path, 1);
            execv("./my_wine32", argv_env);
        } else {
            unsetenv("WINE32_PE_PATH");
            execv("./my_wine32", argv_direct);
        }

        dprintf(STDERR_FILENO, "execv failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    while (used + 1 < output_size) {
        ssize_t n = read(pipefd[0], output + used, output_size - used - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        used += (size_t)n;
    }
    close(pipefd[0]);
    output[used] = '\0';

    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static void test_pe32_launch_mode(const char *label, int use_env_fallback)
{
    static const char *pe_path = "samples/entry_test_32/entry_test_32.exe";
    char output[8192];
    int rc = run_my_wine32(pe_path, use_env_fallback, output, sizeof(output));

    T(rc == 0, label);
    T(strstr(output, "Entry test: OK") != NULL,
      "entry_test_32 did not report success");
    T(strstr(output, "Entry test: cmdline=samples/entry_test_32/entry_test_32.exe") != NULL,
      "GetCommandLineA did not reflect the PE path");
}

int main(void)
{
    test_pe32_launch_mode("my_wine32 launch via argv failed", 0);
    test_pe32_launch_mode("my_wine32 launch via WINE32_PE_PATH fallback failed", 1);

    if (g_failures == 0)
        printf("PASS: pe32 launch path selection and command line seeding\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
