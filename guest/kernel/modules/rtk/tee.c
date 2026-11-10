// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* rtk.ko's TEE: /dev/tee0 with the OP-TEE driver's user ABI (linux/tee.h, TEE_IOC_*), which the
 * Realtek firmwares' TEE client speaks (libkadaptor's statically linked libteec, OP-TEE's), and
 * behind it stand-in trusted applications for the sessions the firmware opens (tee-ta.c).
 *
 * The device: TEE_IOC_VERSION (OP-TEE, GlobalPlatform; no registered memory, so the client takes
 * its shared memory from SHM_ALLOC and copies registered buffers through it), SHM_ALLOC (an fd
 * whose mapping is the memory; the id lives as long as the fd or a mapping of it), OPEN_SESSION,
 * INVOKE, CANCEL (nothing is ever pending) and CLOSE_SESSION. A TA answers at once; a UUID without
 * one is TEEC_ERROR_ITEM_NOT_FOUND from the TEE, as OP-TEE answers a TA it cannot load.
 *
 * No key material of any set is involved: the TAs' secrets are made here at random (tee-ta.c). */
#define pr_fmt(fmt) KBUILD_MODNAME ": tee: " fmt
#include <linux/anon_inodes.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/tee.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "lgemu-drv.h"
#include "rtk-tee.h"

#define SHM_MAX    (16u << 20)
#define PARAMS_MAX 4

/* every context's shared memory and sessions, and the TAs' state, under one lock: a TA answers
 * at once, so nothing waits long */
static DEFINE_MUTEX(tee_lock);

struct tee_ctx {
    struct kref ref;
    struct idr shms;
    struct idr sessions;
};

struct tee_shm {
    struct tee_ctx *ctx;
    void *va;
    size_t size;  /* the client's */
    size_t alloc; /* page-rounded */
    int id;
};

struct tee_sess {
    const struct rtk_ta *ta;
    void *state;
};

static void ctx_free(struct kref *ref)
{
    struct tee_ctx *ctx = container_of(ref, struct tee_ctx, ref);

    idr_destroy(&ctx->shms);
    idr_destroy(&ctx->sessions);
    kfree(ctx);
}

/* ---- shared memory ---- */

static int shm_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct tee_shm *shm = file->private_data;
    unsigned long len = vma->vm_end - vma->vm_start;

    if (vma->vm_pgoff || len > shm->alloc)
        return -EINVAL;
    return remap_vmalloc_range(vma, shm->va, 0);
}

static int shm_release(struct inode *inode, struct file *file)
{
    struct tee_shm *shm = file->private_data;

    mutex_lock(&tee_lock);
    idr_remove(&shm->ctx->shms, shm->id);
    mutex_unlock(&tee_lock);
    kref_put(&shm->ctx->ref, ctx_free);
    vfree(shm->va);
    kfree(shm);
    return 0;
}

static const struct file_operations shm_fops = {
    .owner = THIS_MODULE,
    .mmap = shm_mmap,
    .release = shm_release,
};

static long shm_alloc(struct tee_ctx *ctx, void __user *uarg)
{
    struct tee_ioctl_shm_alloc_data data;
    struct tee_shm *shm;
    int fd;

    if (copy_from_user(&data, uarg, sizeof(data)))
        return -EFAULT;
    if (data.flags || data.size > SHM_MAX)
        return -EINVAL;
    shm = kzalloc(sizeof(*shm), GFP_KERNEL);
    if (!shm)
        return -ENOMEM;
    shm->size = data.size;
    shm->alloc = PAGE_ALIGN(max_t(size_t, data.size, 1));
    shm->va = vmalloc_user(shm->alloc);
    if (!shm->va) {
        kfree(shm);
        return -ENOMEM;
    }
    shm->ctx = ctx;
    mutex_lock(&tee_lock);
    shm->id = idr_alloc(&ctx->shms, shm, 1, 0, GFP_KERNEL);
    mutex_unlock(&tee_lock);
    if (shm->id < 0) {
        fd = shm->id;
        goto fail;
    }
    kref_get(&ctx->ref);
    fd = anon_inode_getfd("tee_shm", &shm_fops, shm, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        mutex_lock(&tee_lock);
        idr_remove(&ctx->shms, shm->id);
        mutex_unlock(&tee_lock);
        kref_put(&ctx->ref, ctx_free);
        goto fail;
    }
    data.id = shm->id;
    if (copy_to_user(uarg, &data, sizeof(data))) {
        /* the fd is the caller's now; it goes with the process */
        return -EFAULT;
    }
    return fd;

fail:
    vfree(shm->va);
    kfree(shm);
    return fd;
}

/* ---- parameters: the client's (struct tee_ioctl_param) to a TA's (struct rtk_ta_param) ---- */

