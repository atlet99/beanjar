// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Counts the frames LG's camera service writes to a preview's shared memory (startPreview's key),
 * in the guest: tests/regress/virtcam/run-camera.py builds it with the webOS SDK and runs it as
 * root.
 *
 *   shm-frames KEY SECONDS
 *
 * The segment is the service's SysV ring: a header of ints, unit_num frame lengths, then unit_num
 * slots of unit_size bytes. webOS OSE's (com.webos.service.camera,
 * src/common/lib/ipc_shared_memory.cpp) header is six ints (write_index, read_index, unit_size,
 * meta_size, unit_num, mark); LG's webOS 6 camera2 has five, without meta_size (seen: 3 -1 3687424
 * 8 0, then the lengths). The layout is told from where a plausible unit_num is. The writer
 * advances write_index once per frame; this polls it every millisecond for SECONDS and counts the
 * advances, and checks each new frame: a JPEG starts FF D8 and ends FF D9, a raw frame has the
 * length asked. Prints KEY=VALUE lines. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: shm-frames KEY SECONDS\n");
        return 2;
    }
    int id = shmget((key_t)strtol(argv[1], NULL, 0), 0, 0);
    if (id < 0) {
        perror("shm_error=shmget");
        return 1;
    }
    volatile unsigned char *base = shmat(id, NULL, SHM_RDONLY);
    if (base == (void *)-1) {
        perror("shm_error=shmat");
        return 1;
    }
    volatile int *hdr = (volatile int *)base;
    /* five ints: unit_num at 3 and a mark (0-2) at 4; six: meta_size at 3, unit_num at 4 */
    const int five = hdr[3] > 0 && hdr[3] <= 64 && hdr[4] >= 0 && hdr[4] <= 2;
    const int header = five ? 5 : 6;
    const int unit_size = hdr[2], unit_num = five ? hdr[3] : hdr[4];
    printf("unit_size=%d\nunit_num=%d\nshm_header_ints=%d\n", unit_size, unit_num, header);
    if (unit_num <= 0 || unit_num > 64 || unit_size <= 0)
        return 1;
    volatile unsigned int *lengths = (volatile unsigned int *)(base + header * sizeof(int));
    volatile unsigned char *data = base + header * sizeof(int) + 4 * unit_num;
    const double end = now() + atof(argv[2]);
    long frames = 0, jpeg = 0, bad = 0;
    unsigned long long bytes = 0;
    unsigned int last_length = 0;
    int last = hdr[0];
    const double t0 = now();
    while (now() < end) {
        int w = hdr[0];
        if (w != last && w >= 0 && w < unit_num) {
            int n = last < 0 ? 1 : (w - last + unit_num) % unit_num;
            /* the newest whole frame: the slot before write_index (the writer may still be in the
             * one at it). The first write takes write_index from -1 to 0, and the slot before it
             * has never been written (length 0): that one is counted and not checked. */
            int slot = (w - 1 + unit_num) % unit_num;
            unsigned int len = lengths[slot];
            volatile unsigned char *f = data + (size_t)slot * unit_size;
            if (last < 0) {
                /* not checked */
            } else if (len >= 4 && len <= (unsigned)unit_size && f[0] == 0xff && f[1] == 0xd8 &&
                       f[len - 2] == 0xff && f[len - 1] == 0xd9) {
                jpeg++;
            } else if (len == 0 || len > (unsigned)unit_size) {
                bad++;
            }
            frames += n;
            bytes += len;
            last_length = len;
            last = w;
        }
        struct timespec ms = {0, 1000000};
        nanosleep(&ms, NULL);
    }
    const double secs = now() - t0;
    printf("shm_frames=%ld\nshm_seconds=%.2f\nshm_fps=%.2f\nshm_jpeg=%ld\nshm_bad=%ld\n"
           "shm_last_length=%u\nshm_bytes=%llu\n",
           frames, secs, frames / secs, jpeg, bad, last_length, bytes);
    shmdt((const void *)base);
    return 0;
}
