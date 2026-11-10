// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_AUDIO_H
#define VNC_AUDIO_H
/* beanjar-vnc's --audio-local: the core's mixed audio (wv_audio_sink) on an owner-only (0600)
 * unix socket, for this host's own front ends (beanjar vnc audio-pipe, the web viewer). A client
 * sends one request line, "opus" or "pcm", and gets a 16-byte header, then the stream:
 *
 *   header  "BJAU", u32 16 (its length), u32 48000 (Hz), u16 2 (channels), u16 format,
 *           all little-endian; format 1: PCM, 2: Opus
 *   PCM     signed 16-bit little-endian stereo frames, interleaved
 *   Opus    packets of 10 ms (libopus, 128 kbit/s, restricted low delay), each a u16 little-endian
 *           length and the packet
 *
 * Each format is encoded once per block for all its clients. A client too slow to take a block
 * loses that block (whole blocks or packets only, so its stream stays framed), and one that takes
 * nothing for 2 s is disconnected; the core is never held up. docs/dev/architecture/vnc.md,
 * "Audio". */
struct wv;
struct audio_server;
/* Listens on path (an earlier run's file is removed first); NULL with errno on failure. */
struct audio_server *audio_server_new(const char *path, struct wv *v);
/* Closes the clients, removes the socket, prints the counters. After wv_finish (its clock stops
 * calling the sink there). */
void audio_server_free(struct audio_server *s);
#endif