static u32 params_in(struct tee_ctx *ctx, const struct tee_ioctl_param *up, u32 n,
                     struct rtk_ta_param *p)
{
    memset(p, 0, sizeof(*p) * PARAMS_MAX);
    for (u32 i = 0; i < n; i++) {
        u32 type = up[i].attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK;
        struct tee_shm *shm;

        if (up[i].attr & ~(u64)TEE_IOCTL_PARAM_ATTR_TYPE_MASK)
            return TEEC_ERROR_BAD_PARAMETERS;
        p[i].type = type;
        switch (type) {
        case TEE_IOCTL_PARAM_ATTR_TYPE_NONE:
            break;
        case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
            p[i].a = up[i].a;
            p[i].b = up[i].b;
            break;
        case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
            /* a: offset in the shared memory, b: size, c: its id */
            p[i].size = up[i].b;
            if ((s64)up[i].c < 0 && !up[i].b)
                break; /* a null memref of no size */
            shm = idr_find(&ctx->shms, (int)up[i].c);
            if (!shm || up[i].a > shm->size || up[i].b > shm->size - up[i].a)
                return TEEC_ERROR_BAD_PARAMETERS;
            p[i].buf = (u8 *)shm->va + up[i].a;
            break;
        default:
            return TEEC_ERROR_BAD_PARAMETERS;
        }
    }
    return TEEC_SUCCESS;
}

static void params_out(struct tee_ioctl_param *up, u32 n, const struct rtk_ta_param *p)
{
    for (u32 i = 0; i < n; i++) {
        switch (p[i].type) {
        case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
            up[i].a = p[i].a;
            up[i].b = p[i].b;
            break;
        case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
        case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
            up[i].b = p[i].size;
            break;
        }
    }
}

/* the argument of OPEN_SESSION and INVOKE: a struct with num_params parameters after it */
static void *arg_in(void __user *uarg, size_t head, u32 *nparams, size_t *len,
                    struct tee_ioctl_param **params, u64 __user **ubuf)
{
    struct tee_ioctl_buf_data buf;
    void *arg;
    u32 n;

    if (copy_from_user(&buf, uarg, sizeof(buf)))
        return ERR_PTR(-EFAULT);
    if (buf.buf_len > TEE_MAX_ARG_SIZE || buf.buf_len < head)
        return ERR_PTR(-EINVAL);
    arg = memdup_user(u64_to_user_ptr(buf.buf_ptr), buf.buf_len);
    if (IS_ERR(arg))
        return arg;
    /* num_params is the last u32 of both heads */
    n = *(u32 *)((u8 *)arg + head - sizeof(u32));
    if (n > PARAMS_MAX || buf.buf_len != head + n * sizeof(struct tee_ioctl_param)) {
        kfree(arg);
        return ERR_PTR(-EINVAL);
    }
    *nparams = n;
    *len = buf.buf_len;
    *params = (struct tee_ioctl_param *)((u8 *)arg + head);
    *ubuf = u64_to_user_ptr(buf.buf_ptr);
    return arg;
}

static long open_session(struct tee_ctx *ctx, void __user *uarg)
{
    struct rtk_ta_param p[PARAMS_MAX];
    struct tee_ioctl_open_session_arg *arg;
    struct tee_ioctl_param *up;
    struct tee_sess *sess = NULL;
    const struct rtk_ta *ta;
    u64 __user *ubuf;
    size_t len;
    long err = 0;
    u32 n;

    arg = arg_in(uarg, sizeof(*arg), &n, &len, &up, &ubuf);
    if (IS_ERR(arg))
        return PTR_ERR(arg);
    arg->ret_origin = TEEC_ORIGIN_TEE;
    arg->session = 0;
    mutex_lock(&tee_lock);
    ta = rtk_ta_find(arg->uuid);
    if (!ta) {
        arg->ret = TEEC_ERROR_ITEM_NOT_FOUND;
        rtk_ta_unknown(arg->uuid);
        goto out;
    }
    arg->ret = params_in(ctx, up, n, p);
    if (arg->ret)
        goto out;
    sess = kzalloc(sizeof(*sess), GFP_KERNEL);
    if (!sess) {
        err = -ENOMEM;
        goto out;
    }
    sess->ta = ta;
    arg->ret_origin = TEEC_ORIGIN_TRUSTED_APP;
    arg->ret = ta->open ? ta->open(&sess->state, p) : TEEC_SUCCESS;
    if (arg->ret)
        goto out;
    err = idr_alloc(&ctx->sessions, sess, 1, 0, GFP_KERNEL);
    if (err < 0) {
        if (ta->close)
            ta->close(sess->state);
        goto out;
    }
    arg->session = err;
    err = 0;
    sess = NULL;
    params_out(up, n, p);
out:
    mutex_unlock(&tee_lock);
    kfree(sess);
    if (!err && copy_to_user(ubuf, arg, len))
        err = -EFAULT;
    kfree(arg);
    return err;
}

