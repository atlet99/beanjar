// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* ARM32 ABI probe for stock O22N2 libvtcapture's video60 path.
 * libvtcapture.dis: setFMT fc78, plane properties fd88/ff44,
 * requestBuffer 10300, queryBuffer 10e58 (video60 mmap at 10f62),
 * dequeueBuffer 111f4, streamOnOff 11590. Raw words deliberately preserve
 * the measured ABI rather than the build host's native V4L2 struct layout. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define QUERYCAP  0x80685600UL
#define G_FMT     0xc0cc5604UL
#define S_FMT     0xc0cc5605UL
#define REQBUFS   0xc0145608UL
#define QUERYBUF  0xc0445609UL
#define QBUF      0xc044560fUL
#define DQBUF     0xc0445611UL
#define STREAMON  0x40045612UL
#define STREAMOFF 0x40045613UL
#define G_EXT     0xc0185647UL
#define S_EXT     0xc0185648UL
#define BASE      0x00982a00U
/* Standard multi-plane FOURCCs: linux/videodev2.h, not LG enum 1/4. */
#define FOURCC(a, b, c, d)                                                                         \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define NV12M   FOURCC('N', 'M', '1', '2')
#define NV16M   FOURCC('N', 'M', '1', '6')
#define WIDTH   320U
#define HEIGHT  180U
#define Y_SIZE  (WIDTH * HEIGHT)
#define UV_SIZE (Y_SIZE / 2)
static unsigned passed, failed;
static void check(int ok, const char *name)
{
    if (ok) {
        ++passed;
        printf("PASS %s\n", name);
    } else {
        ++failed;
        fprintf(stderr, "FAIL %s (errno %d: %s)\n", name, errno, strerror(errno));
    }
}
static int call(int fd, unsigned long cmd, void *p)
{
    int n;
    do {
        n = ioctl(fd, cmd, p);
    } while (n < 0 && errno == EINTR);
    return n;
}
static int ext(int fd, unsigned long cmd, unsigned id, void *data, unsigned size)
{
    uint32_t control[5] = {id, size, 0, (uint32_t)(uintptr_t)data, 0};
    uint32_t group[6] = {0x00980000, 1, 0, 0, 0, (uint32_t)(uintptr_t)control};
    return call(fd, cmd, group);
}
static void buffer(uint32_t b[17], uint32_t p[2][15], unsigned index)
{
    memset(b, 0, 68);
    memset(p, 0, 120);
    b[0] = index;
    b[1] = 9;
    b[12] = 1;
    b[13] = (uint32_t)(uintptr_t)p;
    b[14] = 2;
}
static void rejected(int fd, unsigned long cmd, void *p, int error, const char *name)
{
    int n = call(fd, cmd, p);
    check(n == -1 && errno == error, name);
}
static uint32_t hash(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261U;
    while (n--)
        h = (h ^ *p++) * 16777619U;
    return h;
}
static int prop_unchanged(int fd, const uint32_t before[4])
{
    uint32_t after[4] = {0};
    return !ext(fd, G_EXT, BASE + 3, after, 16) && !memcmp(before, after, 16);
}
/* A one-buffer stream makes pinning observable: while frozen there is no
 * eligible dequeue buffer, and unfreezing allows the next fresh capture. */
