// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Selected LM14 CVBS trim protocol; virtual zero policy is explicit in
 * adc_idac. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void)
{
    const unsigned result[] = {0x4101, 0x4104, 0x4105};
    uint32_t map[] = {0x1f000000, 0x5000};
    int fd = open("/webos/dev/miomap", O_RDWR);
    if (fd < 0 || ioctl(fd, 0x40084901, map))
        return 1;
    volatile uint8_t *p = mmap(0, map[1], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
        return 2;
    p[0x4098] = 0;
    p[0x409c] = 0x24;
    for (unsigned i = 0; i < 3; i++)
        p[result[i]] = 0xa5;
    usleep(50000);
    for (unsigned i = 0; i < 3; i++)
        if (p[result[i]] != 0xa5)
            return 3;
    p[0x409c] = 0x25;
    p[0x4098] = 1;
    usleep(50000);
    if (p[0x4098] != 1)
        return 4;
    for (unsigned i = 0; i < 3; i++)
        if (p[result[i]] != 0xa5)
            return 5;
    for (unsigned repeat = 0; repeat < 3; repeat++) {
        p[0x4098] = 0;
        for (unsigned i = 0; i < 3; i++)
            p[result[i]] = 0xa5;
        p[0x409c] = 0x24;
        p[0x4098] = 0x81;
        for (unsigned tries = 0; (p[0x4098] & 1) && tries < 100; tries++)
            usleep(1000);
        if (p[0x4098] != 0x80)
            return 6;
        for (unsigned i = 0; i < 3; i++)
            if (p[result[i]])
                return 7;
    }
    p[0x4098] = 0;
    munmap((void *)p, map[1]);
    close(fd);
    puts("Virtual IDAC no-start/unsupported/publication/preserved-bits/repeat "
         "PASS");
    return 0;
}
