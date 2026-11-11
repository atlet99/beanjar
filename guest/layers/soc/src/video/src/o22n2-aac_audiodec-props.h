// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The modern LX gst-inspect contract (W22O 04.40 and 13.30, W23O 13.40, W24G 33.20): W23O raises
 * drc-mode's default, W24G adds es-bypass; W22O ponytail (25) drops dts-seamless and adds the
 * write-only downmix and sound-system. LX_DTS: the ponytail and queue builds that keep DTS (W24G
 * 33.31 and 43.21) have W22O's of that release plus dts-seamless. */
enum {
    P_APP_TYPE = 1,
    P_DECODED_SIZE,
    P_DEFAULT_PRL,
    P_DIALOG_ENHANCEMENT_GAIN,
    P_DIALOG_ENHANCEMENT_MAX,
    P_DIALOG_ENHANCEMENT_MIN,
    P_DIRECTMEDIA_TOTAL_AUDIO_BUFFER_SIZE,
    P_DIRECTMEDIA_USED_AUDIO_BUFFER_SIZE,
#if LX_REL >= 25
    P_DOWNMIX,
#endif
    P_DRC_MODE,
#if LX_REL < 25 || defined(LX_DTS)
    P_DTS_SEAMLESS,
#endif
    P_DUAL_MONO,
#if LX_REL >= 24
    P_ES_BYPASS,
#endif
    P_INDEX,
    P_MPEG_DASH,
    P_PLAY_SPEED,
    P_RESOURCE_INFO,
    P_SEAMLESS,
    P_SERVERSIDE_TRICKPLAY,
#if LX_REL >= 25
    P_SOUND_SYSTEM,
#endif
    P_UNDECODED_SIZE,
    P_VERSION,
    N_PROPS = P_VERSION
};
static const struct prop_def props[N_PROPS] = {
    {"app-type", "app-type", G_TYPE_STRING, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"decoded-size", "decoded-size", G_TYPE_UINT64, 0, NULL, G_PARAM_READABLE, NULL, 0, 0, FALSE},
    {"default-prl", "default-prl", G_TYPE_INT, 0, NULL, G_PARAM_WRITABLE, NULL, -31, 0, TRUE},
    {"dialog-enhancement-gain", "dialog-enhancement-gain", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 2147483647, TRUE},
    {"dialog-enhancement-max", "dialog-enhancement-max", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 2147483647, TRUE},
    {"dialog-enhancement-min", "dialog-enhancement-min", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 2147483647, TRUE},
    {"directmedia-total-audio-buffer-size", "directmedia-total-audio-buffer-size", G_TYPE_INT, 0,
     NULL, G_PARAM_READABLE, NULL, 0, 2147483647, TRUE},
    {"directmedia-used-audio-buffer-size", "directmedia-used-audio-buffer-size", G_TYPE_INT, 0,
     NULL, G_PARAM_READABLE, NULL, 0, 2147483647, TRUE},
#if LX_REL >= 25
    {"downmix", "downmix", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0, FALSE},
#endif
    {"drc-mode", "drc-mode", G_TYPE_UINT, LX_REL >= 23 ? 2147483647 : 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 0, FALSE},
#if LX_REL < 25 || defined(LX_DTS)
    {"dts-seamless", "dts-seamless", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
#endif
    {"dual-mono", "dual-mono", G_TYPE_UINT, 1, NULL, G_PARAM_READWRITE, NULL, 0, 2, TRUE},
#if LX_REL >= 24
    {"es-bypass", "es-bypass", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
#endif
    {"index", "index", G_TYPE_UINT, 4294967295, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"mpeg-dash", "mpeg-dash", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"play-speed", "play-speed", G_TYPE_INT, 1000, NULL, G_PARAM_READWRITE, NULL, -2147483648,
     2147483647, TRUE},
    {"resource-info", "resource-info", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0,
     FALSE},
    {"seamless", "seamless", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"serverside-trickplay", "serverside-trickplay", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 0, FALSE},
#if LX_REL >= 25
    {"sound-system", "sound-system", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0, FALSE},
#endif
    {"undecoded-size", "undecoded-size", G_TYPE_UINT64, 0, NULL, G_PARAM_READABLE, NULL, 0, 0,
     FALSE},
    {"version", "version", G_TYPE_STRING, 0, "v1.00.00-2017.10.13", G_PARAM_READABLE, NULL, 0, 0,
     FALSE},
};
