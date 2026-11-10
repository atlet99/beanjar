// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Executes extracted production functions; no ALSA device or fabricated playback. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
#define SNDRV_PCM_STREAM_PLAYBACK 0
#define SNDRV_PCM_STREAM_CAPTURE  1
#define SNDRV_PCM_POS_XRUN        (~0UL)
struct work_struct {
    bool queued;
};
struct snd_pcm_status {
    snd_pcm_uframes_t hw_ptr;
};
struct snd_pcm_runtime {
    unsigned long buffer_size, period_size, boundary;
    unsigned long hw_ptr_base, hw_ptr_interrupt, hw_ptr_jiffies, hw_ptr_buffer_jiffies;
    unsigned long appl_ptr;
    struct snd_pcm_status *status;
    bool running;
    unsigned int channels, rate, format;
};
struct snd_pcm_indirect {
    unsigned int hw_io, sw_io, hw_buffer_size, sw_buffer_size;
    int hw_ready;
};
struct snd_pcm_substream;
struct snd_pcm_ops {
    int (*ack)(struct snd_pcm_substream *);
};
struct virtio_device {
    int dev;
};
struct virtio_snd {
    struct virtio_device *vdev;
};
struct virtio_snd_msg {
    int unused;
};
struct virtio_pcm_substream {
    struct virtio_snd *snd;
    struct snd_pcm_substream *owner;
    size_t buffer_bytes, hw_ptr;
    unsigned int msg_count, sid;
    bool suspended, xfer_xrun;
    struct snd_pcm_substream *substream;
    unsigned int nmsgs, direction;
    size_t hw_done, hw_published, hw_seen;
    bool xfer_enabled;
    int lock;
    struct work_struct elapsed_period;
    struct snd_pcm_indirect pcm_indirect;
};
struct snd_pcm_substream {
    struct snd_pcm_runtime *runtime;
    struct snd_pcm_ops *ops;
    struct virtio_pcm_substream *private_data;
};
static struct virtio_device device;
static struct virtio_snd sound = {&device};
static struct virtio_snd_msg message;
static unsigned int message_frees;
static struct snd_pcm_status status;
static struct snd_pcm_runtime runtime;
static struct snd_pcm_substream substream;
static struct virtio_pcm_substream vss;
static int stream_lock, sub_lock, queue_lock;
static unsigned int notifications, acks, inject_completion, warnings, cancellations;
#define WARN_ON_ONCE(condition) ((condition) ? (++warnings, true) : false)
static bool query_between, stop_after_first;
static unsigned long jiffies;
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
static void snd_pcm_stream_lock_irq(struct snd_pcm_substream *s)
{
    assert(s == &substream && !stream_lock && !sub_lock && !queue_lock);
    stream_lock = 1;
}
static void snd_pcm_stream_unlock_irq(struct snd_pcm_substream *s)
{
    assert(s == &substream && stream_lock && !sub_lock && !queue_lock);
    stream_lock = 0;
}
static void fixture_lock(int *lock)
{
    assert(lock == &vss.lock && !sub_lock);
    sub_lock = 1;
}
static void fixture_unlock(int *lock)
{
    assert(lock == &vss.lock && sub_lock);
    sub_lock = 0;
}
#define spin_lock_irqsave(lock, flags)                                                             \
    do {                                                                                           \
        (flags) = 0;                                                                               \
        fixture_lock(lock);                                                                        \
    } while (0)
#define spin_unlock_irqrestore(lock, flags)                                                        \
    do {                                                                                           \
        (void)(flags);                                                                             \
        fixture_unlock(lock);                                                                      \
    } while (0)
struct fixture_guard {
    int *lock;
    bool active;
};
static struct fixture_guard make_guard(int *lock)
{
    fixture_lock(lock);
    return (struct fixture_guard){lock, true};
}
static void end_guard(struct fixture_guard *guard)
{
    fixture_unlock(guard->lock);
}
#define scoped_guard(kind, lock)                                                                   \
    for (struct fixture_guard guard __attribute__((cleanup(end_guard))) = make_guard(lock);        \
         guard.active; guard.active = false)
