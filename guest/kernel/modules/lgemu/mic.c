// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* The set's built-in far-field microphone ("Hey LG"), as a capture card that records silence.
 *
 * LG's voiceinput_sound (webOS 6 and 7's far-field voice stack: ponytail, queue, ombre) takes a
 * udev "sound" device whose sysname has "card", and keeps it when the card's ALSA short name is one
 * of its predefined built-in devices (usr/share/voiceinput/sound/preDefinedDeviceList.json: LGSIC,
 * Mars) with vendor and product 0xffff. Those come from /proc/asound/cardN/usbid when the card has
 * one, and a parsed usbid can never compare equal, so this is no USB card and has none. It probes
 * and then opens hw:N,0 for capture (blocking, S16_LE, interleaved) at the highest rate of its
 * table the card has (48 kHz here) and the fewest channels at it, mixes that down to mono
 * (CSoundUtil::toMono takes stereo only) and resamples to 16 kHz for its preprocessor. So the
 * card is stereo, 8 to 48 kHz. No control element is used.
 *
 * The card is registered after the built-in virtio-sound card (lgemu.ko loads from the initramfs),
 * so the playback card keeps index 0 and its device numbers. A capture runs at real time from a
 * timer, one period at a time, and its buffer holds zeros.
 *
 *   mic  the board's model has the microphone (targets.Soc.mic, a fact of its hardware): register
 *        the card; i2c.c's micom then reports the microphone (0x16) and its switch (0x67), and
 *        without it reports none, as a set without one */
#define pr_fmt(fmt) KBUILD_MODNAME ": mic: " fmt
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "lgemu.h"

#define NAME       "LGSIC"
#define CHANNELS   2
#define BUFFER_MAX (256 * 1024)

static bool mic;
module_param(mic, bool, 0444);
MODULE_PARM_DESC(
    mic, "mic: register the built-in microphone's capture card (LGSIC), which records silence");

bool lgemu_mic_present(void)
{
    return mic;
}

struct mic_stream {
    struct snd_pcm_substream *substream;
    struct hrtimer timer;
    ktime_t start;
    ktime_t period;
    bool running;
};

static enum hrtimer_restart period_done(struct hrtimer *timer)
{
    struct mic_stream *s = container_of(timer, struct mic_stream, timer);

    if (!READ_ONCE(s->running))
        return HRTIMER_NORESTART;
    snd_pcm_period_elapsed(s->substream);
    hrtimer_forward_now(timer, s->period);
    return HRTIMER_RESTART;
}

static const struct snd_pcm_hardware hardware = {
    .info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
            SNDRV_PCM_INFO_BLOCK_TRANSFER,
    .formats = SNDRV_PCM_FMTBIT_S16_LE,
    .rates = SNDRV_PCM_RATE_8000_48000,
    .rate_min = 8000,
    .rate_max = 48000,
    .channels_min = CHANNELS,
    .channels_max = CHANNELS,
    .buffer_bytes_max = BUFFER_MAX,
    .period_bytes_min = 64,
    .period_bytes_max = BUFFER_MAX / 2,
    .periods_min = 2,
    .periods_max = 1024,
};

static int mic_open(struct snd_pcm_substream *substream)
{
    struct mic_stream *s = kzalloc(sizeof(*s), GFP_KERNEL);

    if (!s)
        return -ENOMEM;
    s->substream = substream;
    hrtimer_setup(&s->timer, period_done, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
    substream->runtime->private_data = s;
    substream->runtime->hw = hardware;
    return 0;
}

static int mic_close(struct snd_pcm_substream *substream)
{
    struct mic_stream *s = substream->runtime->private_data;

    hrtimer_cancel(&s->timer);
    kfree(s);
    return 0;
}

static int mic_prepare(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct mic_stream *s = runtime->private_data;

    memset(runtime->dma_area, 0, runtime->dma_bytes); /* the silence the reads return */
    s->period = ns_to_ktime(div_u64((u64)runtime->period_size * NSEC_PER_SEC, runtime->rate));
    return 0;
}

static int mic_trigger(struct snd_pcm_substream *substream, int cmd)
{
    struct mic_stream *s = substream->runtime->private_data;

    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
    case SNDRV_PCM_TRIGGER_RESUME:
        s->start = ktime_get();
        WRITE_ONCE(s->running, true);
        hrtimer_start(&s->timer, s->period, HRTIMER_MODE_REL_SOFT);
        return 0;
    case SNDRV_PCM_TRIGGER_STOP:
    case SNDRV_PCM_TRIGGER_SUSPEND:
        WRITE_ONCE(s->running, false);
        hrtimer_try_to_cancel(&s->timer); /* atomic here: sync_stop waits for a running one */
        return 0;
    }
    return -EINVAL;
}

static int mic_sync_stop(struct snd_pcm_substream *substream)
{
    struct mic_stream *s = substream->runtime->private_data;

    hrtimer_cancel(&s->timer);
    return 0;
}

/* the frames recorded since the start, at the stream's rate */
static snd_pcm_uframes_t mic_pointer(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct mic_stream *s = runtime->private_data;
    u64 frames =
        div_u64((u64)ktime_to_ns(ktime_sub(ktime_get(), s->start)) * runtime->rate, NSEC_PER_SEC);

    return do_div(frames, runtime->buffer_size);
}

static const struct snd_pcm_ops ops = {
    .open = mic_open,
    .close = mic_close,
    .prepare = mic_prepare,
    .trigger = mic_trigger,
    .sync_stop = mic_sync_stop,
    .pointer = mic_pointer,
};

static int mic_probe(struct platform_device *pdev)
{
    struct snd_card *card;
    struct snd_pcm *pcm;
    int err;

    /* -1: the first free index, after the virtio-sound card */
    err = snd_card_new(&pdev->dev, -1, NAME, THIS_MODULE, 0, &card);
    if (err)
        return err;
    strscpy(card->driver, NAME, sizeof(card->driver));
    strscpy(card->shortname, NAME, sizeof(card->shortname)); /* what snd_card_get_name reads */
    strscpy(card->longname, NAME " built-in microphone (lgemu)", sizeof(card->longname));
    err = snd_pcm_new(card, NAME, 0, 0, 1, &pcm);
    if (err)
        goto fail;
    strscpy(pcm->name, NAME, sizeof(pcm->name));
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &ops);
    snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);
    err = snd_card_register(card);
    if (err)
        goto fail;
    platform_set_drvdata(pdev, card);
    pr_info("card %d: %s\n", card->number, card->shortname);
    return 0;

fail:
    snd_card_free(card);
    return err;
}

static void mic_remove(struct platform_device *pdev)
{
    snd_card_free(platform_get_drvdata(pdev));
}

static struct platform_driver driver = {
    .probe = mic_probe,
    .remove = mic_remove,
    .driver = {.name = "lgemu-mic"},
};

static struct platform_device *device;

int lgemu_mic_init(void)
{
    int err;

    if (!mic)
        return 0;
    err = platform_driver_register(&driver);
    if (err)
        return err;
    device = platform_device_register_simple("lgemu-mic", PLATFORM_DEVID_NONE, NULL, 0);
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
    return err;
}

void lgemu_mic_exit(void)
{
    if (!device)
        return;
    platform_device_unregister(device);
    device = NULL;
    platform_driver_unregister(&driver);
}
