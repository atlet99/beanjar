// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* W21A's libkadaptor ABI: independently opened descriptors must share named
 * counting semaphores and permit parallel readers while excluding writers. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#define CMD(n) _IOR('S', n, int)
static pid_t child;
static void check(int ok, const char *s)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s (errno %d)\n", s, errno);
        if (child > 0) {
            kill(child, SIGKILL);
            waitpid(child, NULL, 0);
        }
        exit(1);
    }
}
int main(void)
{
    int a = open("/dev/semutex", O_RDWR), b = open("/dev/semutex", O_RDWR), id;
    struct {
        uint32_t count;
        char name[64];
    } sem = {.count = 1};
    char name[64];
    int timed[2];
    check(a >= 0 && b >= 0, "open two files");
    snprintf(sem.name, sizeof(sem.name), "named-sem-check-%ld", (long)getpid());
    id = ioctl(a, CMD(12), &sem);
    check(id >= 0x55660000, "semaphore index namespace");
    check(ioctl(b, CMD(12), &sem) == id, "same named semaphore across files");
    check(ioctl(a, CMD(14), &id) == 0, "take initial token");
    check(ioctl(b, CMD(14), &id) != 0, "empty semaphore does not acquire");
    check(ioctl(b, CMD(15), &id) == 0, "cross-file semaphore release");
    check(ioctl(a, CMD(13), &id) == 0, "blocking acquire after release");
    check(ioctl(a, CMD(15), &id) == 0, "return semaphore token");
    check(ioctl(a, CMD(15), &id) != 0, "reject unmatched release");
    check(ioctl(a, CMD(13), &id) == 0, "hold token for blocking check");
    child = fork();
    check(child >= 0, "fork blocking waiter");
    if (!child)
        _exit(ioctl(b, CMD(13), &id) == 0 ? 0 : 1);
    usleep(100000);
    int status;
    check(waitpid(child, &status, WNOHANG) == 0, "blocking acquire stays asleep without a token");
    check(ioctl(a, CMD(15), &id) == 0, "wake blocked waiter");
    pid_t done = 0;
    for (int retry = 0; retry < 100 && !done; retry++) {
        done = waitpid(child, &status, WNOHANG);
        if (!done)
            usleep(20000);
    }
    check(done == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "blocking waiter acquires released token");
    child = 0;
    check(ioctl(a, CMD(15), &id) == 0, "return waiter's token");
    check(ioctl(a, CMD(13), &id) == 0, "obtain token before reset");
    check(ioctl(b, CMD(16), &id) == 0, "reset acquired semaphore");
    check(ioctl(a, CMD(14), &id) == 0, "reset restores token");
    check(ioctl(a, CMD(15), &id) == 0, "return restored token");
    snprintf(name, sizeof(name), "named-rw-check-%ld", (long)getpid());
    id = ioctl(a, CMD(18), name);
    check(id >= 255, "RW index namespace");
    check(ioctl(b, CMD(18), name) == id, "same named RW lock across files");
    check(ioctl(a, CMD(20), &id) == 1, "first reader");
    check(ioctl(b, CMD(20), &id) == 1, "parallel reader");
    check(ioctl(a, CMD(23), &id) == 0, "writer excluded by readers");
    timed[0] = id;
    timed[1] = 20;
    check(ioctl(b, CMD(24), timed) == 0, "writer timeout while readers hold");
    check(ioctl(a, CMD(26), &id) == 0 && ioctl(b, CMD(26), &id) == 0, "release readers");
    check(ioctl(a, CMD(23), &id) == 1, "writer after readers finish");
    check(ioctl(b, CMD(20), &id) == 0, "reader excluded by writer");
    check(ioctl(a, CMD(27), &id) == 0, "release writer");
    check(ioctl(b, CMD(22), &id) == 1, "blocking reader after writer finishes");
    check(ioctl(b, CMD(26), &id) == 0, "release blocking reader");
    close(a);
    close(b);
    puts("W21A named semaphores and reader/writer exclusion PASS");
    return 0;
}