static bool schedule_work(struct work_struct *work)
{
    bool fresh = !work->queued;
    work->queued = true;
    return fresh;
}
static bool snd_pcm_running(struct snd_pcm_substream *s)
{
    return s->runtime->running;
}
static struct virtio_pcm_substream *snd_pcm_substream_chip(struct snd_pcm_substream *s)
{
    return s->private_data;
}
static size_t snd_pcm_lib_period_bytes(struct snd_pcm_substream *s)
{
    return s->runtime->period_size * 4;
}
static unsigned long bytes_to_frames(struct snd_pcm_runtime *r, size_t bytes)
{
    (void)r;
    return bytes / 4;
}
static snd_pcm_uframes_t virtsnd_pcm_pb_pointer(struct snd_pcm_substream *);

/* The exact pinned ALSA core ring-delta and interrupt-wrap block is inserted
 * by the Python runner; zero elapsed jiffies exercises the failed recovery. */
static void core_update(struct snd_pcm_substream *s, unsigned int in_interrupt)
{
    assert(stream_lock && !sub_lock && !queue_lock);
    struct snd_pcm_runtime *runtime = s->runtime;
    unsigned long pos = virtsnd_pcm_pb_pointer(s);
    assert(pos < runtime->buffer_size);
    unsigned long old_hw_ptr = runtime->status->hw_ptr, new_hw_ptr, hw_base;
    unsigned long curr_jiffies = jiffies;
    long hdelta, delta;
    int crossed_boundary = 0;
    /* CORE_RECOVERY */
    (void)crossed_boundary;
    assert(delta >= 0);
    runtime->hw_ptr_base = hw_base;
    runtime->status->hw_ptr = new_hw_ptr;
    runtime->hw_ptr_jiffies = curr_jiffies;
    if (in_interrupt)
        runtime->hw_ptr_interrupt = new_hw_ptr - new_hw_ptr % runtime->period_size;
}
static int ack(struct snd_pcm_substream *s)
{
    assert(s == &substream && stream_lock && !sub_lock && !queue_lock);
    queue_lock = 1;
    fixture_lock(&vss.lock);
    ++acks;
    fixture_unlock(&vss.lock);
    queue_lock = 0;
    return 0;
}
static struct snd_pcm_ops ops = {.ack = ack};
static void snd_pcm_period_elapsed_under_stream_lock(struct snd_pcm_substream *s)
{
    assert(stream_lock && !sub_lock);
    ++notifications;
    if (s->runtime->running && vss.direction == SNDRV_PCM_STREAM_PLAYBACK) {
        core_update(s, 1);
        if (query_between) {
            unsigned long pointer = status.hw_ptr;
            core_update(s, 0);
            assert(status.hw_ptr == pointer);
        }
        if (stop_after_first) {
            runtime.running = false;
            vss.xfer_enabled = false;
        }
        if (inject_completion) {
            queue_lock = 1;
            fixture_lock(&vss.lock);
            vss.hw_done += snd_pcm_lib_period_bytes(s);
            vss.pcm_indirect.hw_ready += snd_pcm_lib_period_bytes(s);
            fixture_unlock(&vss.lock);
            queue_lock = 0;
            --inject_completion;
        }
    }
}
static void snd_pcm_period_elapsed(struct snd_pcm_substream *s)
{
    snd_pcm_stream_lock_irq(s);
    snd_pcm_period_elapsed_under_stream_lock(s);
    snd_pcm_stream_unlock_irq(s);
}
/* WORKER */
/* POINTER */
static bool cancel_work_sync(struct work_struct *work)
{
    assert(!stream_lock && !sub_lock && !queue_lock && !vss.xfer_enabled);
    bool queued = work->queued;
    work->queued = false;
    if (queued) {
        unsigned int before = notifications;
        virtsnd_pcm_period_elapsed(work);
        assert(notifications == before && !work->queued);
    }
    ++cancellations;
    return queued;
}
/* CANCEL_HELPER */
#define GFP_KERNEL                   0
#define VIRTIO_SND_R_PCM_PREPARE     1
#define WRITE_ONCE(target, value)    ((target) = (value))
#define dev_err(device, format, ...) ((void)(device))
static unsigned int virtsnd_pcm_msg_pending_num(struct virtio_pcm_substream *s)
{
    return s->msg_count;
}
static unsigned long snd_pcm_lib_buffer_bytes(struct snd_pcm_substream *s)
{
    return s->runtime->buffer_size * 4;
}
static int virtsnd_pcm_dev_set_params(struct virtio_pcm_substream *s, unsigned int buffer,
                                      unsigned int period, unsigned int channels,
                                      unsigned int format, unsigned int rate)
{
    (void)s;
    (void)buffer;
    (void)period;
    (void)channels;
    (void)format;
    (void)rate;
    return 0;
}
static struct virtio_snd_msg *virtsnd_pcm_ctl_msg_alloc(struct virtio_pcm_substream *s, int command,
                                                        int allocation)
{
    (void)s;
    (void)command;
    (void)allocation;
    return &message;
}
static int virtsnd_ctl_msg_send_sync(struct virtio_snd *s, struct virtio_snd_msg *msg)
{
    assert(s == &sound && msg == &message && !sub_lock && !stream_lock);
    return 0;
}
static void virtsnd_pcm_msg_free(struct virtio_pcm_substream *s)
{
    assert(s == &vss && !s->xfer_enabled && !s->elapsed_period.queued);
    ++message_frees;
    s->nmsgs = 0;
}
/* PREPARE */
/* HW_FREE */
/* CLOSE */

