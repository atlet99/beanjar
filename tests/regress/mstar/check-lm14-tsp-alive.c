// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    if (!dlopen("/lib/libpthread.so.0", RTLD_LAZY | RTLD_GLOBAL))
        return 1;
    const char *names[] = {"libMsOS.so", "libdrvMMIO.so", "libapiDMX.so"};
    void *h[3];
    char path[160];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "/mnt/vendor/%s", names[i]);
        h[i] = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
        if (!h[i]) {
            puts(dlerror());
            return 2;
        }
    }
    int (*init)(void) = dlsym(h[1], "MDrv_MMIO_Init");
    int (*base)(uint32_t *, uint32_t *, uint32_t) = dlsym(h[1], "MDrv_MMIO_GetBASE");
    void (*bank)(uint32_t, uint32_t) = dlsym(h[2], "HAL_TSP_SetBank");
    void (*control)(uint32_t, uint32_t, uint32_t) = dlsym(h[2], "HAL_TSP_SetCtrlMode");
    int (*alive)(void) = dlsym(h[2], "HAL_TSP_Alive");
    uint32_t a = 0, b = 0, size = 0;
    if (!init || !base || !bank || !control || !alive)
        return 3;
    if (!init() || !base(&a, &size, 0x124) || !base(&b, &size, 0x10b))
        return 4;
    bank(a, b);
    control(0, 3, 0);
    printf("TSP bank=%08x ctrl=%04x\n", a, *(volatile uint16_t *)(a + 0x2be8));
    int count = argc > 1 ? atoi(argv[1]) : 1, failed = 0;
    if (count < 1 || count > 100)
        return 6;
    for (int i = 0; i < count; i++) {
        if (argc > 2)
            usleep((i % 7) * 37);
        struct timeval before, after;
        gettimeofday(&before, 0);
        int ret = alive();
        gettimeofday(&after, 0);
        printf("call=%d HAL_TSP_Alive=%d elapsed_us=%ld cmd_hi=%04x data_hi=%04x\n", i, ret,
               (long)((after.tv_sec - before.tv_sec) * 1000000 + after.tv_usec - before.tv_usec),
               *(volatile uint16_t *)(a + 0x2b04), *(volatile uint16_t *)(a + 0x2c04));
        if (!ret)
            failed++;
    }
    return failed ? 5 : 0;
}
