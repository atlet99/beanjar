// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Trust in the mode=mitm proxy's CA for jailed native apps, from LG's stock jailer (sam's
 * LD_PRELOAD: guest/layers/common/jail). The hook runs as jailer enters an app's jail, so it
 * also covers apps installed or updated while the VM runs. Vendor jail descriptions, engines and
 * verification stay unchanged.
 *
 * - The native browser's NSS engine opens /etc/pki/nssdb. Its stock jail gets read-only access to
 *   the firmware database, where init imported the CA.
 * - Cobalt apps (YouTube's youtube.leanback.v4 and others) verify TLS against their own roots,
 *   content/app/cobalt/content/ssl/certs in the app, an OpenSSL hashed directory, and nothing else:
 *   with the proxy's certificate they completed the handshake, then closed the connection and
 *   retried for ever on the splash screen. The CA goes there as an extra <hash>.<n>, the first
 *   free n, named in .beanjar-netstub-ca beside it, and is removed when the VM has no CA. A store
 *   app's directory is an overlay with its own upper directory, so the app's files stay as shipped.
 * - The kernel store device in jails whose stock device list omits /dev/lg.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../../../include/webos-sestore.h"

#define JAILS        "/var/palm/jail/"
#define CA_PEM       "/etc/emulator/netstub-ca.pem"
#define CA_HASH      "/etc/emulator/netstub-ca.hash"
#define COBALT_CERTS "content/app/cobalt/content/ssl/certs"
#define STAMP        ".beanjar-netstub-ca"
#define CA_MAX       65536

static const char *const app_dirs[] = {
    "/media/cryptofs/apps/usr/palm/applications",  /* store apps */
    "/media/developer/apps/usr/palm/applications", /* developer-mode apps */
    "/usr/palm/applications",                      /* firmware apps */
};

static int directory(const char *root, const char *suffix, char *path)
{
    int n = snprintf(path, PATH_MAX, "%s%s", root, suffix);
    if (n < 0 || n >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (mkdir(path, 0755) && errno != EEXIST)
        return -1;
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static int trust(const char *root)
{
    char path[PATH_MAX];
    struct stat st;
    if (stat("/etc/pki/nssdb/cert9.db", &st) || !S_ISREG(st.st_mode))
        return -1;
    snprintf(path, sizeof path, "%s/etc/pki/nssdb/cert9.db", root);
    if (!lstat(path, &st))
        return 0; /* Keep an existing stock jail database. */
    if (errno != ENOENT)
        return -1;
    if (directory(root, "/etc", path) || directory(root, "/etc/pki", path) ||
        directory(root, "/etc/pki/nssdb", path))
        return -1;
    if (mount("/etc/pki/nssdb", path, NULL, MS_BIND, NULL))
        return -1;
    if (mount(NULL, path, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC,
              NULL)) {
        int saved = errno;
        umount(path);
        errno = saved;
        return -1;
    }
    return 0;
}

/* A whole small file into buf (NUL-terminated); its length, or -1. */
static ssize_t slurp(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t len = 0;
    for (;;) {
        ssize_t r = read(fd, buf + len, size - 1 - len);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        len += (size_t)r;
        if (len == size - 1) {
            close(fd);
            errno = EFBIG;
            return -1;
        }
    }
    close(fd);
    buf[len] = 0;
    return (ssize_t)len;
}

static int spew(const char *path, const char *buf, size_t len, mode_t mode)
{
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof tmp, "%s.beanjar-tmp", path) >= (int)sizeof tmp) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode & 0600);
    if (fd < 0)
        return -1;
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, buf + done, len - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            goto fail;
        }
        done += (size_t)w;
    }
    if (fchmod(fd, mode) || close(fd)) {
        fd = -1;
        goto fail;
    }
    if (rename(tmp, path)) {
        fd = -1;
        goto fail;
    }
    return 0;
fail:
    if (fd >= 0)
        close(fd);
    unlink(tmp);
    return -1;
}

