// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
/* Guest playback: QEMU's D-Bus audio (audio/dbusaudio.c) calls org.qemu.Display1.AudioOutListener
 * on a peer-to-peer connection that the viewer hands over with RegisterOutListener. QEMU never
 * waits for the replies and has no backpressure, so the SDL queue is capped here. */
#include "audio.h"
#include "peer.h"
#include "sdl-audio.h"
#include <gio/gunixfdlist.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define AUDIO_PATH   "/org/qemu/Display1/Audio"
#define OUT_LISTENER "/org/qemu/Display1/AudioOutListener"
static const char out_xml[] =
    "<node><interface name='org.qemu.Display1.AudioOutListener'>"
    "<method name='Init'><arg type='t' direction='in'/><arg type='y' direction='in'/>"
    "<arg type='b' direction='in'/><arg type='b' direction='in'/><arg type='u' direction='in'/>"
    "<arg type='y' direction='in'/><arg type='u' direction='in'/><arg type='u' direction='in'/>"
    "<arg type='b' direction='in'/></method>"
    "<method name='Fini'><arg type='t' direction='in'/></method>"
    "<method name='SetEnabled'><arg type='t' direction='in'/><arg type='b' "
    "direction='in'/></method>"
    "<method name='SetVolume'><arg type='t' direction='in'/><arg type='b' direction='in'/>"
    "<arg type='ay' direction='in'/></method>"
    "<method name='Write'><arg type='t' direction='in'/><arg type='ay' direction='in'/></method>"
    "<property name='Interfaces' type='as' access='read'/></interface></node>";

