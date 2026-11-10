// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_AUDIO_H
#define VIEWER_AUDIO_H
#include <SDL.h>
#include <gio/gio.h>
#include <stdio.h>
/* Guest playback from QEMU's D-Bus audio (-audiodev dbus, -display dbus,audiodev=):
 * an org.qemu.Display1.AudioOutListener on a private peer connection. */
struct audio_out {
    /* All stream state below is owned by this context's thread until audio_close joins it.
     * Only closing and the cancellable are signalled across threads. */
    GThread *thread;
    GMainContext *context;
    GMainLoop *loop;
    GDBusConnection *bus;
    GCancellable *cancel;
    gint closing;
    int registering;
    guint registration;
    GDBusConnection *peer;
    SDL_AudioDeviceID dev;
    int use_sdl;
    guint64 id;
    int have_id, enabled, inited;
    unsigned bits, freq, channels, frame, rate; /* rate = bytes per second */
    int is_signed, is_float, be;
    SDL_AudioSpec spec;
    float gain[2];
    int mute, unity;
    unsigned prefill, target, cap; /* SDL queue sizes in bytes */
    int primed, dropping;
    char *wav_path;
    FILE *wav;
    int wav_failed;
    guint64 wav_bytes;
    guint64 writes, frames, bytes, dropped, underruns, toggles, volumes;
    double sumsq;
    guint64 samples;            /* samples in sumsq (16-bit PCM only) */
    gint64 last_write, max_gap; /* host monotonic microseconds */
    int closed, fd;
    /* another sink of the guest's audio (pcm-out.c): each Write as received, after the counters
     * and the WAV, in the format above with the volume above not applied; pcm NULL when the
     * stream stops. Listener thread. Installed before that thread starts. */
    void (*tap)(void *data, const struct audio_out *a, const unsigned char *pcm, size_t len);
    void *tap_data;
};
/* Registers asynchronously on a dedicated thread/context. A VM without D-Bus audio (no
 * -display dbus,audiodev=) prints "audio: none" and the viewer carries on without sound.
 * use_sdl: play through SDL (the caller must not have initialised the SDL audio subsystem). */
void audio_connect(struct audio_out *a, GDBusConnection *bus, int use_sdl, const char *wav,
                   void (*tap)(void *, const struct audio_out *, const unsigned char *, size_t),
                   void *tap_data);
/* Cancels registration/authentication and joins before the tap or its data may be freed. */
void audio_close(struct audio_out *a);
#endif
