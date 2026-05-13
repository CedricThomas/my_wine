/*
 * wrapper_main.c — standalone PE-type detector and dispatcher
 *
 * Reads a PE file, determines whether it is PE32 or PE32+,
 * then execs the matching backend binary (my_wine32 or my_wine64)
 * found as a sibling in the same directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

/* Return values for detect_pe_type */
#define PE_TYPE_NONE  0
#define PE_TYPE_32    1
#define PE_TYPE_64    2

/*
 * resolve_exe_dir — write the directory of the running executable
 * into buf (NUL-terminated). Returns buf on success, NULL on failure.
 *
 * Uses readlink("/proc/self/exe") then strrchr to trim the filename.
 */
static char *resolve_exe_dir(char *buf, size_t bufsz)
{
    ssize_t len = readlink("/proc/self/exe", buf, bufsz - 1);
    if (len <= 0) return NULL;
    buf[len] = '\0';

    char *slash = strrchr(buf, '/');
    if (!slash) return NULL;
    *(slash + 1) = '\0';  /* keep the trailing '/' */
    return buf;
}

/*
 * detect_pe_type — open a file and inspect PE headers via raw byte reads.
 *
 * Returns:
 *   PE_TYPE_32  (1) — PE32  (OptionalHeader Magic == 0x10B)
 *   PE_TYPE_64  (2) — PE32+ (OptionalHeader Magic == 0x20B)
 *   PE_TYPE_NONE (0) — not a PE file
 */
static int detect_pe_type(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return PE_TYPE_NONE;

    /* 1. MZ magic at offset 0 */
    unsigned char mz[2];
    if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, mz, 2) != 2) {
        close(fd);
        return PE_TYPE_NONE;
    }
    if (mz[0] != 'M' || mz[1] != 'Z') {
        close(fd);
        return PE_TYPE_NONE;
    }

    /* 2. e_lfanew at offset 0x3C (little-endian uint32) */
    unsigned char buf4[4];
    if (lseek(fd, 0x3C, SEEK_SET) != 0x3C || read(fd, buf4, 4) != 4) {
        close(fd);
        return PE_TYPE_NONE;
    }
    uint32_t e_lfanew = (uint32_t)buf4[0]
                      | ((uint32_t)buf4[1] << 8)
                      | ((uint32_t)buf4[2] << 16)
                      | ((uint32_t)buf4[3] << 24);

    /* 3. PE signature at e_lfanew */
    unsigned char pesig[2];
    if (lseek(fd, (off_t)e_lfanew, SEEK_SET) != (off_t)e_lfanew
        || read(fd, pesig, 2) != 2) {
        close(fd);
        return PE_TYPE_NONE;
    }
    if (pesig[0] != 'P' || pesig[1] != 'E') {
        close(fd);
        return PE_TYPE_NONE;
    }

    /* 4. OptionalHeader Magic at e_lfanew + 24 (2 bytes, little-endian)
     *    COFF file header is 20 bytes after the PE sig,
     *    then the OptionalHeader starts; Magic is the first 2 bytes.
     *    Offset = e_lfanew + 4 ("PE\0\0") + 20 (COFF header) = e_lfanew + 24
     */
    unsigned char magic[2];
    uint32_t magic_off = e_lfanew + 24;
    if (lseek(fd, (off_t)magic_off, SEEK_SET) != (off_t)magic_off
        || read(fd, magic, 2) != 2) {
        close(fd);
        return PE_TYPE_NONE;
    }
    uint16_t opt_magic = (uint16_t)magic[0] | ((uint16_t)magic[1] << 8);

    close(fd);

    if (opt_magic == 0x10B) {
        return PE_TYPE_32;
    } else if (opt_magic == 0x20B) {
        return PE_TYPE_64;
    }
    return PE_TYPE_NONE;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pe_binary>\n", argv[0]);
        return 126;
    }

    int pe_type = detect_pe_type(argv[1]);

    if (pe_type == PE_TYPE_NONE) {
        fprintf(stderr, "my_wine: %s: not a PE binary (PE32 or PE32+)\n", argv[1]);
        return 126;
    }

    /* Resolve sibling backend path */
    char dir[4096];
    if (!resolve_exe_dir(dir, sizeof(dir))) {
        fprintf(stderr, "my_wine: could not determine own directory\n");
        return 126;
    }

    const char *backend = (pe_type == PE_TYPE_32) ? "my_wine32" : "my_wine64";
    char *slash = strrchr(dir, '/');
    /* Replace the filename component with the backend name. Use snprintf for bounds safety. */
    size_t remaining = sizeof(dir) - (size_t)(slash + 1 - dir);
    snprintf(slash + 1, remaining, "%s", backend);

    /* Build a new argv: new_argv[0] = backend path, new_argv[1..] = original args */
    char **new_argv = malloc(sizeof(char *) * (argc + 1));
    if (!new_argv) {
        perror("my_wine: malloc");
        return 127;
    }
    new_argv[0] = dir;         /* backend path becomes argv[0] */
    for (int i = 1; i < argc; i++) {
        new_argv[i] = argv[i]; /* pass through PE path + any user args */
    }
    new_argv[argc] = NULL;

    /* execvp the backend — child sees argv[0]=backend, argv[1]=PE path, argv[2..]=user args */
    execvp(dir, new_argv);

    /* Only reached on exec failure */
    perror("my_wine: execvp");
    return 127;
}
