/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LGEMU_MICOM_RTC_H
#define LGEMU_MICOM_RTC_H
#include <linux/types.h>
int lgemu_micom_rtc_read(u8 *buf, unsigned int len);
#endif