static long invoke(struct tee_ctx *ctx, void __user *uarg)
{
    struct rtk_ta_param p[PARAMS_MAX];
    struct tee_ioctl_invoke_arg *arg;
    struct tee_ioctl_param *up;
    struct tee_sess *sess;
    u64 __user *ubuf;
    size_t len;
    long err = 0;
    u32 n;

    arg = arg_in(uarg, sizeof(*arg), &n, &len, &up, &ubuf);
    if (IS_ERR(arg))
        return PTR_ERR(arg);
    mutex_lock(&tee_lock);
    sess = idr_find(&ctx->sessions, arg->session);
    if (!sess) {
        err = -EINVAL;
        goto out;
    }
    arg->ret_origin = TEEC_ORIGIN_TEE;
    arg->ret = params_in(ctx, up, n, p);
    if (arg->ret)
        goto out;
    arg->ret_origin = TEEC_ORIGIN_TRUSTED_APP;
    arg->ret = sess->ta->invoke(sess->state, arg->func, p);
    params_out(up, n, p);
out:
    mutex_unlock(&tee_lock);
    if (!err && copy_to_user(ubuf, arg, len))
        err = -EFAULT;
    kfree(arg);
    return err;
}

static void session_end(struct tee_sess *sess)
{
    if (sess->ta->close)
        sess->ta->close(sess->state);
    kfree(sess);
}

static long close_session(struct tee_ctx *ctx, void __user *uarg)
{
    struct tee_ioctl_close_session_arg arg;
    struct tee_sess *sess;

    if (copy_from_user(&arg, uarg, sizeof(arg)))
        return -EFAULT;
    mutex_lock(&tee_lock);
    sess = idr_remove(&ctx->sessions, arg.session);
    if (sess)
        session_end(sess);
    mutex_unlock(&tee_lock);
    return sess ? 0 : -EINVAL;
}

static long tee_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct tee_ctx *ctx = file->private_data;
    void __user *uarg = (void __user *)arg;

    switch (cmd) {
    case TEE_IOC_VERSION: {
        struct tee_ioctl_version_data v = {
            .impl_id = TEE_IMPL_ID_OPTEE,
            .impl_caps = TEE_OPTEE_CAP_TZ,
            .gen_caps = TEE_GEN_CAP_GP,
        };
        return copy_to_user(uarg, &v, sizeof(v)) ? -EFAULT : 0;
    }
    case TEE_IOC_SHM_ALLOC:
        return shm_alloc(ctx, uarg);
    case TEE_IOC_OPEN_SESSION:
        return open_session(ctx, uarg);
    case TEE_IOC_INVOKE:
        return invoke(ctx, uarg);
    case TEE_IOC_CANCEL:
        return 0;
    case TEE_IOC_CLOSE_SESSION:
        return close_session(ctx, uarg);
    }
    return -EINVAL;
}

static int tee_open(struct inode *inode, struct file *file)
{
    struct tee_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

    if (!ctx)
        return -ENOMEM;
    kref_init(&ctx->ref);
    idr_init(&ctx->shms);
    idr_init(&ctx->sessions);
    file->private_data = ctx;
    return 0;
}

static int tee_release(struct inode *inode, struct file *file)
{
    struct tee_ctx *ctx = file->private_data;
    struct tee_sess *sess;
    int id;

    mutex_lock(&tee_lock);
    idr_for_each_entry(&ctx->sessions, sess, id) session_end(sess);
    mutex_unlock(&tee_lock);
    kref_put(&ctx->ref, ctx_free); /* the shared memory still mapped keeps it */
    return 0;
}

static const struct file_operations tee_fops = {
    .owner = THIS_MODULE,
    .open = tee_open,
    .release = tee_release,
    .unlocked_ioctl = tee_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

/* ---- the device ---- */

static char *tee_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static const struct class tee_class = {.name = "tee", .devnode = tee_devnode};
static struct lgemu_cdev tee_cdev;
static bool tee_class_registered, tee_made;

int rtk_tee_add(struct proc_dir_entry *dir)
{
    struct device *d;
    int err;

    err = rtk_ta_setup(dir);
    if (err)
        return err;
    err = lgemu_cdev_add(&tee_cdev, 0, 0, 1, "tee", &tee_fops);
    if (err)
        return err;
    err = class_register(&tee_class);
    if (err)
        return err;
    tee_class_registered = true;
    d = device_create(&tee_class, NULL, tee_cdev.dev, NULL, "tee0");
    if (IS_ERR(d))
        return PTR_ERR(d);
    tee_made = true;
    return 0;
}

void rtk_tee_remove(void)
{
    if (tee_made)
        device_destroy(&tee_class, tee_cdev.dev);
    tee_made = false;
    if (tee_class_registered)
        class_unregister(&tee_class);
    tee_class_registered = false;
    lgemu_cdev_del(&tee_cdev);
}