static void put16(unsigned char *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void put32(unsigned char *p, guint64 v)
{
    if (v > 0xffffffffu)
        v = 0xffffffffu;
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
/* RIFF/WAVE header for the current format and wav_bytes of data. */
static void wav_header(struct audio_out *a)
{
    unsigned char h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + a->wav_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, a->is_float ? 3 : 1);
    put16(h + 22, a->channels);
    put32(h + 24, a->freq);
    put32(h + 28, a->rate);
    put16(h + 32, a->frame);
    put16(h + 34, a->bits);
    memcpy(h + 36, "data", 4);
    put32(h + 40, a->wav_bytes);
    if (fseek(a->wav, 0, SEEK_SET) || fwrite(h, sizeof(h), 1, a->wav) != 1 ||
        fseek(a->wav, 0, SEEK_END))
        fprintf(stderr, "audio: cannot write the WAV header\n");
}
/* WAV holds little-endian PCM; unsigned is only valid at 8 bits, signed above. */
static int wav_format_ok(const struct audio_out *a)
{
    if (a->be)
        return 0;
    if (a->is_float)
        return a->bits == 32;
    return a->bits == 8 ? !a->is_signed : a->is_signed && (a->bits == 16 || a->bits == 32);
}
static SDL_AudioFormat sdl_format(const struct audio_out *a)
{
    switch (a->bits) {
    case 8:
        return a->is_signed ? AUDIO_S8 : AUDIO_U8;
    case 16:
        if (a->is_signed)
            return a->be ? AUDIO_S16MSB : AUDIO_S16LSB;
        return a->be ? AUDIO_U16MSB : AUDIO_U16LSB;
    case 32:
        if (a->is_float)
            return a->be ? AUDIO_F32MSB : AUDIO_F32LSB;
        if (a->is_signed)
            return a->be ? AUDIO_S32MSB : AUDIO_S32LSB;
        return 0; /* SDL has no unsigned 32-bit format */
    }
    return 0;
}
static unsigned ms_bytes(const struct audio_out *a, unsigned ms)
{
    guint64 b = (guint64)a->rate * ms / 1000;
    return (unsigned)(b - b % a->frame);
}
static void sdl_stop(struct audio_out *a)
{
    if (!a->dev)
        return;
    SDL_PauseAudioDevice(a->dev, 1);
    SDL_ClearQueuedAudio(a->dev);
    a->primed = 0;
    a->dropping = 0;
}
static void sdl_open(struct audio_out *a)
{
    SDL_AudioSpec want = {0};
    want.freq = a->freq;
    want.format = sdl_format(a);
    want.channels = a->channels;
    want.samples = 512;
    if (a->dev && want.freq == a->spec.freq && want.format == a->spec.format &&
        want.channels == a->spec.channels)
        return;
    if (a->dev) {
        sdl_audio_close(a->dev);
        a->dev = 0;
    }
    if (!want.format || !want.channels) {
        fprintf(stderr, "audio: %u-bit %s%s PCM cannot be played through SDL\n", a->bits,
                a->is_float    ? "float"
                : a->is_signed ? "signed"
                               : "unsigned",
                a->be ? " big-endian" : "");
        return;
    }
    /* SDL converts to the device format; the queue stays in the guest's format. */
    a->dev = sdl_audio_open(&want, &a->spec);
    if (!a->dev) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    a->prefill = ms_bytes(a, 40);
    a->target = ms_bytes(a, 60);
    a->cap = ms_bytes(a, 150);
    a->primed = 0;
    a->dropping = 0;
    fprintf(stderr, "audio: SDL %s, queue prefill %u target %u cap %u bytes\n",
            SDL_GetCurrentAudioDriver(), a->prefill, a->target, a->cap);
}
static void on_init(struct audio_out *a, GVariant *p)
{
    guint64 id;
    guchar bits, ch;
    gboolean sign, flt, be;
    guint32 freq, bpf, bps;
    g_variant_get(p, "(tybbuyuub)", &id, &bits, &sign, &flt, &freq, &ch, &bpf, &bps, &be);
    if (a->have_id && id != a->id) {
        fprintf(stderr, "audio: ignoring a second stream %llu\n", (unsigned long long)id);
        return;
    }
    if (!ch || !bits || bits % 8 || bpf != (unsigned)ch * bits / 8 || !freq || bps != freq * bpf) {
        fprintf(stderr, "audio: invalid Init (%u bits, %u ch, %u B/frame)\n", bits, ch, bpf);
        return;
    }
    int changed = a->inited && (bits != a->bits || !sign != !a->is_signed || !flt != !a->is_float ||
                                freq != a->freq || ch != a->channels || !be != !a->be);
    a->id = id;
    a->have_id = 1;
    a->inited = 1;
    a->bits = bits;
    a->is_signed = sign;
    a->is_float = flt;
    a->freq = freq;
    a->channels = ch;
    a->frame = bpf;
    a->rate = bps;
    a->be = be;
    fprintf(stderr, "audio: Init stream %llu: %u Hz, %u ch, %u-bit %s%s\n", (unsigned long long)id,
            freq, ch, bits,
            flt    ? "float"
            : sign ? "signed"
                   : "unsigned",
            be ? " BE" : " LE");
    if (a->wav && changed) {
        /* One WAV holds one format: keep what was recorded so far. */
        fprintf(stderr, "audio: format changed, WAV recording stops\n");
        fclose(a->wav);
        a->wav = NULL;
        a->wav_failed = 1;
    }
    if (a->wav_path && !a->wav && !a->wav_failed) {
        if (!wav_format_ok(a)) {
            fprintf(stderr, "audio: this format cannot be stored as WAV\n");
            a->wav_failed = 1;
        } else if (!(a->wav = fopen(a->wav_path, "wb"))) {
            fprintf(stderr, "audio: cannot open %s\n", a->wav_path);
            a->wav_failed = 1;
        } else
            wav_header(a);
    }
    if (a->use_sdl)
        sdl_open(a);
}
static void on_enabled(struct audio_out *a, gboolean on)
{
    a->toggles++;
    a->enabled = on;
    if (!on && a->tap)
        a->tap(a->tap_data, a, NULL, 0);
    /* Disabled: the guest stopped the stream (pulse suspends idle sinks). QEMU sends this right
     * after the last Write, so the queue still holds the tail of the sound (up to the cap):
     * let it drain, then audio_poll pauses without counting an underrun. A queue that has not
     * reached the prefill yet is played as it is. */
    if (!on && a->dev && !a->primed && SDL_GetQueuedAudioSize(a->dev)) {
        a->primed = 1;
        SDL_PauseAudioDevice(a->dev, 0);
    }
}
static void on_volume(struct audio_out *a, gboolean mute, GVariant *vol)
{
    gsize n;
    const guchar *v = g_variant_get_fixed_array(vol, &n, 1);
    a->volumes++;
    a->mute = mute;
    a->unity = !mute;
    for (unsigned i = 0; i < 2; i++) {
        a->gain[i] = n ? (i < n ? v[i] : v[n - 1]) / 255.0f : 1.0f;
        if (a->gain[i] != 1.0f)
            a->unity = 0;
    }
    for (gsize i = 2; i < n; i++)
        if (v[i] != 255)
            a->unity = 0;
}
/* Volume and mute from SetVolume, applied to a copy (QEMU skips mixeng_volume for dbus). */
static const void *gain(struct audio_out *a, const guchar *data, gsize len, guchar **copy)
{
    if (a->unity)
        return data;
    *copy = g_malloc(len);
    if (a->mute) {
        memset(*copy, a->spec.silence, len);
        return *copy;
    }
    memcpy(*copy, data, len);
    SDL_AudioFormat f = a->spec.format;
    if (f == AUDIO_S16SYS) {
        gint16 *s = (gint16 *)*copy;
        for (gsize i = 0; i < len / 2; i++)
            s[i] = (gint16)lrintf(s[i] * a->gain[i % a->channels ? 1 : 0]);
    } else if (f == AUDIO_F32SYS) {
        float *s = (float *)*copy;
        for (gsize i = 0; i < len / 4; i++)
            s[i] *= a->gain[i % a->channels ? 1 : 0];
    } /* Other formats play at unity; virtio-snd only ever sends 255/unmuted anyway. */
    return *copy;
}
static void on_write(struct audio_out *a, guint64 id, GVariant *bytes)
{
    gsize len;
    const guchar *data = g_variant_get_fixed_array(bytes, &len, 1);
    if (!a->inited || id != a->id)
        return;
    gint64 now = g_get_monotonic_time();
    if (a->last_write && now - a->last_write > a->max_gap)
        a->max_gap = now - a->last_write;
    a->last_write = now;
    a->writes++;
    a->bytes += len;
    a->frames += len / a->frame;
    if (a->bits == 16 && a->is_signed && !a->be) {
        const guchar *s = data;
        for (gsize i = 0; i + 1 < len; i += 2) {
            double x = (gint16)(s[i] | s[i + 1] << 8);
            a->sumsq += x * x;
        }
        a->samples += len / 2;
    }
    if (a->wav) {
        if (fwrite(data, 1, len, a->wav) != len) {
            fprintf(stderr, "audio: WAV write failed\n");
            fclose(a->wav);
            a->wav = NULL;
            a->wav_failed = 1;
        } else
            a->wav_bytes += len;
    }
    if (a->tap)
        a->tap(a->tap_data, a, data, len);
    if (!a->dev)
        return;
    /* No backpressure from QEMU: past the cap, drop whole chunks until the queue is back at the
     * target, so a stall costs one skip instead of growing latency. */
    Uint32 queued = SDL_GetQueuedAudioSize(a->dev);
    if (queued > a->cap)
        a->dropping = 1;
    if (a->dropping) {
        if (queued > a->target) {
            a->dropped++;
            return;
        }
        a->dropping = 0;
    }
    guchar *copy = NULL;
    if (SDL_QueueAudio(a->dev, gain(a, data, len, &copy), len) < 0)
        fprintf(stderr, "audio: SDL_QueueAudio: %s\n", SDL_GetError());
    g_free(copy);
    if (!a->primed && SDL_GetQueuedAudioSize(a->dev) >= a->prefill) {
        a->primed = 1;
        SDL_PauseAudioDevice(a->dev, 0);
    }
}
static void method(GDBusConnection *conn, const gchar *sender, const gchar *path,
                   const gchar *iface, const gchar *name, GVariant *p, GDBusMethodInvocation *call,
                   gpointer data)
{
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    struct audio_out *a = data;
    if (g_atomic_int_get(&a->closing)) {
        g_dbus_method_invocation_return_error_literal(call, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                      "audio listener is closing");
        return;
    }
    guint64 id = 0;
    if (!strcmp(name, "Init"))
        on_init(a, p);
    else if (!strcmp(name, "Write")) {
        GVariant *bytes;
        g_variant_get(p, "(t@ay)", &id, &bytes);
        on_write(a, id, bytes);
        g_variant_unref(bytes);
    } else if (!strcmp(name, "SetEnabled")) {
        gboolean on;
        g_variant_get(p, "(tb)", &id, &on);
        if (a->have_id && id == a->id)
            on_enabled(a, on);
    } else if (!strcmp(name, "SetVolume")) {
        gboolean mute;
        GVariant *vol;
        g_variant_get(p, "(tb@ay)", &id, &mute, &vol);
        if (a->have_id && id == a->id)
            on_volume(a, mute, vol);
        g_variant_unref(vol);
    } else if (!strcmp(name, "Fini")) {
        g_variant_get(p, "(t)", &id);
        if (a->have_id && id == a->id) {
            sdl_stop(a);
            if (a->tap)
                a->tap(a->tap_data, a, NULL, 0);
            a->have_id = 0;
            a->enabled = 0;
        }
    }
    g_dbus_method_invocation_return_value(call, NULL);
}
static GVariant *property(GDBusConnection *conn, const gchar *sender, const gchar *path,
                          const gchar *iface, const gchar *prop, GError **error, gpointer data)
{
    (void)conn;
    (void)sender;
    (void)path;
    (void)iface;
    (void)prop;
    (void)error;
    (void)data;
    return g_variant_new_strv(NULL, 0);
}
static void closed(GDBusConnection *conn, gboolean remote, GError *error, gpointer data)
{
    (void)conn;
    (void)remote;
    struct audio_out *a = data;
    if (g_atomic_int_get(&a->closing))
        return;
    /* Audio goes; the display may still be there. */
    fprintf(stderr, "audio: listener disconnected%s%s\n", error ? ": " : "",
            error ? error->message : "");
    a->closed = 1;
    sdl_stop(a);
}
static void registered(GObject *source, GAsyncResult *result, gpointer data)
{
    struct audio_out *a = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(source),
                                                                      NULL, result, &error);
    a->registering = 0;
    int fd = a->fd;
    a->fd = -1;
    if (!reply || g_atomic_int_get(&a->closing)) {
        /* No audiodev on the display: QEMU does not export the Audio object. */
        if (!g_atomic_int_get(&a->closing))
            fprintf(stderr, "audio: none (%s)\n", error->message);
        if (reply)
            g_variant_unref(reply);
        g_clear_error(&error);
        close(fd);
        if (g_atomic_int_get(&a->closing))
            g_main_loop_quit(a->loop);
        return;
    }
    g_variant_unref(reply);
    /* QEMU authenticates as the server, then does a synchronous GetAll on the listener object:
     * register it before processing messages. */
    a->peer = peer_connect_cancellable(fd, a->cancel, &error);
    if (!a->peer)
        goto fail;
    g_dbus_connection_set_exit_on_close(a->peer, FALSE);
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(out_xml, &error);
    if (!info)
        goto fail;
    static const GDBusInterfaceVTable table = {.method_call = method, .get_property = property};
    a->registration = g_dbus_connection_register_object(a->peer, OUT_LISTENER, info->interfaces[0],
                                                        &table, a, NULL, &error);
    g_dbus_node_info_unref(info);
    if (!a->registration)
        goto fail;
    g_signal_connect(a->peer, "closed", G_CALLBACK(closed), a);
    g_dbus_connection_start_message_processing(a->peer);
    fprintf(stderr, "audio: listener registered%s%s%s\n", a->use_sdl ? ", SDL output" : "",
            a->wav_path ? ", WAV " : "", a->wav_path ? a->wav_path : "");
    return;
fail:
    if (!g_atomic_int_get(&a->closing))
        fprintf(stderr, "audio: %s\n", error ? error->message : "setup failed");
    g_clear_error(&error);
    if (a->peer) {
        g_object_unref(a->peer);
        a->peer = NULL;
    }
    if (g_atomic_int_get(&a->closing))
        g_main_loop_quit(a->loop);
}
/* Everything from registering the async call through dispatching Writes belongs to the
 * worker context. In particular, registering the object on the caller's context would
 * still leave the incoming audio behind that caller's GL work. */
