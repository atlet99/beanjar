// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Small deterministic ARM workloads; diagnostic only, not a webOS speed score. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(void)
{
    uint32_t x = 1, sum = 0;
    double t = now();
    for (unsigned i = 0; i < 100000000; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        sum += x;
    }
    printf("integer %.6f checksum=%u\n", now() - t, sum);
    unsigned n = 1 << 20;
    uint32_t *a = malloc(n * sizeof(*a));
    if (!a)
        return 1;
    for (unsigned i = 0; i < n; i++)
        a[i] = (i * 1664525u + 1013904223u) & (n - 1);
    x = 0;
    t = now();
    for (unsigned i = 0; i < 30000000; i++)
        x = a[x];
    printf("memory %.6f checksum=%u\n", now() - t, x);
    free(a);
    t = now();
    for (unsigned i = 0; i < 100000; i++)
        syscall(SYS_getpid);
    printf("syscall %.6f\n", now() - t);
    return 0;
}
