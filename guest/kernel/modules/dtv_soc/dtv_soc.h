/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
/* Inside dtv_soc.ko: the demuxes and the decoders' input (main.c), the video decoder's node
 * (vdec.c). */
#ifndef DTV_SOC_H
#define DTV_SOC_H
#include <linux/types.h>

/* A record for /dev/dtv-soc-av (include/webos-tuner.h): the header's fields and len bytes of data.
 * Any context; dropped whole when the reader is behind. */
void dtv_soc_av(u16 type, u8 kind, u8 decoder, u32 channel, u32 codec, const void *data, u32 len);

/* The video decoder (vdec.c). vdec_packet: a TS packet of the video decoder filter; returns the
 * VDEC channel streaming it (it is passed on as that decoder's input), or -1 when none streams.
 * Called with the demux's lock held. */
int vdec_init(void);
void vdec_exit(void);
int vdec_packet(const u8 *pkt);
void vdec_show(struct seq_file *m);
#endif
