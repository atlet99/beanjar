// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Give stock application jails access to the GL layer's render device.
 * MStar's jail descriptions expose Mali nodes, not DRM render nodes. Keep
 * those descriptions intact and add the VM GPU before the jailer chroots. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int gpu(const char *root)
{
    char path[PATH_MAX];
    if (directory(root, "/dev", path) || directory(root, "/dev/dri", path))
        return -1;
    glob_t nodes = {0};
    int ret = glob("/dev/dri/renderD*", 0, NULL, &nodes);
    if (ret) {
        globfree(&nodes);
        errno = ENODEV;
        return -1;
    }
    for (size_t i = 0; i < nodes.gl_pathc; i++) {
        struct stat src, dst;
        if (stat(nodes.gl_pathv[i], &src) || !S_ISCHR(src.st_mode)) {
            ret = -1;
            break;
        }
        int n = snprintf(path, sizeof path, "%s%s", root, nodes.gl_pathv[i]);
        if (n < 0 || (size_t)n >= sizeof path) {
            errno = ENAMETOOLONG;
            ret = -1;
            break;
        }
        if (lstat(path, &dst)) {
            if (errno != ENOENT || mknod(path, S_IFCHR | 0666, src.st_rdev)) {
                ret = -1;
                break;
            }
        } else if (!S_ISCHR(dst.st_mode) || dst.st_rdev != src.st_rdev) {
            errno = EEXIST;
            ret = -1;
            break;
        }
        if (chmod(path, 0666)) {
            ret = -1;
            break;
        }
    }
    globfree(&nodes);
    if (ret)
        return -1;
    /* libdrm needs this metadata to identify a render fd; otherwise Wayland
     * EGL attempts primary-node authentication (drmGetMagic), which fails. */
    if (directory(root, "/sys", path) || directory(root, "/sys/dev", path) ||
        directory(root, "/sys/dev/char", path))
        return -1;
    struct stat src, dst;
    if (stat("/sys/dev/char", &src) || stat(path, &dst))
        return -1;
    if (src.st_dev != dst.st_dev || src.st_ino != dst.st_ino) {
        if (mount("/sys/dev/char", path, NULL, MS_BIND, NULL))
            return -1;
        if (mount(NULL, path, NULL,
                  MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)) {
            int saved = errno;
            umount(path);
            errno = saved;
            return -1;
        }
    }
    return 0;
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
        if (name && !strcmp(name + 1, "jailer") &&
            !strncmp(path, "/var/palm/jail/", sizeof "/var/palm/jail/" - 1)) {
            if (gpu(path))
                fprintf(stderr, "beanjar jail GPU: %s\n", strerror(errno));
        }
    }
    return next(path);
}
