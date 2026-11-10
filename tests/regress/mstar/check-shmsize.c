// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* W21A MsOS_GetSHMSize: the source UP7560 reports 10 MiB. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(void)
{
    uint32_t size = 0;
    int fd = open("/dev/semutex", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    int r = ioctl(fd, 0x8004530b, &size);
    close(fd);
    if (r) {
        perror("query");
        return 1;
    }
    printf("semutex shared-memory capacity: 0x%x bytes\n", size);
    return size == 0xa00000 ? 0 : 1;
}
