// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Sequential read timing. Caller controls guest page-cache state. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
static unsigned char buffer[131072];
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    double start = now();
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    uint64_t bytes = 0;
    ssize_t n;
    while ((n = read(fd, buffer, sizeof buffer)) > 0)
        bytes += n;
    if (n < 0) {
        perror("read");
        return 1;
    }
    close(fd);
    printf("read path=%s bytes=%llu seconds=%.6f\n", argv[1], (unsigned long long)bytes,
           now() - start);
    return bytes != 25198624;
}