static void nv16_and_freeze(void)
{
    int fd = open("/dev/video60", O_RDWR);
    if (fd < 0) {
        check(0, "NV16M probe open");
        return;
    }
    unsigned char *map[2] = {NULL, NULL};
    uint32_t fmt[51] = {9, WIDTH, HEIGHT, NV16M};
    uint32_t prop[4] = {3, 0, WIDTH | (HEIGHT << 16), 1};
    uint32_t req[5] = {1, 9, 1, 0, 0}, type = 9;
    uint32_t b[17], planes[2][15];
    int streaming = 0;
    if (call(fd, S_FMT, fmt) || ext(fd, S_EXT, BASE + 3, prop, 16) || call(fd, REQBUFS, req)) {
        check(0, "NV16M one-buffer setup");
        goto cleanup;
    }
    buffer(b, planes, 0);
    if (call(fd, QUERYBUF, b)) {
        check(0, "NV16M QUERYBUF");
        goto cleanup;
    }
    check(planes[0][1] == Y_SIZE && planes[1][1] == Y_SIZE, "NV16M two full-height planes");
    for (unsigned p = 0; p < 2; ++p) {
        map[p] = mmap(NULL, Y_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, planes[p][2]);
        if (map[p] == MAP_FAILED) {
            map[p] = NULL;
            check(0, "NV16M plane mmap");
            goto cleanup;
        }
    }
    if (call(fd, QBUF, b) || call(fd, STREAMON, &type)) {
        check(0, "NV16M queue and stream");
        goto cleanup;
    }
    streaming = 1;
    buffer(b, planes, 0);
    if (call(fd, DQBUF, b)) {
        check(0, "NV16M fresh dequeue");
        goto cleanup;
    }
    uint32_t sequence = b[11];
    check(b[0] == 0 && (b[3] & 4) && planes[0][0] == Y_SIZE && planes[1][0] == Y_SIZE,
          "NV16M captured bytesused include full-height UV");
    unsigned nonuniform = 0;
    for (unsigned i = 1; i < Y_SIZE; ++i)
        nonuniform |= map[0][i] != map[0][0];
    check(nonuniform, "NV16M viewer frame is nonuniform");
    printf("NV16M sequence %u Y hash %08x UV hash %08x\n", sequence, hash(map[0], Y_SIZE),
           hash(map[1], Y_SIZE));
    if (call(fd, QBUF, b)) {
        check(0, "NV16M requeue before freeze");
        goto cleanup;
    }
    uint32_t freeze[2] = {0, 1}, state[2] = {0};
    if (ext(fd, S_EXT, BASE + 4, freeze, 8)) {
        check(0, "freeze captures fresh frame");
        goto cleanup;
    }
    check(!ext(fd, G_EXT, BASE + 4, state, 8) && state[0] == 0 && (state[1] & 255) == 1,
          "freeze readback pins buffer zero");
    buffer(b, planes, 0);
    if (call(fd, QUERYBUF, b)) {
        check(0, "frozen QUERYBUF");
        goto cleanup;
    }
    check(b[11] == sequence + 1 && planes[0][0] == Y_SIZE && planes[1][0] == Y_SIZE,
          "freeze itself takes one fresh NV16M frame");
    sequence = b[11];
    uint32_t y_hash = hash(map[0], Y_SIZE), uv_hash = hash(map[1], Y_SIZE);
    uint32_t invalid[2] = {1, 1};
    int result = ext(fd, S_EXT, BASE + 4, invalid, 8);
    check(result < 0 && errno == EINVAL, "out-of-range freeze index rejected");
    check(!ext(fd, G_EXT, BASE + 4, state, 8) && state[0] == 0 && (state[1] & 255) == 1,
          "invalid freeze preserves pinned index");
    buffer(b, planes, 0);
    rejected(fd, DQBUF, b, EAGAIN, "frozen single buffer cannot dequeue");
    buffer(b, planes, 0);
    check(!call(fd, QUERYBUF, b) && b[11] == sequence && hash(map[0], Y_SIZE) == y_hash &&
              hash(map[1], Y_SIZE) == uv_hash,
          "invalid freeze and failed dequeue preserve sequence and pixels");
    freeze[1] = 0;
    if (ext(fd, S_EXT, BASE + 4, freeze, 8)) {
        check(0, "unfreeze");
        goto cleanup;
    }
    check(!ext(fd, G_EXT, BASE + 4, state, 8) && state[0] == UINT32_MAX && !(state[1] & 255),
          "unfreeze readback has no pinned buffer");
    buffer(b, planes, 0);
    if (call(fd, DQBUF, b)) {
        check(0, "fresh dequeue after unfreeze");
        goto cleanup;
    }
    check(b[11] == sequence + 1 && planes[0][0] == Y_SIZE && planes[1][0] == Y_SIZE,
          "unfreeze resumes fresh NV16M capture without skipped sequence");
cleanup:
    if (streaming)
        check(!call(fd, STREAMOFF, &type), "NV16M STREAMOFF");
    for (unsigned p = 0; p < 2; ++p)
        if (map[p])
            munmap(map[p], Y_SIZE);
    close(fd);
}
int main(int argc, char **argv)
{
    int present = 0, disconnect = 0;
    const char *output = NULL;
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        puts("Usage: check-capture --absent|--present|--disconnect [output.nv12]\n"
             "Present requires a nonuniform viewer scene. Disconnect waits for a newline\n"
             "after STREAMON; disconnect the viewer first. Output is raw 320x180 NV12.");
        return argc < 2 ? 2 : 0;
    }
    if (!strcmp(argv[1], "--present"))
        present = 1;
    else if (!strcmp(argv[1], "--disconnect"))
        disconnect = 1;
    else if (strcmp(argv[1], "--absent"))
        return 2;
    if (argc > 3 || (argc == 3 && !present))
        return 2;
    if (argc == 3)
        output = argv[2];
    if (sizeof(void *) != 4) {
        fputs("ARM32 userspace required\n", stderr);
        return 77;
    }
    int fd = open("/dev/video60", O_RDWR);
    if (fd < 0) {
        perror("video60");
        return 1;
    }
    unsigned char cap[104] = {0};
    check(!call(fd, QUERYCAP, cap), "stock QUERYCAP");
    uint32_t capabilities[10] = {0};
    if (ext(fd, G_EXT, BASE, capabilities, 40)) {
        check(0, "capture capability 40 bytes");
        goto finish;
    }
    check(capabilities[8] == 2 && capabilities[9] == 1, "two planes and NV12 capability");
    uint32_t fmt[51] = {9, WIDTH, HEIGHT, 0};
    if (call(fd, S_FMT, fmt)) {
        check(0, "stock zero-pixelformat S_FMT");
        goto finish;
    }
    check(fmt[1] == WIDTH && fmt[2] == HEIGHT && fmt[3] == NV12M, "S_FMT returns NV12 dimensions");
    uint32_t unrelated[51] = {9, WIDTH, HEIGHT, FOURCC('Y', 'U', 'Y', 'V')};
    rejected(fd, S_FMT, unrelated, EOPNOTSUPP, "unrelated standard FOURCC rejected");
    uint32_t current[51] = {9};
    check(!call(fd, G_FMT, current) && current[1] == WIDTH && current[2] == HEIGHT &&
              current[3] == NV12M,
          "rejected FOURCC preserves format");
    uint32_t nv16[51] = {9, WIDTH, HEIGHT, NV16M};
    check(!call(fd, S_FMT, nv16) && nv16[3] == NV16M && nv16[6] == Y_SIZE && nv16[11] == Y_SIZE &&
              ((unsigned char *)nv16)[184] == 2,
          "explicit NV16M setup and plane sizes");
    memset(current, 0, sizeof current);
    current[0] = 9;
    check(!call(fd, G_FMT, current) && current[3] == NV16M && current[11] == Y_SIZE,
          "NV16M format readback");
    check(!ext(fd, G_EXT, BASE, capabilities, 40) && capabilities[9] == 4,
          "private capability keeps LG NV16 enum");
    uint32_t restore[51] = {9, WIDTH, HEIGHT, NV12M};
    if (call(fd, S_FMT, restore)) {
        check(0, "restore explicit NV12M");
        goto finish;
    }
    uint32_t zero[51] = {9, WIDTH, HEIGHT, 0};
    check(!call(fd, S_FMT, zero) && zero[3] == NV12M && zero[11] == UV_SIZE,
          "stock zero format retains current NV12M");
    uint32_t prop[4] = {3, 0, WIDTH | (HEIGHT << 16), 2}, readback[4] = {0};
    if (ext(fd, S_EXT, BASE + 3, prop, 16)) {
        check(0, "BLENDED plane properties");
        goto finish;
    }
    check(!ext(fd, G_EXT, BASE + 3, readback, 16) && !memcmp(prop, readback, 16),
          "plane property readback");
    /* Rejected configuration must preserve previously accepted properties. */
    const uint32_t invalid_dims[] = {0, 1, 321, 1922};
    for (unsigned i = 0; i < sizeof invalid_dims / sizeof invalid_dims[0]; ++i) {
        uint32_t bad[4];
        memcpy(bad, prop, 16);
        bad[2] = invalid_dims[i] | (HEIGHT << 16);
        int n = ext(fd, S_EXT, BASE + 3, bad, 16);
        check(n < 0 && errno == EINVAL && prop_unchanged(fd, prop),
              "invalid property width preserves state");
        uint32_t badfmt[51] = {9, invalid_dims[i], HEIGHT, 0};
        n = call(fd, S_FMT, badfmt);
        uint32_t actual[51] = {9};
        check(n < 0 && errno == EINVAL && !call(fd, G_FMT, actual) && actual[1] == WIDTH &&
                  actual[2] == HEIGHT && actual[3] == NV12M,
              "invalid format width preserves state");
    }
    const uint32_t invalid_heights[] = {0, 1, 181, 1082};
    for (unsigned i = 0; i < sizeof invalid_heights / sizeof invalid_heights[0]; ++i) {
        uint32_t bad[4];
        memcpy(bad, prop, 16);
        bad[2] = WIDTH | (invalid_heights[i] << 16);
        int n = ext(fd, S_EXT, BASE + 3, bad, 16);
        check(n < 0 && errno == EINVAL && prop_unchanged(fd, prop),
              "invalid property height preserves state");
        uint32_t badfmt[51] = {9, WIDTH, invalid_heights[i], 0};
        n = call(fd, S_FMT, badfmt);
        uint32_t actual[51] = {9};
        check(n < 0 && errno == EINVAL && !call(fd, G_FMT, actual) && actual[1] == WIDTH &&
                  actual[2] == HEIGHT && actual[3] == NV12M,
              "invalid format height preserves state");
    }
    uint32_t cropped[4];
    memcpy(cropped, prop, 16);
    cropped[1] = 2;
    int crop_result = ext(fd, S_EXT, BASE + 3, cropped, 16);
    check(crop_result < 0 && errno == EOPNOTSUPP && prop_unchanged(fd, prop),
          "nonzero crop origin preserves state");
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t bad[4];
        memcpy(bad, prop, 16);
        bad[0] = i ? 5 : 1;
        int n = ext(fd, S_EXT, BASE + 3, bad, 16);
        check(n < 0 && errno == EOPNOTSUPP && prop_unchanged(fd, prop),
              "unsupported location preserves state");
        memcpy(bad, prop, 16);
        bad[3] = i ? 6 : 0;
        n = ext(fd, S_EXT, BASE + 3, bad, 16);
        check(n < 0 && errno == EINVAL && prop_unchanged(fd, prop),
              "invalid requested count preserves state");
    }
    uint32_t physical[16] = {0}, crop[5] = {9}, control[2] = {BASE, 0};
    check(ext(fd, G_EXT, BASE + 6, physical, sizeof physical) < 0 && errno == EOPNOTSUPP,
          "unsupported physical memory info rejected");
    rejected(fd, 0x4014563cUL, crop, ENOTTY, "unsupported S_CROP rejected");
    rejected(fd, 0xc008561bUL, control, EOPNOTSUPP, "unsupported G_CTRL rejected");
    rejected(fd, 0xc008561cUL, control, EOPNOTSUPP, "unsupported S_CTRL rejected");
    uint32_t req[5] = {2, 9, 1, 0, 0};
    if (call(fd, REQBUFS, req)) {
        check(0, "REQBUFS two mmap buffers");
        goto finish;
    }
    check(req[0] == 2, "REQBUFS count");
    uint32_t info[5] = {0}, window[7] = {0};
    check(!ext(fd, G_EXT, BASE + 1, info, 20) && info[0] == WIDTH &&
              info[2] == (WIDTH | HEIGHT << 16),
          "PLANE_INFO stride and dimensions");
    int wn = ext(fd, G_EXT, BASE + 2, window, 28);
    check(present || disconnect ? wn == 0 && window[0] == 1 : wn < 0 && errno == ENODEV,
          "WINDOW_INFO reflects provider availability");
    unsigned char *map[2][2] = {{NULL}};
    size_t lengths[2] = {Y_SIZE, UV_SIZE};
    uint32_t b[17], planes[2][15];
    for (unsigned i = 0; i < 2; ++i) {
        buffer(b, planes, i);
        if (call(fd, QUERYBUF, b)) {
            check(0, "QUERYBUF");
            goto unmap;
        }
        check(planes[0][0] == 0 && planes[1][0] == 0 && planes[0][1] == Y_SIZE &&
                  planes[1][1] == UV_SIZE && !(b[3] & 4),
              "new buffers have no completed pixels");
        for (unsigned p = 0; p < 2; ++p) {
            map[i][p] =
                mmap(NULL, lengths[p], PROT_READ | PROT_WRITE, MAP_SHARED, fd, planes[p][2]);
            if (map[i][p] == MAP_FAILED) {
                map[i][p] = NULL;
                check(0, "plane mmap");
                goto unmap;
            }
        }
        if (!i) {
            long page = sysconf(_SC_PAGESIZE);
            size_t over = ((Y_SIZE + page - 1) / page + 1) * page;
            void *bad = mmap(NULL, over, PROT_READ, MAP_SHARED, fd, planes[0][2]);
            check(bad == MAP_FAILED && errno == EINVAL, "oversized plane mapping rejected");
            if (bad != MAP_FAILED)
                munmap(bad, over);
            b[14] = 1;
            rejected(fd, QBUF, b, EINVAL, "wrong plane array length rejected");
            b[14] = 2;
        }
        check(!call(fd, QBUF, b), "QBUF");
    }
    req[0] = 6;
    rejected(fd, REQBUFS, req, EINVAL, "invalid allocation count rejected");
    buffer(b, planes, 0);
    check(!call(fd, QUERYBUF, b) && (b[3] & 2) && planes[0][1] == Y_SIZE,
          "invalid allocation and plane length preserve queue");
    uint32_t type = 9;
    int on = call(fd, STREAMON, &type);
    if (!present && !disconnect) {
        check(on < 0 && errno == ENODEV, "absent viewer fails STREAMON closed");
    } else {
        if (on) {
            check(0, "STREAMON");
            goto unmap;
        }
        check(1, "STREAMON");
        if (disconnect) {
            puts("ready for viewer disconnect");
            fflush(stdout);
            if (getchar() == EOF) {
                check(0, "disconnect synchronization newline");
                goto unmap;
            }
            buffer(b, planes, 0);
            int n = call(fd, DQBUF, b);
            check(n < 0 && (errno == ENODEV || errno == ETIMEDOUT),
                  "disconnect DQBUF returns no frame");
        } else {
            uint32_t sequence = 0;
            for (unsigned frame = 0; frame < 2; ++frame) {
                buffer(b, planes, 0);
                if (call(fd, DQBUF, b)) {
                    check(0, "fresh DQBUF");
                    goto unmap;
                }
                check(b[0] < 2 && (b[3] & 4) && !(b[3] & 2) && planes[0][0] == Y_SIZE &&
                          planes[1][0] == UV_SIZE,
                      "completed NV12 plane sizes and flags");
                if (b[0] >= 2)
                    goto unmap;
                check(!frame || b[11] == sequence + 1, "fresh capture sequence increments");
                sequence = b[11];
                unsigned char *y = map[b[0]][0];
                unsigned nonuniform = 0;
                for (unsigned j = 1; j < Y_SIZE; ++j)
                    nonuniform |= y[j] != y[0];
                check(nonuniform, "viewer image is nonuniform");
                printf("frame %u sequence %u Y hash %08x UV hash %08x timestamp %u.%06u\n", frame,
                       sequence, hash(y, Y_SIZE), hash(map[b[0]][1], UV_SIZE), b[5], b[6]);
                if (!frame && output) {
                    FILE *f = fopen(output, "wb");
                    if (!f)
                        check(0, "open NV12 artifact");
                    else {
                        int ok = fwrite(y, 1, Y_SIZE, f) == Y_SIZE &&
                                 fwrite(map[b[0]][1], 1, UV_SIZE, f) == UV_SIZE;
                        int closed = fclose(f);
                        check(ok && !closed, "write NV12 artifact");
                    }
                }
                check(!call(fd, QBUF, b), "requeue captured buffer");
            }
        }
    }
    if (!present) {
        for (unsigned i = 0; i < 2; ++i) {
            buffer(b, planes, i);
            check(!call(fd, QUERYBUF, b) && !(b[3] & 4) && !planes[0][0] && !planes[1][0],
                  "failed capture never completes a buffer");
        }
    }
    check(!call(fd, STREAMOFF, &type), "STREAMOFF");
    /* A mapping retains its old allocation across REQBUFS(0), replacement
     * allocation at the same offset token, and final close. */
    map[0][0][0] = 0x5a;
    map[0][1][UV_SIZE - 1] = 0xa5;
    req[0] = 0;
    check(!call(fd, REQBUFS, req), "REQBUFS zero releases file ownership");
    req[0] = 2;
    check(!call(fd, REQBUFS, req), "replacement buffers allocate");
    buffer(b, planes, 0);
    if (!call(fd, QUERYBUF, b)) {
        unsigned char *replacement =
            mmap(NULL, Y_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, planes[0][2]);
        check(replacement != MAP_FAILED, "replacement mapping");
        if (replacement != MAP_FAILED) {
            replacement[0] = 0x33;
            check(map[0][0][0] == 0x5a, "replacement allocation does not alias retained mapping");
            munmap(replacement, Y_SIZE);
        }
    } else
        check(0, "replacement QUERYBUF");
    close(fd);
    fd = -1;
    check(map[0][0][0] == 0x5a && map[0][1][UV_SIZE - 1] == 0xa5,
          "mappings survive buffer release and close");
unmap:
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned p = 0; p < 2; ++p)
            if (map[i][p])
                munmap(map[i][p], lengths[p]);
finish:
    if (fd >= 0)
        close(fd);
    if (present && !failed)
        nv16_and_freeze();
    printf("capture checks: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
