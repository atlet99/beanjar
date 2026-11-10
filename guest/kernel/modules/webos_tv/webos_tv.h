/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
/* webos_tv.ko's frontends and the module that brings them a signal (dtv_soc.ko, the stock
 * layout's demodulator-and-TS piece): webos_tv registers the DVB adapter and the frontends, and
 * asks its source whether a tuned frequency carries a signal; the source puts its demuxes on the
 * adapter and feeds them the transport stream of what frontend 0 is tuned to. Without a source
 * nothing locks. */
#ifndef WEBOS_TV_H
#define WEBOS_TV_H
#include <media/dvbdev.h>

struct webos_tv_source {
    /* whether frequency freq_hz (enum fe_delivery_system delsys) carries a signal */
    bool (*signal)(u32 freq_hz, u32 delsys);
    /* frontend fe has been tuned (DTV_TUNE) to freq_hz; also called for each frontend at attach */
    void (*tuned)(unsigned int fe, u32 freq_hz, u32 delsys);
};

/* The adapter, with src as the frontends' signal; NULL if a source is attached already. */
struct dvb_adapter *webos_tv_attach(const struct webos_tv_source *src);
void webos_tv_detach(void);
#endif
