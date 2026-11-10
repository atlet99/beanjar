// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* The board's HDMI CEC adapter, /dev/cec0, on the kernel's CEC framework (kernel patch 0015 shows
 * CEC_CORE's prompt), with nothing else on the bus.
 *
 * LG's libcecp (eim's CEC, webOS 6 and later on every SoC) opens /dev/cec0 and drives it through
 * the framework's ioctls: CEC_S_MODE (initiator and follower), CEC_ADAP_S_PHYS_ADDR 0.0.0.0 (the
 * TV), CEC_ADAP_S_LOG_ADDRS, then CEC_TRANSMIT, and a thread that loops on CEC_RECEIVE with a
 * 10 s timeout. Without the node its open fails and that thread issues CEC_RECEIVE on the bad fd
 * in a loop with no pause, a whole CPU from boot. The framework is the ABI those calls
 * expect; the adapter below it is a bus with no other device:
 *   - a directed message (and a logical address poll) is not acknowledged, after its header
 *     block: CEC_TX_STATUS_NACK, so every address the TV polls for is free and it claims it;
 *   - a broadcast is not refused by anyone, which on CEC is its acknowledgement: CEC_TX_STATUS_OK
 *     after the whole frame;
 *   - nothing is ever received.
 * The times are the bus's own (CEC 1.4 bit timing: a 4.5 ms start bit, 2.4 ms data bits, ten to
 * a block), after the signal free time the framework asks for. A transmit completes from a work
 * item: the framework calls adap_transmit with its lock held and cec_transmit_attempt_done takes
 * that lock. A pending work holds the adapter's device (the adapter, and this board's state with
 * it, is freed with its last reference), and the module's own queue is drained at unload.
 *
 *   cec  register the adapter (O22's family; a SoC whose own module models its CEC node, rtk.ko's
 *        cec0, leaves it off) */
#define pr_fmt(fmt) KBUILD_MODNAME ": cec: " fmt
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include "lgemu.h"

#define START_US   4500
#define BIT_US     2400
#define BLOCK_BITS 10

static bool cec;
module_param(cec, bool, 0444);
MODULE_PARM_DESC(cec, "cec: register the board's HDMI CEC adapter (/dev/cec0), alone on its bus");

struct board_cec {
    struct cec_adapter *adap;
    struct delayed_work done;
    u8 status;
};

static struct workqueue_struct *wq;

static void transmit_done(struct work_struct *work)
{
    struct board_cec *b = container_of(to_delayed_work(work), struct board_cec, done);
    struct cec_adapter *adap = b->adap;

    cec_transmit_attempt_done(adap, b->status); /* none pending after a disable: ignored */
    put_device(&adap->devnode.dev);
}

/* called with the adapter's lock held: a work already running waits for it, and finishes after */
static int adap_enable(struct cec_adapter *adap, bool enable)
{
    struct board_cec *b = cec_get_drvdata(adap);

    if (!enable && cancel_delayed_work(&b->done))
        put_device(&adap->devnode.dev);
    return 0;
}

static int adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
    return 0;
}

static int adap_transmit(struct cec_adapter *adap, u8 attempts, u32 signal_free_time,
                         struct cec_msg *msg)
{
    struct board_cec *b = cec_get_drvdata(adap);
    bool broadcast = cec_msg_is_broadcast(msg);
    unsigned int blocks = broadcast ? msg->len : 1;
    unsigned long us = signal_free_time * BIT_US + START_US + blocks * BLOCK_BITS * BIT_US;

    b->status = broadcast ? CEC_TX_STATUS_OK : CEC_TX_STATUS_NACK;
    get_device(&adap->devnode.dev);
    if (!queue_delayed_work(wq, &b->done, usecs_to_jiffies(us)))
        put_device(&adap->devnode.dev); /* one transmit at a time: not reached */
    return 0;
}

static void adap_free(struct cec_adapter *adap)
{
    kfree(cec_get_drvdata(adap));
}

static const struct cec_adap_ops ops = {
    .adap_enable = adap_enable,
    .adap_log_addr = adap_log_addr,
    .adap_transmit = adap_transmit,
    .adap_free = adap_free,
};

static int board_cec_probe(struct platform_device *pdev)
{
    struct board_cec *b = kzalloc(sizeof(*b), GFP_KERNEL);
    int err;

    if (!b)
        return -ENOMEM;
    INIT_DELAYED_WORK(&b->done, transmit_done);
    b->adap = cec_allocate_adapter(&ops, b, "lgemu",
                                   CEC_CAP_PHYS_ADDR | CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT |
                                       CEC_CAP_PASSTHROUGH,
                                   CEC_MAX_LOG_ADDRS);
    if (IS_ERR(b->adap)) {
        err = PTR_ERR(b->adap);
        kfree(b);
        return err;
    }
    err = cec_register_adapter(b->adap, &pdev->dev);
    if (err) {
        cec_delete_adapter(b->adap); /* frees b (adap_free) */
        return err;
    }
    platform_set_drvdata(pdev, b);
    return 0;
}

static void board_cec_remove(struct platform_device *pdev)
{
    struct board_cec *b = platform_get_drvdata(pdev);

    cec_unregister_adapter(b->adap); /* disables it; freed with its last reference */
}

static struct platform_driver driver = {
    .probe = board_cec_probe,
    .remove = board_cec_remove,
    .driver = {.name = "lgemu-cec"},
};

static struct platform_device *device;

int lgemu_cec_init(void)
{
    int err;

    if (!cec)
        return 0;
    wq = alloc_workqueue("lgemu-cec", 0, 1);
    if (!wq)
        return -ENOMEM;
    err = platform_driver_register(&driver);
    if (err)
        goto no_driver;
    device = platform_device_register_simple("lgemu-cec", PLATFORM_DEVID_NONE, NULL, 0);
    if (IS_ERR(device)) {
        err = PTR_ERR(device);
        goto no_device;
    }
    if (!platform_get_drvdata(device)) { /* probed synchronously: it failed */
        platform_device_unregister(device);
        err = -ENODEV;
        goto no_device;
    }
    return 0;

no_device:
    device = NULL;
    platform_driver_unregister(&driver);
no_driver:
    destroy_workqueue(wq);
    return err;
}

void lgemu_cec_exit(void)
{
    if (!device)
        return;
    platform_device_unregister(device);
    device = NULL;
    platform_driver_unregister(&driver);
    destroy_workqueue(wq); /* a work that was running finishes first */
}
