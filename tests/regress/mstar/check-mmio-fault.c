// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t owned_child;
static void check(int ok, const char *why)
{
    if (!ok) {
        if (owned_child > 0) {
            kill(owned_child, SIGKILL);
            waitpid(owned_child, NULL, 0);
        }
        fprintf(stderr, "FAIL %s\n", why);
        exit(1);
    }
}
static unsigned load(const void *p)
{
    unsigned v;
    __asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static void store(void *p, unsigned v)
{
    __asm__ volatile("str %0, [%1]" ::"r"(v), "r"(p) : "memory");
}
__attribute__((naked, used)) static void step_target(void)
{
    __asm__ volatile("str r1, [r0]\n\tmov r0, #0\n\tmov r7, #1\n\tsvc #0");
}
__attribute__((target("thumb"), naked, noinline)) static void
thumb_store(void *p __attribute__((unused)), unsigned v __attribute__((unused)))
{
    __asm__ volatile("str r1, [r0]\n\tbx lr");
}
/* Each rejected operation executes in a child so SIGBUS does not conceal
 * transaction side effects in the parent's alias. */
static void rejected(void *p, int kind)
{
    pid_t pid = fork();
    int status;
    check(pid >= 0, "fork");
    if (pid)
        owned_child = pid;
    if (!pid) {
        register void *base __asm__("r0") = p;
        register unsigned value __asm__("r1") = 0x11223344;
        switch (kind) {
        case 0:
            __asm__ volatile("str %1, [%0, #4]!" : "+r"(base) : "r"(value) : "memory");
            break;
        case 1:
            __asm__ volatile("cmp %1, %1\n\tstreq %1, [%0]" ::"r"(base), "r"(value)
                             : "cc", "memory");
            break;
        case 2:
            __asm__ volatile("ldrsb %1, [%0]" : "+r"(base), "+r"(value)::"memory");
            break;
        case 3:
            __asm__ volatile("ldrex %1, [%0]" : "+r"(base), "+r"(value)::"memory");
            break;
        case 4:
            store((char *)p + 8, value);
            break;
        case 5:
            __asm__ volatile("setend be\n\tstr %1, [%0]" ::"r"(base), "r"(value) : "memory");
            break;
        case 6:
            __asm__ volatile("ldr pc, [%0]" ::"r"(base) : "memory");
            break;
        case 7:
            thumb_store(p, value);
            break;
        }
        _exit(99);
    }
    check(waitpid(pid, &status, 0) == pid, "wait");
    owned_child = 0;
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS, "unsupported SIGBUS");
}
int main(void)
{
    int fd = open("/dev/mmio-fixture", O_RDWR), status;
    unsigned char *a, *b;
    unsigned v, index = 1;
    pid_t pid;
    check(fd >= 0, "open");
    a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    b = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(a != MAP_FAILED && b != MAP_FAILED, "map aliases");
    check(load(a) == ~0U, "initial W1C");
    store(a, 0xff00);
    check(load(b) == 0xffff00ff, "synchronous W1C alias");
    store(a + 4, 0x12345678);
    check(load(b + 4) == 0x12345678, "word alias");
    __asm__ volatile("strb %1, [%0, #5]" ::"r"(a), "r"(0xaa) : "memory");
    __asm__ volatile("ldrb %0, [%1, #5]" : "=r"(v) : "r"(b) : "memory");
    check(v == 0xaa, "byte immediate");
    __asm__ volatile("strh %1, [%0, #6]" ::"r"(a), "r"(0xbeef) : "memory");
    __asm__ volatile("ldrh %0, [%1, #6]" : "=r"(v) : "r"(b) : "memory");
    check(v == 0xbeef, "halfword immediate");
    __asm__ volatile("ldr %0, [%1, %2, lsl #2]" : "=r"(v) : "r"(b), "r"(index) : "memory");
    check(v == 0xbeefaa78, "shifted offset");
    __asm__ volatile("ldrh %0, [%1, #-2]" : "=r"(v) : "r"(b + 8) : "memory");
    check(v == 0xbeef, "negative offset");
    index = 2;
    __asm__ volatile("ldrh %0, [%1, %2]" : "=r"(v) : "r"(b + 4), "r"(index) : "memory");
    check(v == 0xbeef, "halfword register offset");
    v = (uintptr_t)(b + 4);
    __asm__ volatile("ldr %0, [%0]" : "+r"(v)::"memory");
    check(v == 0xbeefaa78, "load destination equals base");
    for (int i = 0; i < 8; i++) {
        rejected(a + 4, i);
        check(load(b + 4) == 0xbeefaa78, "rejection no side effects");
    }
    /* A debugger step across the faulting instruction must complete once and
     * deliver SIGTRAP at the following instruction, using the real arch helper. */
    pid = fork();
    check(pid >= 0, "step fork");
    if (pid)
        owned_child = pid;
    if (!pid) {
        check(ptrace(PTRACE_TRACEME, 0, 0, 0) == 0, "traceme");
        raise(SIGSTOP);
        _exit(0);
    }
    check(waitpid(pid, &status, 0) == pid && WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP,
          "step setup stop");
    unsigned regs[18];
    check(ptrace(PTRACE_GETREGS, pid, 0, regs) == 0, "step getregs");
    regs[0] = (uintptr_t)a;
    regs[1] = 1;
    regs[15] = (uintptr_t)step_target;
    check(ptrace(PTRACE_SETREGS, pid, 0, regs) == 0, "step setregs");
    check(ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == 0, "single step");
    check(waitpid(pid, &status, 0) == pid && WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP,
          "step trap");
    unsigned after[18];
    check(ptrace(PTRACE_GETREGS, pid, 0, after) == 0 && after[15] == regs[15] + 4,
          "step PC completion");
    check(load(b) == 0xffff00fe, "step transaction final state");
    check(ptrace(PTRACE_CONT, pid, 0, 0) == 0, "step continue");
    check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status),
          "step child exit");
    owned_child = 0;
    munmap(a, 4096);
    munmap(b, 4096);
    close(fd);
    puts("MMIO fault decode/W1C/aliases/rejection/single-step PASS");
    return 0;
}