static void setup(unsigned int periods)
{
    memset(&runtime, 0, sizeof(runtime));
    memset(&status, 0, sizeof(status));
    memset(&vss, 0, sizeof(vss));
    substream = (struct snd_pcm_substream){&runtime, &ops, &vss};
    runtime.buffer_size = periods * 441;
    runtime.period_size = 441;
    runtime.boundary = runtime.buffer_size * 1024;
    runtime.hw_ptr_buffer_jiffies = runtime.buffer_size * 250 / 44100;
    runtime.appl_ptr = runtime.buffer_size;
    runtime.status = &status;
    runtime.running = true;
    vss.snd = &sound;
    vss.owner = &substream;
    vss.substream = &substream;
    vss.nmsgs = periods;
    vss.xfer_enabled = true;
    vss.pcm_indirect.hw_buffer_size = vss.pcm_indirect.sw_buffer_size = runtime.buffer_size * 4;
    vss.pcm_indirect.hw_ready = runtime.buffer_size * 4;
    notifications = acks = inject_completion = warnings = cancellations = message_frees = 0;
    query_between = stop_after_first = false;
    jiffies = 0;
    assert(!stream_lock && !sub_lock && !queue_lock);
}
static void run_worker(void)
{
    vss.elapsed_period.queued = false;
    virtsnd_pcm_period_elapsed(&vss.elapsed_period);
    assert(!stream_lock && !sub_lock && !queue_lock);
}
int main(void)
{
    setup(2);
    vss.hw_done = runtime.buffer_size * 4;
    run_worker();
    /* Negative control: the old worker/pointer loses this whole turn. */
    assert(runtime.status->hw_ptr == runtime.buffer_size);
    assert(notifications == 2 && vss.hw_seen == vss.hw_done);
    assert(vss.pcm_indirect.hw_ready == 0);
    for (unsigned int periods = 2; periods <= 16; periods *= 2) {
        setup(periods);
        query_between = true;
        /* Before publication an app query cannot eat raw completions. */
        vss.hw_done = runtime.buffer_size * 4;
        snd_pcm_stream_lock_irq(&substream);
        core_update(&substream, 0);
        snd_pcm_stream_unlock_irq(&substream);
        assert(status.hw_ptr == 0 && vss.hw_seen == 0);
        run_worker();
        assert(status.hw_ptr == runtime.buffer_size);
        assert(notifications == periods && vss.hw_seen == vss.hw_done);
        for (unsigned int cycle = 1; cycle < 20; ++cycle) {
            vss.pcm_indirect.hw_ready = runtime.buffer_size * 4;
            vss.hw_done += runtime.buffer_size * 4;
            run_worker();
            assert(status.hw_ptr == (cycle + 1) * runtime.buffer_size);
        }
    }
    setup(2);
    vss.hw_done = runtime.buffer_size * 4;
    stop_after_first = true;
    run_worker();
    assert(notifications == 1 && vss.hw_published == 441 * 4);
    run_worker();
    assert(notifications == 1);
    /* Pause/release (no prepare): retain pending real completions. */
    stop_after_first = false;
    runtime.running = vss.xfer_enabled = true;
    run_worker();
    assert(status.hw_ptr == runtime.buffer_size && notifications == 2);
    setup(2);
    runtime.running = false;
    vss.hw_done = 441 * 4;
    run_worker();
    assert(notifications == 0 && vss.hw_published == 0);
    setup(2);
    vss.hw_done = runtime.buffer_size * 4;
    inject_completion = 2;
    run_worker();
    assert(notifications == 2 && vss.elapsed_period.queued);
    run_worker();
    assert(notifications == 4 && vss.hw_seen == vss.hw_done);
    /* Unsigned byte counters can wrap without losing their bounded delta. */
    setup(2);
    vss.hw_done = vss.hw_published = vss.hw_seen = SIZE_MAX - 100;
    vss.hw_done += runtime.buffer_size * 4;
    run_worker();
    assert(status.hw_ptr == runtime.buffer_size && vss.hw_seen == vss.hw_done);
    setup(2);
    vss.nmsgs = 0;
    vss.hw_done = 1764;
    run_worker();
    assert(notifications == 0 && !vss.elapsed_period.queued);
    setup(2);
    runtime.period_size = 0;
    vss.hw_done = 1764;
    run_worker();
    assert(notifications == 0 && !vss.elapsed_period.queued);
    /* STOP wins before queued publication and disables self-requeue. */
    setup(2);
    vss.hw_done = 3528;
    vss.elapsed_period.queued = true;
    virtsnd_pcm_cancel_playback_work(&vss);
    assert(cancellations == 1 && notifications == 0 && vss.hw_published == 0);
    /* Execute genuine prepare/reset and free/close callbacks after cancellation. */
    assert(virtsnd_pcm_prepare(&substream) == 0);
    assert(vss.hw_done == 0 && vss.hw_published == 0 && vss.hw_seen == 0);
    vss.pcm_indirect.hw_ready = runtime.buffer_size * 4;
    vss.xfer_enabled = true;
    vss.hw_done = 1764;
    run_worker();
    assert(status.hw_ptr == 441 && vss.hw_seen == 1764);
    setup(2);
    vss.hw_done = 3528;
    vss.elapsed_period.queued = true;
    assert(virtsnd_pcm_hw_free(&substream) == 0);
    assert(message_frees == 1 && !vss.elapsed_period.queued && !vss.nmsgs);
    setup(2);
    vss.elapsed_period.queued = true;
    vss.hw_done = 1764;
    assert(virtsnd_pcm_close(&substream) == 0);
    assert(vss.owner == NULL && !vss.elapsed_period.queued && notifications == 0);
    setup(2);
    vss.msg_count = 1;
    vss.hw_done = 1764;
    assert(virtsnd_pcm_prepare(&substream) == -EBADFD);
    assert(vss.hw_done == 1764 && !vss.xfer_enabled);
    /* Impossible queue accounting must surface XRUN, not invent or drop bytes. */
    setup(2);
    vss.hw_published = 1764;
    vss.pcm_indirect.hw_ready = 0;
    snd_pcm_stream_lock_irq(&substream);
    assert(virtsnd_pcm_pb_pointer(&substream) == SNDRV_PCM_POS_XRUN);
    snd_pcm_stream_unlock_irq(&substream);
    assert(warnings == 1 && vss.hw_seen == 0);
    setup(2);
    vss.direction = SNDRV_PCM_STREAM_CAPTURE;
    run_worker();
    assert(notifications == 1 && vss.hw_published == 0);
    puts("progress/locks/lifecycle regression PASS");
    return 0;
}
