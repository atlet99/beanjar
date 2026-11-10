// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* CP_READ_RTC_YMDHMS: the stock micom_timer reads seven binary calendar bytes.
 * rtc0 is the virt machine's real PL031; system time can be overwritten by
 * userspace's factory fallback and must not be used as the micom clock. */
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include "micom-rtc.h"

int lgemu_micom_rtc_read(u8 *buf, unsigned int len)
{
    struct rtc_device *rtc;
    struct rtc_time tm;
    int ret, year;

    if (len != 7)
        return -EREMOTEIO;
    /* UCOM's ioctl holds a mutex, not a spinlock. The shared I2C entry also
     * has kernel callers: never sleep if one arrives in atomic context. */
    if (in_atomic() || irqs_disabled())
        return -EWOULDBLOCK;
    rtc = rtc_class_open("rtc0");
    if (!rtc)
        return -ENODEV;
    ret = rtc_read_time(rtc, &tm); /* Includes calendar validation in Linux 6.18.53. */
    rtc_class_close(rtc);
    if (ret)
        return ret;
    /* The century is one binary byte, not BCD. Reject an unrepresentable year
     * before writing any response bytes. */
    if (tm.tm_year < 0 || tm.tm_year > 25599 - 1900)
        return -ERANGE;
    year = tm.tm_year + 1900;
    buf[0] = year / 100;
    buf[1] = year % 100;
    buf[2] = tm.tm_mon + 1;
    buf[3] = tm.tm_mday;
    buf[4] = tm.tm_hour;
    buf[5] = tm.tm_min;
    buf[6] = tm.tm_sec;
    return 0;
}
