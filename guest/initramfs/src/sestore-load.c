// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Load a host-validated snapshot before firmware init. No TOML parser and no key diagnostics. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../../include/webos-sestore.h"

int main(int argc, char **argv)
{
    int source = -1, device = -1, result = 1;
    unsigned char *data = NULL;
    struct stat info;
    size_t done = 0, size = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: sestore-load BUNDLE\n");
        return 2;
    }
    source = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source < 0 || fstat(source, &info))
        goto fail;
    if (!S_ISREG(info.st_mode) || (info.st_mode & 077) || info.st_size < 8 ||
        info.st_size > WEBOS_SESTORE_MAX_BUNDLE) {
        errno = EINVAL;
        goto fail;
    }
    size = (size_t)info.st_size;
    data = malloc(size);
    if (!data)
        goto fail;
    while (done < size) {
        ssize_t n = read(source, data + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (!n)
                errno = EIO;
            goto fail;
        }
        done += (size_t)n;
    }
    device = open(WEBOS_SESTORE_DEVICE, O_RDONLY | O_CLOEXEC);
    if (device < 0)
        goto fail;
    struct webos_sestore_load request = {.data = (uintptr_t)data, .bytes = (unsigned int)size};
    if (ioctl(device, WEBOS_SESTORE_LOAD, &request))
        goto fail;
    result = 0;
    goto out;
fail:
    fprintf(stderr, "sestore-load: provisioning failed: %s\n", strerror(errno));
out:
    if (source >= 0)
        close(source);
    if (device >= 0)
        close(device);
    if (data) {
        explicit_bzero(data, size);
        free(data);
    }
    return result;
}
