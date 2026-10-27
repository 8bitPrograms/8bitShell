#include <stdio.h>

#include "putty.h"
#include <windows.h>
#include <bcrypt.h>

char *get_random_data(int len, const char *device)
{
    if (device) {
        fprintf(stderr, "puttygen: --random-device is not supported on Windows\n");
        return NULL;
    }

    char *buffer = snewn(len, char);
    const NTSTATUS status = BCryptGenRandom(
        NULL, (PUCHAR)buffer, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        smemclr(buffer, len);
        sfree(buffer);
        fprintf(stderr, "puttygen: BCryptGenRandom failed\n");
        return NULL;
    }
    return buffer;
}
