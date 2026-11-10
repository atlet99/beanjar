// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* The board's sound engine after the PCM: the master (OSD) volume and the speaker mute, as one
 * playback gain on the guest's sound card (kernel patch 0008, virtsnd_set_playback_gain).
 *
 * On an LG set PulseAudio's pcm_output (hw:0,6) feeds the SoC's sound engine, which applies the
 * OSD volume (LG's sound-engine function 9, "LGSE FN009": the level from the model's volume
 * curve, main byte dB + 127 and low byte 1/16 dB, 0x7F00 at volume 100, 0 silence) and the
 * speaker mute. The firmware sets them its own way per SoC; each stand-in that receives them
 * passes them here, so every target has the one volume model:
 *   - lg115x.ko: KADP's sound-engine call on the audio master, /dev/lg/amst0 (H13, M14, M16P);
 *   - mstar-utopia.ko: the AUDIO module's ADVSOUND parameter of the kernel-mode Utopia (M2);
 *   - rtk.ko: the audio CPU's LGSE FN009 RPC (K5LP);
 *   - the card's own LG control elements, when snd_ctls=1 (webOS 5 and 6's audio adaptation
 *     layer writes "LGSE Var Fn009" [level, volume] and "Mute Output" [output, mute] on hw:0;
 *     W18R's audio HAL shim writes them too).
 * lgemu_snd_osd_level() and lgemu_snd_speaker_mute() are the stand-ins' entry points. */
#define pr_fmt(fmt) KBUILD_MODNAME ": snd: " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/control.h>
#include "lgemu.h"
#include "lgemu-drv.h"

void virtsnd_set_playback_gain(u32 q16); /* kernel patch 0008 (sound/virtio) */

static bool snd_ctls;
module_param(snd_ctls, bool, 0444);
MODULE_PARM_DESC(snd_ctls, "Add the LG control elements (LGSE Var Fn009, Mute Output) to card 0");

#define LEVEL_UNITY 0x7F00u

static DEFINE_MUTEX(lock);
static u32 osd_level = LEVEL_UNITY; /* unity until the firmware sets its volume */
static bool speaker_mute;
static u32 fn009[2] = {LEVEL_UNITY, 100}, mute_output[2];
static struct snd_card *card;

/* 10^(-d/20) and 10^(-f/320) in Q30: whole dB and sixteenths */
static const u32 db_q30[20] = {
    1073741824, 956973408, 852903448, 760150998, 677485290, 603809400, 538145694,
    479622855,  427464319, 380977976, 339546978, 302621563, 269711752, 240380852,
    214239660,  190941298, 170176611, 151670064, 135176087, 120475814,
};
static const u32 fine_q30[16] = {
    1073741824, 1066043361, 1058400094, 1050811628, 1043277569, 1035797527, 1028371116, 1020997950,
    1013677647, 1006409830, 999194121,  992030146,  984917536,  977855921,  970844937,  963884219,
};

/* sixteenths of a dB below 0 dB of a level (0 for 0x7F00 and above) */
static u32 attenuation(u32 level)
{
    int s = ((int)((level >> 8) & 0xff) - 127) * 16 + (int)(level & 0xf);
    return s >= 0 ? 0 : -s;
}

static u32 gain_q16(u32 level)
{
    u32 n = attenuation(level), d = n / 16 % 20, decades = n / 320;
    u64 g = (u64)db_q30[d] * fine_q30[n % 16] >> 30;

    while (decades-- && g)
        g = div_u64(g, 10);
    return (u32)(g >> 14);
}

static void apply(void)
{
    bool mute = speaker_mute || !(osd_level & 0xffff);
    u32 n = attenuation(osd_level);

    virtsnd_set_playback_gain(mute ? 0 : gain_q16(osd_level));
    pr_info("level 0x%04x (-%u.%04u dB)%s\n", osd_level & 0xffff, n / 16, n % 16 * 625,
            mute ? ", muted" : "");
}

void lgemu_snd_osd_level(u32 level)
{
    mutex_lock(&lock);
    if (level != osd_level) {
        osd_level = level;
        apply();
    }
    mutex_unlock(&lock);
}
EXPORT_SYMBOL_GPL(lgemu_snd_osd_level);

void lgemu_snd_speaker_mute(bool on)
{
    mutex_lock(&lock);
    if (on != speaker_mute) {
        speaker_mute = on;
        apply();
    }
    mutex_unlock(&lock);
}
EXPORT_SYMBOL_GPL(lgemu_snd_speaker_mute);

/* ---- the card's LG control elements ---- */

static int ctl_info(struct snd_kcontrol *k, struct snd_ctl_elem_info *info)
{
    info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    info->count = 2;
    info->value.integer.min = 0;
    info->value.integer.max = 0x7fffffff;
    return 0;
}

static int ctl_get(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    u32 *vals = (u32 *)k->private_value;

    mutex_lock(&lock);
    v->value.integer.value[0] = vals[0];
    v->value.integer.value[1] = vals[1];
    mutex_unlock(&lock);
    return 0;
}

