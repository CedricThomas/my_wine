#define _GNU_SOURCE

#include "kernel32_priv.h"

#define CP_ACP 0

KERNEL32_STUB
int lstrlenA(const char *lpString)
{
    const char *s = lpString;
    while (*s)
        s++;
    return (int)(s - lpString);
}

KERNEL32_STUB
char *lstrcpyA(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;
    if (d == NULL || s == NULL)
        return FORCE_PTR_RETURN(NULL);
    do {
        *d++ = *s++;
    } while (s[-1] != '\0');
    return FORCE_PTR_RETURN(dest);
}

KERNEL32_STUB
char *lstrcatA(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;
    if (d == NULL)
        return FORCE_PTR_RETURN(NULL);
    if (s == NULL)
        return FORCE_PTR_RETURN(dest);
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = '\0';
    return FORCE_PTR_RETURN(dest);
}

KERNEL32_STUB
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte)
{
    (void)code_page;
    (void)byte;
    return 0;
}

KERNEL32_STUB
int MultiByteToWideChar(uint32_t code_page, uint32_t dw_flags,
                        const char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpWideCharStr, int cchWideChar)
{
    uint16_t *out = (uint16_t *)lpWideCharStr;
    int count = 0;
    int i;

    (void)code_page;
    (void)dw_flags;

    if (lpMultiByteStr == NULL)
        return 0;

    if (cbMultiByteChar < 0) {
        cbMultiByteChar = 0;
        while (lpMultiByteStr[cbMultiByteChar] != '\0')
            cbMultiByteChar++;
        cbMultiByteChar++;
    }

    if (cchWideChar == 0 || out == NULL)
        return cbMultiByteChar;

    count = (cbMultiByteChar < cchWideChar) ? cbMultiByteChar : cchWideChar;
    for (i = 0; i < count; i++)
        out[i] = (uint8_t)lpMultiByteStr[i];

    return count;
}

KERNEL32_STUB
int WideCharToMultiByte(uint32_t code_page, uint32_t dw_flags,
                        const void *lpWideCharStr, int cchWideChar,
                        char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpDefaultChar, void *lpUsedDefaultChar)
{
    const uint16_t *src = (const uint16_t *)lpWideCharStr;
    int count = 0;
    int i;

    if (code_page != 0 && code_page != 1252 && code_page != CP_ACP)
        return 0;
    (void)dw_flags;
    (void)lpDefaultChar;
    (void)lpUsedDefaultChar;

    if (src == NULL)
        return 0;

    if (cchWideChar < 0) {
        cchWideChar = 0;
        while (src[cchWideChar] != 0)
            cchWideChar++;
        cchWideChar++;
    }

    if (cbMultiByteChar == 0 || lpMultiByteStr == NULL)
        return cchWideChar;

    count = (cchWideChar < cbMultiByteChar) ? cchWideChar : cbMultiByteChar;
    for (i = 0; i < count; i++) {
        uint16_t ch = src[i];
        lpMultiByteStr[i] = (ch <= 0xffu) ? (char)ch : '?';
    }

    return count;
}
