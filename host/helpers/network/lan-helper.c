// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-lan-helper: put a VM on the host's wired LAN (docs/dev/architecture/network.md, link =
 * "lan").
 *
 *   beanjar-lan-helper macvlan PARENT MAC PID
 *
 * creates a macvlan interface "lan0" (mode bridge) on PARENT with the unicast MAC, directly in the
 * network namespace of PID, a process of the calling user (the VM's rootless namespace, netns.py).
 * That is all it does. It is installed with only CAP_NET_ADMIN as a file capability (the package's
 * postinst: setcap cap_net_admin=ep); everything else of Beanjar stays rootless.
 *
 * What it refuses: a PARENT that is not a physical Ethernet interface of this host (no device
 * behind it, a bridge, bond, VLAN or other virtual one, a wireless one: macvlan does not work on
 * Wi-Fi), a MAC that is not six hex octets or is multicast, a PID that is not the caller's or is in
 * this helper's own network namespace. The interface is made inside the target namespace in one
 * step (ip link add ... netns PID), so it never exists on the host side, and it is destroyed with
 * that namespace when the VM stops, whatever way it stops.
 *
 * The work is done by iproute2's ip (an absolute path, an empty environment), to which the
 * capability is passed as an ambient one; nothing of the caller's environment reaches it.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define IFNAME "lan0"

static int fail(const char *fmt, const char *arg)
{
    fprintf(stderr, "beanjar-lan-helper: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    return 1;
}

static int read_small(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = 0;
    while (n > 0 && isspace((unsigned char)buf[n - 1]))
        buf[--n] = 0;
    return 0;
}

static int exists(const char *dir, const char *name, const char *leaf)
{
    char path[256];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s/%s", dir, name, leaf);
    return lstat(path, &st) == 0;
}

/* A physical, wired Ethernet interface: ARPHRD_ETHER, a device behind it, none of the virtual or
 * wireless kinds. */
static int check_parent(const char *name)
{
    const char *net = "/sys/class/net";
    char path[256], type[32];
    size_t len = strlen(name);
    if (len == 0 || len > 15 || !strcmp(name, ".") || !strcmp(name, ".."))
        return fail("%s: not an interface name", name);
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)name[i]) && !strchr("_.:-", name[i]))
            return fail("%s: not an interface name", name);
    snprintf(path, sizeof path, "%s/%s/type", net, name);
    if (read_small(path, type, sizeof type) < 0)
        return fail("%s: no such network interface", name);
    if (strcmp(type, "1"))
        return fail("%s: not an Ethernet interface", name);
    if (exists(net, name, "wireless") || exists(net, name, "phy80211"))
        return fail("%s: wireless; link=lan needs a wired interface", name);
    if (!exists(net, name, "device"))
        return fail("%s: a virtual interface; link=lan needs a physical one", name);
    if (exists(net, name, "bridge") || exists(net, name, "bonding"))
        return fail("%s: a bridge or bond; link=lan needs a physical interface", name);
    return 0;
}

static int check_mac(const char *mac)
{
    if (strlen(mac) != 17)
        return fail("%s: six hex octets with colons", mac);
    for (int i = 0; i < 17; i++)
        if ((i % 3 == 2) ? mac[i] != ':' : !isxdigit((unsigned char)mac[i]))
            return fail("%s: six hex octets with colons", mac);
    char first[3] = {mac[0], mac[1], 0};
    if (strtol(first, NULL, 16) & 1)
        return fail("%s: a multicast address", mac);
    return 0;
}

static int check_pid(const char *pid)
{
    char path[64], self[64], target[64];
    struct stat st;
    size_t len = strlen(pid);
    if (len == 0 || len > 10)
        return fail("%s: not a pid", pid);
    for (size_t i = 0; i < len; i++)
        if (!isdigit((unsigned char)pid[i]))
            return fail("%s: not a pid", pid);
    snprintf(path, sizeof path, "/proc/%s", pid);
    if (stat(path, &st) < 0)
        return fail("%s: no such process", pid);
    if (st.st_uid != getuid())
        return fail("%s: not a process of the caller", pid);
    snprintf(path, sizeof path, "/proc/%s/ns/net", pid);
    ssize_t n = readlink(path, target, sizeof target - 1);
    ssize_t m = readlink("/proc/self/ns/net", self, sizeof self - 1);
    if (n < 0 || m < 0)
        return fail("%s: its network namespace is unreadable", pid);
    target[n] = self[m] = 0;
    if (!strcmp(target, self))
        return fail("%s: in the host's network namespace, not a VM's", pid);
    return 0;
}

/* CAP_NET_ADMIN from the permitted set into the inheritable and ambient ones, for ip. */
static int pass_capability(void)
{
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2];
    if (syscall(SYS_capget, &header, data) < 0)
        return -1;
    if (!(data[0].permitted & (1u << CAP_NET_ADMIN))) {
        errno = EPERM;
        return -1;
    }
    data[0].inheritable |= 1u << CAP_NET_ADMIN;
    if (syscall(SYS_capset, &header, data) < 0)
        return -1;
    return prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0);
}

int main(int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[1], "macvlan")) {
        fprintf(stderr, "usage: beanjar-lan-helper macvlan PARENT MAC PID\n");
        return 2;
    }
    const char *parent = argv[2], *mac = argv[3], *pid = argv[4];
    if (check_parent(parent) || check_mac(mac) || check_pid(pid))
        return 1;
    const char *ip = access("/usr/sbin/ip", X_OK) == 0 ? "/usr/sbin/ip" : "/sbin/ip";
    if (pass_capability() < 0)
        return fail(
            "%s: CAP_NET_ADMIN unavailable (the package installs it with setcap cap_net_admin=ep)",
            strerror(errno));
    char *const args[] = {"ip",      "link",    "add",       "link",  (char *)parent, "name",
                          IFNAME,    "address", (char *)mac, "netns", (char *)pid,    "type",
                          "macvlan", "mode",    "bridge",    NULL};
    char *const env[] = {NULL};
    execve(ip, args, env);
    return fail("%s: cannot run ip", strerror(errno));
}
