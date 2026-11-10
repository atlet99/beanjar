// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* rtk-main.c:node_ioctl / rtk-vsc.c retains a label through lgemu_count. V4L2 per-open
 * state is freed by rtk_vsc_release; labels must survive close and reuse.
 * Only QUERYCAP is issued: rtk_vsc_ioctl currently returns zero for it without
 * filling capabilities or changing scaler state. No capability data is assumed.
 * Run against two actually configured v4l2_vsc nodes in an isolated guest:
 *   check-v4l2-counter-lifetime /dev/video30 /dev/video31 [cycles]
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHURN      16
#define MAX_TABLE  (128 * 1024)
#define MAX_CYCLES 4096
static const char *names[2];
static unsigned long expected[2];

static int read_counts(unsigned long counts[2])
{
    FILE *file = fopen("/proc/rtk/ioctl", "r");
    char line[512], name[128], trailing;
    size_t bytes = 0;
    unsigned int cmd;
    unsigned long count;
    int seen[2] = {0, 0};
    counts[0] = counts[1] = 0;
    if (!file) {
        perror("open /proc/rtk/ioctl");
        return -1;
    }
    while (fgets(line, sizeof(line), file)) {
        bytes += strlen(line);
        if (bytes > MAX_TABLE || !strchr(line, '\n')) {
            fprintf(stderr, "counter snapshot exceeds probe bounds\n");
            fclose(file);
            return -1;
        }
        if (sscanf(line, "%127s ioctl %x: %lu %c", name, &cmd, &count, &trailing) != 3 ||
            cmd != (unsigned int)VIDIOC_QUERYCAP)
            continue;
        for (int i = 0; i < 2; i++) {
            if (strcmp(name, names[i]))
                continue;
            if (seen[i]++) {
                fprintf(stderr, "duplicate QUERYCAP counter for %s\n", names[i]);
                fclose(file);
                return -1;
            }
            counts[i] = count;
        }
    }
    if (ferror(file)) {
        perror("read /proc/rtk/ioctl");
        fclose(file);
        return -1;
    }
    if (fclose(file)) {
        perror("close counter snapshot");
        return -1;
    }
    return 0;
}

static int check_counts(void)
{
    unsigned long counts[2];
    if (read_counts(counts))
        return -1;
    for (int i = 0; i < 2; i++) {
        if (counts[i] < expected[i]) {
            fprintf(stderr, "%s QUERYCAP count lost after close: %lu < %lu\n", names[i], counts[i],
                    expected[i]);
            return -1;
        }
    }
    return 0;
}

static int query_and_close(const char *path, int index)
{
    struct v4l2_capability cap = {0};
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        perror("open configured V4L2 node");
        return -1;
    }
    int result = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    int saved_errno = errno;
    if (close(fd)) {
        perror("close V4L2 node");
        return -1;
    }
    if (result) {
        errno = saved_errno;
        perror("QUERYCAP (stand-in contract returns zero)");
        return -1;
    }
    expected[index]++;
    return check_counts();
}

static int churn(const char *other)
{
    int fds[CHURN], opened = 0, result = -1;
    for (; opened < CHURN; opened++) {
        fds[opened] = open(other, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fds[opened] < 0) {
            perror("open churn V4L2 node");
            goto out;
        }
    }
    /* Allocations for the other node can reuse the just-closed node's slab.
     * Read while replacements are alive, and again after all are closed. */
    result = check_counts();
out:
    while (opened) {
        if (close(fds[--opened])) {
            perror("close churn node");
            result = -1;
        }
    }
    if (!result)
        result = check_counts();
    return result;
}

int main(int argc, char **argv)
{
    unsigned long cycles = 256;
    char *end;
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s NODE_A NODE_B [cycles: 1-%d]\n", argv[0], MAX_CYCLES);
        return 2;
    }
    for (int i = 0; i < 2; i++) {
        names[i] = strrchr(argv[i + 1], '/');
        names[i] = names[i] ? names[i] + 1 : argv[i + 1];
        if (!*names[i]) {
            fprintf(stderr, "node basename is empty\n");
            return 2;
        }
    }
    if (!strcmp(names[0], names[1])) {
        fprintf(stderr, "two different configured V4L2 nodes are required\n");
        return 2;
    }
    if (argc == 4) {
        errno = 0;
        cycles = strtoul(argv[3], &end, 10);
        if (errno || !*argv[3] || *end || cycles < 1 || cycles > MAX_CYCLES) {
            fprintf(stderr, "invalid cycle count\n");
            return 2;
        }
    }
    if (read_counts(expected))
        return 1;
    unsigned long baseline[2] = {expected[0], expected[1]};
    for (unsigned long n = 0; n < cycles; n++) {
        if (query_and_close(argv[1], 0) || churn(argv[2]) || query_and_close(argv[2], 1) ||
            churn(argv[1]))
            return 1;
    }
    if (check_counts())
        return 1;
    printf("PASS: %lu QUERYCAP open/ioctl/close cycles per node; labels/counts survive %lu churn "
           "opens\n",
           cycles, cycles * 2 * CHURN);
    printf("%s: baseline %lu, required >= %lu; %s: baseline %lu, required >= %lu\n", names[0],
           baseline[0], expected[0], names[1], baseline[1], expected[1]);
    return 0;
}