static void register_audio(struct audio_out *a)
{
    GError *error = NULL;
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) {
        fprintf(stderr, "audio: socketpair failed\n");
        return;
    }
    GUnixFDList *fds = g_unix_fd_list_new();
    int handle = g_unix_fd_list_append(fds, pair[1], &error);
    close(pair[1]);
    if (handle < 0) {
        fprintf(stderr, "audio: %s\n", error->message);
        g_clear_error(&error);
        g_object_unref(fds);
        close(pair[0]);
        return;
    }
    /* Asynchronous: QEMU may be blocked on the display listener (its synchronous GetAll) until
     * the main loop runs, so a synchronous call here could only time out. */
    a->fd = pair[0];
    a->registering = 1;
    g_dbus_connection_call_with_unix_fd_list(
        a->bus, "org.qemu", AUDIO_PATH, "org.qemu.Display1.Audio", "RegisterOutListener",
        g_variant_new("(h)", handle), NULL, G_DBUS_CALL_FLAGS_NONE, 5000, fds, a->cancel,
        registered, a);
    g_object_unref(fds);
}
static gboolean audio_poll(gpointer data)
{
    struct audio_out *a = data;
    /* The queue ran dry while playing: count it (unless the stream was stopped and this is its
     * end), pause, and prime again to the prefill rather than playing every following chunk the
     * moment it arrives. */
    if (a->dev && a->primed && SDL_GetQueuedAudioSize(a->dev) == 0) {
        if (a->enabled)
            a->underruns++;
        a->primed = 0;
        SDL_PauseAudioDevice(a->dev, 1);
    }
    return G_SOURCE_CONTINUE;
}
static void stream_close(struct audio_out *a)
{
    if (a->peer) {
        g_signal_handlers_disconnect_by_data(a->peer, a);
        if (a->registration) {
            g_dbus_connection_unregister_object(a->peer, a->registration);
            a->registration = 0;
        }
        g_dbus_connection_close_sync(a->peer, NULL, NULL);
        g_object_unref(a->peer);
        a->peer = NULL;
    }
    if (a->dev) {
        sdl_audio_close(a->dev);
        a->dev = 0;
    }
    if (a->use_sdl)
        sdl_audio_quit();
    if (a->wav) {
        wav_header(a);
        fclose(a->wav);
        a->wav = NULL;
    }
    if (a->inited) {
        double secs = a->freq ? (double)a->frames / a->freq : 0;
        double rms = a->samples ? sqrt(a->sumsq / a->samples) / 32768.0 : 0;
        fprintf(
            stderr,
            "audio: writes=%llu frames=%llu (%.2f s) rms=%.1f dBFS max_gap=%.1f ms "
            "enabled_changes=%llu volume_changes=%llu dropped=%llu underruns=%llu wav_bytes=%llu\n",
            (unsigned long long)a->writes, (unsigned long long)a->frames, secs,
            rms > 0 ? 20 * log10(rms) : -INFINITY, a->max_gap / 1000.0,
            (unsigned long long)a->toggles, (unsigned long long)a->volumes,
            (unsigned long long)a->dropped, (unsigned long long)a->underruns,
            (unsigned long long)a->wav_bytes);
    } else if (a->closed || a->wav_path)
        fprintf(stderr, "audio: no stream was initialised\n");
    if (a->fd >= 0) {
        close(a->fd);
        a->fd = -1;
    }
}

