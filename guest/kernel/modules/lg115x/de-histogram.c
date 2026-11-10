// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Stopped-state DE histogram polling, not an active capture backend.
 * LG SIC's GPL-2.0 C6 de_proc.c/v4l2_vt.c: proc id 45, initial cmd/preset
 * 2/2, non-consuming getter and 5 ms stop pacing.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "lg115x.h"

static bool de_histogram_proc;
module_param(de_histogram_proc, bool, 0444);
MODULE_PARM_DESC(de_histogram_proc, "read-only stopped histogram state; no capture backend");
static struct proc_dir_entry *de_histogram_dir;

static int de_histogram_show(struct seq_file *seq, void *unused)
{
    /* Called in the sleeping proc read context, with no DE/VTM lock held.
     * State is immutable until a real histogram backend exists. */
    msleep(5);
    seq_printf(seq, "%x %x \n", 2, 2);
    return 0;
}

int lx_de_histogram_add(struct proc_dir_entry *parent)
{
    if (!de_histogram_proc)
        return 0;
    de_histogram_dir = proc_mkdir("de", parent);
    if (!de_histogram_dir)
        return -ENOMEM;
    /* proc_create_single supplies seq reads/seeks/releases and no write op. */
    if (!proc_create_single("v4l2_capture_read", 0444, de_histogram_dir, de_histogram_show)) {
        lx_de_histogram_remove();
        return -ENOMEM;
    }
    return 0;
}

void lx_de_histogram_remove(void)
{
    proc_remove(de_histogram_dir);
    de_histogram_dir = NULL;
}