static int hex8(const char *s)
{
    for (int i = 0; i < 8; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}

/* name is <8 hex>.<1 to 3 digits>, the only names this hook writes or removes */
static int hashed_name(const char *name)
{
    if (!hex8(name) || name[8] != '.' || !name[9])
        return 0;
    for (size_t i = 9; name[i]; i++)
        if (name[i] < '0' || name[i] > '9' || i > 11)
            return 0;
    return 1;
}

/* dir/name into path (PATH_MAX), or -1 if it does not fit */
static int join(char *path, const char *dir, const char *name)
{
    int n = snprintf(path, PATH_MAX, "%s/%s", dir, name);
    return n < 0 || n >= PATH_MAX ? -1 : 0;
}

static int same_content(const char *path, const char *ca, size_t len)
{
    static char buf[CA_MAX];
    ssize_t n = slurp(path, buf, sizeof buf);
    return n == (ssize_t)len && !memcmp(buf, ca, len);
}

static void cobalt_certs(const char *certs, const char *ca, size_t ca_len, const char *hash)
{
    char stamp[PATH_MAX], old[PATH_MAX], name[32], path[PATH_MAX];
    if (join(stamp, certs, STAMP))
        return;
    old[0] = 0;
    if (slurp(stamp, name, sizeof name) > 0) {
        name[strcspn(name, "\n")] = 0;
        if (hashed_name(name) && join(old, certs, name))
            return;
    }
    if (ca && old[0] && !strncmp(name, hash, 8) && same_content(old, ca, ca_len))
        return; /* in place from an earlier launch */
    if (old[0])
        unlink(old);
    if (!ca) {
        unlink(stamp);
        return;
    }
    for (int n = 0; n < 100; n++) {
        snprintf(name, sizeof name, "%s.%d", hash, n);
        struct stat st;
        if (join(path, certs, name))
            return;
        if (!lstat(path, &st) && !(S_ISREG(st.st_mode) && same_content(path, ca, ca_len)))
            continue; /* the app's own certificate at this n */
        if (spew(path, ca, ca_len, 0644) || spew(stamp, name, strlen(name), 0644))
            fprintf(stderr, "beanjar jail Cobalt trust: %s: %s\n", path, strerror(errno));
        return;
    }
}

/* Firmware native jails usually mount /dev/lg. Older browser jails have a fixed node list;
 * expose this one read-only device there without copying any store contents or policy. */
static int sestore_device(const char *jail)
{
    char path[PATH_MAX];
    struct stat source, target;
    if (stat(WEBOS_SESTORE_DEVICE, &source))
        return errno == ENOENT ? 0 : -1;
    if (!S_ISCHR(source.st_mode)) {
        errno = ENODEV;
        return -1;
    }
    if (directory(jail, "/dev", path) || directory(jail, "/dev/lg", path))
        return -1;
    if (join(path, jail, "dev/lg/beanjar-sestore"))
        return -1;
    if (lstat(path, &target)) {
        if (errno != ENOENT || mknod(path, S_IFCHR | 0444, source.st_rdev))
            return -1;
    } else if (!S_ISCHR(target.st_mode) || target.st_rdev != source.st_rdev) {
        errno = EEXIST;
        return -1;
    }
    return chmod(path, 0444);
}

/* Remove only the files our previous Cobalt hook wrote, including policy. No key reads. */
static void remove_legacy_store(const char *jail)
{
    char dir[PATH_MAX], path[PATH_MAX];
    struct stat st;
    if (join(dir, jail, "etc/emulator") || lstat(dir, &st) || !S_ISDIR(st.st_mode))
        return;
    if (!join(path, dir, "youtube.key"))
        unlink(path);
    if (join(dir, jail, "etc/emulator/sestore") || lstat(dir, &st) || !S_ISDIR(st.st_mode))
        return;
    if (!join(path, dir, "C1FFB834"))
        unlink(path);
    if (!join(path, dir, ".missing"))
        unlink(path);
    rmdir(dir);
}

static void cobalt(const char *id, const char *jail)
{
    static char ca[CA_MAX];
    char hash[16], certs[PATH_MAX];
    ssize_t ca_len = slurp(CA_PEM, ca, sizeof ca);
    ssize_t hash_len = slurp(CA_HASH, hash, sizeof hash);
    int have_ca = ca_len > 0 && hash_len >= 8 && hex8(hash);
    hash[hash_len >= 8 ? 8 : 0] = 0;
    for (size_t i = 0; i < sizeof app_dirs / sizeof *app_dirs; i++) {
        struct stat st;
        if (snprintf(certs, sizeof certs, "%s/%s/" COBALT_CERTS, app_dirs[i], id) >=
            (int)sizeof certs)
            continue;
        if (stat(certs, &st) || !S_ISDIR(st.st_mode))
            continue;
        cobalt_certs(certs, have_ca ? ca : NULL, have_ca ? (size_t)ca_len : 0, hash);
    }
    remove_legacy_store(jail);
}

int chroot(const char *path)
{
    int (*next)(const char *) = dlsym(RTLD_NEXT, "chroot");
    if (!next) {
        errno = ENOSYS;
        return -1;
    }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        const char *name = strrchr(exe, '/');
        const char *id = !strncmp(path, JAILS, sizeof JAILS - 1) ? path + sizeof JAILS - 1 : NULL;
        if (name && !strcmp(name + 1, "jailer") && id && *id && !strchr(id, '/') &&
            strcmp(id, "..")) {
            if (!strcmp(id, "com.webos.app.browser") && !access(CA_PEM, F_OK) && trust(path))
                fprintf(stderr, "beanjar jail NSS trust: %s\n", strerror(errno));
            if (sestore_device(path))
                fprintf(stderr, "beanjar jail secure-store device: %s\n", strerror(errno));
            cobalt(id, path);
        }
    }
    return next(path);
}
