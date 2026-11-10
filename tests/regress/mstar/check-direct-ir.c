// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Exercise W21A's stock KADP read wrapper, not a copy of the kernel record
 * decoder. VM only: inject a NEC Right-button edge through lgemu's transport. */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
static void release_receiver(void)
{
    int fd = open("/dev/ir", O_RDWR), owner = 0;
    if (fd < 0)
        return;
    if (!ioctl(fd, _IOW('u', 10, int), &owner) && owner == getpid()) {
        owner = 0;
        ioctl(fd, _IOW('u', 9, int), &owner);
    }
    close(fd);
}
int main(void)
{
    void *lib = dlopen("libkadaptor.so", RTLD_NOW);
    if (!lib) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    void (*init)(void) = dlsym(lib, "MAdp_IR_Init");
    int (*read_key)(unsigned, uint8_t *, uint32_t *, uint8_t *, uint8_t *, uint8_t *) =
        dlsym(lib, "MAdp_GetKeyEventWithTimeout");
    if (!init || !read_key)
        return 1;
    atexit(release_receiver);
    init();
    int fd = open("/dev/lgemu-ir", O_WRONLY);
    if (fd < 0) {
        perror("open transport");
        return 1;
    }
    const uint8_t press[] = {0x49, 1, 0x04, 0x06}, release[] = {0x49, 0, 0x04, 0x06};
    uint8_t type = 0, key_type = 0, key_value = 0, tvlink[10] = {0};
    uint32_t raw = 0;
    /* A viewer may have queued buttons while no native receiver was running. */
    int pending = 0;
    for (; pending < 256; pending++)
        if (read_key(0, &type, &raw, tvlink, &key_type, &key_value))
            break;
    if (pending == 256) {
        fprintf(stderr, "FAIL receiver stayed busy before injection\n");
        close(fd);
        return 1;
    }
    if (write(fd, press, sizeof(press)) != sizeof(press))
        return 1;
    int r = read_key(25, &type, &raw, tvlink, &key_type, &key_value);
    write(fd, release, sizeof(release));
    close(fd);
    if (r || type != 1 || raw != UINT32_C(0xf906fb04)) {
        fprintf(stderr, "FAIL native IR read: return %d type %u frame %08x\n", r, type, raw);
        return 1;
    }
    puts("PASS native KADP IR read: NEC Right frame decoded");
    return 0;
}
