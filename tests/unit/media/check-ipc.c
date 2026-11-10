// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include "../../../host/media/ipc.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static void check_buffer_construction(void)
{
    struct hm_buffer buffer;
    assert(hm_buffer_create(&buffer, 0) == -1 && errno == EINVAL);
    assert(buffer.fd == -1 && !buffer.data && !buffer.size);
    assert(hm_buffer_create(&buffer, (size_t)HOST_MEDIA_MAX_BYTES + 1) == -1 && errno == EINVAL);
    assert(!hm_buffer_create(&buffer, 4096));
    int fd = buffer.fd;
    hm_buffer_discard(&buffer); /* A failed/abandoned copy owns no FD or mapping. */
    assert(buffer.fd == -1 && !buffer.data && !buffer.size);
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    assert(!hm_buffer_create(&buffer, 4096));
    fd = buffer.fd;
    void *other = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(other != MAP_FAILED);
    /* An extra writable alias makes publication fail closed. The caller owns
     * this test alias; finish still closes its FD and unmaps its own view. */
    assert(hm_buffer_finish(&buffer) == -1 && errno == EBUSY);
    assert(buffer.fd == -1 && !buffer.data && !buffer.size);
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    assert(!munmap(other, 4096));
    assert(!hm_buffer_create(&buffer, 4096));
    memset(buffer.data, 0x5a, buffer.size);
    fd = hm_buffer_finish(&buffer);
    assert(fd >= 0 && buffer.fd == -1 && !buffer.data && !buffer.size);
    int seals = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
    assert((fcntl(fd, F_GET_SEALS) & seals) == seals);
    assert(mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED && errno == EPERM);
    unsigned char byte;
    assert(pread(fd, &byte, 1, 4095) == 1 && byte == 0x5a);
    close(fd);
}

int main(void)
{
    check_buffer_construction();
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair));
    const unsigned char pixels[] = {1, 2, 3, 4, 5, 6};
    int fd = hm_memfd(pixels, sizeof pixels);
    assert(fd >= 0);
    assert(pwrite(fd, pixels, 1, 0) == -1 && errno == EPERM);
    assert(ftruncate(fd, 1) == -1 && errno == EPERM);
    struct host_media_message m = {.magic = HOST_MEDIA_MAGIC,
                                   .version = HOST_MEDIA_VERSION,
                                   .type = HM_VIDEO,
                                   .generation = 7,
                                   .token = 19,
                                   .format = HM_YUV420,
                                   .width = 2,
                                   .height = 2,
                                   .objects = 1,
                                   .layers = 1};
    m.object[0].size = sizeof pixels;
    m.layer[0].planes = 3;
    m.layer[0].plane[0] = (struct host_media_plane){0, 2, 0};
    m.layer[0].plane[1] = (struct host_media_plane){0, 1, 4};
    m.layer[0].plane[2] = (struct host_media_plane){0, 1, 5};
    assert(hm_validate_frame(&m, 1));
    assert(hm_send(pair[0], &m, &fd, 1) == 1);
    close(fd); /* SCM_RIGHTS receiver owns a real independent reference. */
    struct host_media_message received;
    int fds[HOST_MEDIA_OBJECTS];
    unsigned n;
    assert(hm_receive(pair[1], &received, fds, &n) == 1 && n == 1);
    assert(hm_validate_frame(&received, n));
    assert(fcntl(fds[0], F_GETFD) & FD_CLOEXEC);
    void *p = mmap(NULL, sizeof pixels, PROT_READ, MAP_SHARED, fds[0], 0);
    assert(p != MAP_FAILED && !memcmp(p, pixels, sizeof pixels));
    hm_close_fds(fds, n);
    assert(!memcmp(p, pixels, sizeof pixels)); /* Mapping owns its pages. */
    munmap(p, sizeof pixels);
    m.layer[0].plane[2].offset = UINT64_MAX;
    assert(!hm_validate_frame(&m, 1));
    m.layer[0].plane[2].offset = 5;
    m.layer[0].plane[0].stride = UINT32_MAX;
    assert(!hm_validate_frame(&m, 1));
    assert(!hm_validate_frame(&m, 0));
    assert(send(pair[0], &m, sizeof m - 1, 0) == sizeof m - 1);
    assert(hm_receive(pair[1], &received, fds, &n) == -1 && errno == EPROTO && n == 0);
    unsigned char oversized[sizeof m + 1];
    memcpy(oversized, &m, sizeof m);
    assert(send(pair[0], oversized, sizeof oversized, 0) == sizeof oversized);
    assert(hm_receive(pair[1], &received, fds, &n) == -1 && errno == EPROTO && n == 0);
    close(pair[0]);
    close(pair[1]);
    puts("PASS immutable construction/seal failure cleanup, descriptor bounds, FD/mapping lifetime "
         "and truncated datagrams");
    return 0;
}