static gboolean stop_audio(gpointer data)
{
    struct audio_out *a = data;
    /* Cancellation's callback must retire the async registration before the context and
     * audio_out can go away. Authentication uses that same cancellable. */
    if (!a->registering)
        g_main_loop_quit(a->loop);
    return G_SOURCE_REMOVE;
}

static gpointer audio_thread(gpointer data)
{
    struct audio_out *a = data;
    g_main_context_push_thread_default(a->context);
    GSource *poll = NULL;
    int want_sdl = a->use_sdl;
    a->use_sdl = 0;
    if (!g_atomic_int_get(&a->closing)) {
        if (want_sdl) {
            if (sdl_audio_init() < 0)
                fprintf(stderr, "audio: SDL audio unavailable: %s\n", SDL_GetError());
            else
                a->use_sdl = 1;
        }
        register_audio(a);
        if (a->use_sdl) {
            poll = g_timeout_source_new(10);
            g_source_set_callback(poll, audio_poll, a, NULL);
            g_source_attach(poll, a->context);
        }
        g_main_loop_run(a->loop);
    }
    if (poll) {
        g_source_destroy(poll);
        g_source_unref(poll);
    }
    stream_close(a);
    /* Retire ready closures while their data still exists, after unregistering the object. */
    while (g_main_context_pending(a->context))
        g_main_context_iteration(a->context, FALSE);
    g_main_context_pop_thread_default(a->context);
    return NULL;
}

