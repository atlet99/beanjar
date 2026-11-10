// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* btmtk_usb.ko: MediaTek's Bluetooth USB driver as the userland sees it with no adapter plugged in.
 * The firmware's own btmtk_usb.ko (W18R's /lib/modules, W19P's bsppart /mnt/lg/res/lglib) is a
 * 32-bit ARM module this arm64 kernel cannot load. webos-bluetooth-service's BluetoothManagerBtpd
 * thread talks to it over a netlink family the driver creates (btmtk_nl_init:
 * netlink_kernel_create(&init_net, family, ...)), MediaTek's own number, which differs per build
 * (W18R 30, W19P 29) and so is the parameter family, the chip table's: it opens socket(AF_NETLINK,
 * SOCK_RAW, family), and while that fails it checks /sys/module/btmtk_usb/version and runs "insmod
 * .../btmtk_usb.ko" when that is missing, every 100 ms, 100 times. With the family it binds, sends
 * its registration and waits for the driver's USB state events, of which none come: the driver is
 * loaded, no adapter is. */
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <linux/netlink.h>

static int family;
module_param(family, int, 0444);
MODULE_PARM_DESC(
    family,
    "the netlink family the firmware's driver creates (after mainline's last, NETLINK_SMC 22)");

static struct sock *nl;

static void btmtk_nl_input(struct sk_buff *skb)
{
    /* the service's registration (its port id): nothing will ever be sent to it */
}

static int __init btmtk_init(void)
{
    struct netlink_kernel_cfg cfg = {.input = btmtk_nl_input};

    if (family <= NETLINK_SMC || family >= MAX_LINKS) {
        pr_err("btmtk_usb: family=%d is not a vendor netlink family\n", family);
        return -EINVAL;
    }
    nl = netlink_kernel_create(&init_net, family, &cfg);
    if (!nl)
        return -ENOMEM;
    pr_info("btmtk_usb: netlink family %d, no adapter\n", family);
    return 0;
}

static void __exit btmtk_exit(void)
{
    netlink_kernel_release(nl);
}

module_init(btmtk_init);
module_exit(btmtk_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("emulator");
MODULE_DESCRIPTION("MediaTek Bluetooth USB driver's netlink family, no adapter (beanjar)");