static int ctl_put(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    u32 *vals = (u32 *)k->private_value;
    int changed;

    mutex_lock(&lock);
    changed = vals[0] != v->value.integer.value[0] || vals[1] != v->value.integer.value[1];
    vals[0] = v->value.integer.value[0];
    vals[1] = v->value.integer.value[1];
    mutex_unlock(&lock);
    if (vals == fn009)
        lgemu_snd_osd_level(vals[0]);
    else
        lgemu_snd_speaker_mute(vals[1] != 0);
    return changed;
}

/* The sound output's routing and switches (libumi's setControlElement): kept as written, each
 * KEPT_VALUES integers. The card has one output, the speaker PCM, so none of them changes it.
 * "Sndout Spk Output" is the amplifier library's (W21A libaudio-adaptation-layer-amplifier,
 * AALAmpAlsaControl::setAmpParam): every amplifier reset, at start-up and in Prepare Resume,
 * writes the speaker output's channel count there (2 above two amplifiers, else 1), and a
 * missing element fails the reset, so audiooutputd answers prepareResume with error 20. */
#define KEPT_VALUES 4
static const char *const kept_names[] = {
    "Mute Input",
    "Sndout MainAudio Output",
    "Sndout Open",
    "Sndout Close",
    "Sndout Connect",
    "Sndout Disconnect",
    "Sndout EARC OnOff",
    "Sndout EARC Outputtype",
    "Sndout Optic LG",
    "Sndout Optic LightOnOff",
    "Sndout Spdif OutputType",
    "Sndout Spdif CategoryCode",
    "Sndout Spk Output",
};
static long kept[ARRAY_SIZE(kept_names)][KEPT_VALUES];

static int kept_info(struct snd_kcontrol *k, struct snd_ctl_elem_info *info)
{
    info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    info->count = KEPT_VALUES;
    info->value.integer.min = 0;
    info->value.integer.max = 0x7fffffff;
    return 0;
}

static int kept_get(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    mutex_lock(&lock);
    memcpy(v->value.integer.value, kept[k->private_value], sizeof(kept[0]));
    mutex_unlock(&lock);
    return 0;
}

static int kept_put(struct snd_kcontrol *k, struct snd_ctl_elem_value *v)
{
    int changed;

    mutex_lock(&lock);
    changed = memcmp(kept[k->private_value], v->value.integer.value, sizeof(kept[0])) != 0;
    memcpy(kept[k->private_value], v->value.integer.value, sizeof(kept[0]));
    mutex_unlock(&lock);
    return changed;
}

static struct snd_kcontrol_new ctls[] = {
    {.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "LGSE Var Fn009",
     .info = ctl_info,
     .get = ctl_get,
     .put = ctl_put,
     .private_value = (unsigned long)fn009},
    {.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
     .name = "Mute Output",
     .info = ctl_info,
     .get = ctl_get,
     .put = ctl_put,
     .private_value = (unsigned long)mute_output},
};

static int add_kept(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(kept_names); i++) {
        struct snd_kcontrol_new k = {.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
                                     .name = kept_names[i],
                                     .info = kept_info,
                                     .get = kept_get,
                                     .put = kept_put,
                                     .private_value = i};
        int err = snd_ctl_add(card, snd_ctl_new1(&k, NULL));

        if (err)
            return err;
    }
    return 0;
}

int lgemu_snd_init(void)
{
    int err;

    if (!snd_ctls)
        return 0;
    card = snd_card_ref(0);
    if (!card) {
        pr_info("no sound card: no LG control elements\n");
        return 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(ctls); i++) {
        err = snd_ctl_add(card, snd_ctl_new1(&ctls[i], NULL));
        if (err) {
            pr_err("cannot add %s (%d)\n", ctls[i].name, err);
            snd_card_unref(card);
            card = NULL;
            return 0;
        }
    }
    err = add_kept();
    if (err)
        pr_err("cannot add the sound output's elements (%d)\n", err);
    pr_info("card 0: LG control elements %s, %s and %zu of the sound output\n", ctls[0].name,
            ctls[1].name, ARRAY_SIZE(kept_names));
    return 0;
}

void lgemu_snd_exit(void)
{
    if (card) {
        for (size_t i = 0; i < ARRAY_SIZE(ctls); i++) {
            struct snd_ctl_elem_id id = {.iface = ctls[i].iface};

            strscpy(id.name, ctls[i].name, sizeof(id.name));
            snd_ctl_remove_id(card, &id);
        }
        for (size_t i = 0; i < ARRAY_SIZE(kept_names); i++) {
            struct snd_ctl_elem_id id = {.iface = SNDRV_CTL_ELEM_IFACE_MIXER};

            strscpy(id.name, kept_names[i], sizeof(id.name));
            snd_ctl_remove_id(card, &id);
        }
        snd_card_unref(card);
        card = NULL;
    }
    virtsnd_set_playback_gain(0x10000);
}