void audio_connect(struct audio_out *a, GDBusConnection *bus, int use_sdl, const char *wav,
                   void (*tap)(void *, const struct audio_out *, const unsigned char *, size_t),
                   void *tap_data)
{
    memset(a, 0, sizeof(*a));
    a->fd = -1;
    a->gain[0] = a->gain[1] = 1.0f;
    a->unity = 1;
    a->use_sdl = use_sdl;
    a->wav_path = wav ? g_strdup(wav) : NULL;
    a->tap = tap;
    a->tap_data = tap_data;
    a->bus = g_object_ref(bus);
    a->cancel = g_cancellable_new();
    a->context = g_main_context_new();
    a->loop = g_main_loop_new(a->context, FALSE);
    GError *error = NULL;
    a->thread = g_thread_try_new("guest-audio", audio_thread, a, &error);
    if (!a->thread) {
        fprintf(stderr, "audio: cannot start listener thread: %s\n", error->message);
        g_clear_error(&error);
    }
}

void audio_close(struct audio_out *a)
{
    if (a->thread) {
        g_atomic_int_set(&a->closing, 1);
        g_cancellable_cancel(a->cancel);
        /* Attach explicitly: g_main_context_invoke may run inline on the caller during
         * startup, before the worker owns its context. Only the worker mutates stream state. */
        GSource *stop = g_idle_source_new();
        g_source_set_priority(stop, G_PRIORITY_HIGH);
        g_source_set_callback(stop, stop_audio, a, NULL);
        g_source_attach(stop, a->context);
        g_source_unref(stop);
        g_thread_join(a->thread);
        a->thread = NULL;
    }
    g_clear_pointer(&a->loop, g_main_loop_unref);
    g_clear_pointer(&a->context, g_main_context_unref);
    g_clear_object(&a->bus);
    g_clear_object(&a->cancel);
    g_clear_pointer(&a->wav_path, g_free);
}
