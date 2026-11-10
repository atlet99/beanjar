// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* One immutable VM store, provisioned before firmware init. Readers need only the device;
 * no pathname, userspace daemon or jail-specific copy participates in item lookup. */
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include "../../../../include/webos-sestore.h"
#include "lgemu.h"

struct store_item {
    const char *id;
    const u8 *data;
    u32 bytes;
};
struct store_snapshot {
    u8 *bundle;
    u32 bytes, missing, count;
    struct store_item items[WEBOS_SESTORE_MAX_ITEMS];
};
static DEFINE_MUTEX(store_lock);
static struct store_snapshot *snapshot;

static bool store_name(const char *id)
{
    size_t n = strnlen(id, WEBOS_SESTORE_MAX_NAME + 1);
    if (!n || n > WEBOS_SESTORE_MAX_NAME)
        return false;
    for (size_t i = 0; i < n; ++i) {
        char c = id[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            continue;
        if (i && (c == '_' || c == '-'))
            continue;
        return false;
    }
    return true;
}

static void store_free(struct store_snapshot *store)
{
    if (store) {
        kvfree_sensitive(store->bundle, store->bytes);
        kfree_sensitive(store);
    }
}

static int store_parse(struct store_snapshot *store)
{
    size_t offset = sizeof(struct webos_sestore_header);
    if (store->bytes < offset)
        return -EINVAL;
    store->missing = get_unaligned_le32(store->bundle);
    store->count = get_unaligned_le32(store->bundle + 4);
    if (store->missing > WEBOS_SESTORE_MISSING_FALLBACK || store->count > WEBOS_SESTORE_MAX_ITEMS)
        return -EINVAL;
    for (u32 i = 0; i < store->count; ++i) {
        const struct webos_sestore_record *record;
        struct store_item *item = &store->items[i];
        if (store->bytes - offset < sizeof(*record))
            return -EINVAL;
        record = (const void *)(store->bundle + offset);
        if (!store_name(record->id) || memchr_inv(record->reserved, 0, sizeof record->reserved))
            return -EINVAL;
        /* Canonical zero padding avoids ambiguous item names in a provisioned bundle. */
        size_t n = strlen(record->id);
        if (memchr_inv(record->id + n, 0, sizeof record->id - n))
            return -EINVAL;
        item->id = record->id;
        item->bytes = get_unaligned_le32(&record->bytes);
        offset += sizeof(*record);
        if (item->bytes > WEBOS_SESTORE_MAX_VALUE || item->bytes > store->bytes - offset)
            return -EINVAL;
        item->data = store->bundle + offset;
        offset += item->bytes;
        for (u32 j = 0; j < i; ++j)
            if (!strcmp(item->id, store->items[j].id))
                return -EINVAL;
    }
    return offset == store->bytes ? 0 : -EINVAL;
}

static long store_load(unsigned long arg)
{
    struct webos_sestore_load request;
    struct store_snapshot *store;
    int ret;
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
    if (copy_from_user(&request, (void __user *)arg, sizeof request))
        return -EFAULT;
    if (request.reserved || request.bytes < sizeof(struct webos_sestore_header) ||
        request.bytes > WEBOS_SESTORE_MAX_BUNDLE)
        return -EINVAL;
    store = kzalloc(sizeof(*store), GFP_KERNEL);
    if (!store)
        return -ENOMEM;
    store->bytes = request.bytes;
    store->bundle = kvmalloc(store->bytes, GFP_KERNEL);
    if (!store->bundle) {
        store_free(store);
        return -ENOMEM;
    }
    if (copy_from_user(store->bundle, u64_to_user_ptr(request.data), store->bytes)) {
        store_free(store);
        return -EFAULT;
    }
    ret = store_parse(store);
    if (!ret) {
        mutex_lock(&store_lock);
        if (snapshot)
            ret = -EBUSY;
        else {
            snapshot = store;
            store = NULL;
        }
        mutex_unlock(&store_lock);
    }
    store_free(store);
    return ret;
}

static long store_get(unsigned long arg)
{
    struct webos_sestore_get request;
    const struct store_item *item = NULL;
    int ret = 0;
    if (copy_from_user(&request, (void __user *)arg, sizeof request))
        return -EFAULT;
    if (!store_name(request.id) || memchr_inv(request.reserved, 0, sizeof request.reserved))
        return -EINVAL;
    mutex_lock(&store_lock);
    if (!snapshot) {
        ret = -ENODATA;
        goto out;
    }
    for (u32 i = 0; i < snapshot->count; ++i)
        if (!strcmp(request.id, snapshot->items[i].id)) {
            item = &snapshot->items[i];
            break;
        }
    if (!item) {
        ret = -ENOENT;
        goto out;
    }
    request.bytes = item->bytes;
    if (request.data && request.capacity < item->bytes)
        ret = -EMSGSIZE;
    if (copy_to_user((void __user *)arg, &request, sizeof request)) {
        ret = -EFAULT;
        goto out;
    }
    if (!ret && request.data && item->bytes &&
        copy_to_user(u64_to_user_ptr(request.data), item->data, item->bytes))
        ret = -EFAULT;
out:
    mutex_unlock(&store_lock);
    return ret;
}

static long store_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct webos_sestore_info info;
    int ret;
    switch (cmd) {
    case WEBOS_SESTORE_LOAD:
        return store_load(arg);
    case WEBOS_SESTORE_GET:
        return store_get(arg);
    case WEBOS_SESTORE_INFO:
        mutex_lock(&store_lock);
        if (!snapshot)
            ret = -ENODATA;
        else {
            info.missing = snapshot->missing;
            info.count = snapshot->count;
            ret = copy_to_user((void __user *)arg, &info, sizeof info) ? -EFAULT : 0;
        }
        mutex_unlock(&store_lock);
        return ret;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations store_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = store_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
    .llseek = noop_llseek,
};
static struct miscdevice store_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "beanjar-sestore",
    .nodename = "lg/beanjar-sestore",
    .fops = &store_fops,
    .mode = 0444,
};
int lgemu_sestore_init(void)
{
    return misc_register(&store_device);
}
void lgemu_sestore_exit(void)
{
    misc_deregister(&store_device);
    store_free(snapshot);
    snapshot = NULL;
}
