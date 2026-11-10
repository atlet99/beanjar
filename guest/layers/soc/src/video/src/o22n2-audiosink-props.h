// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The modern LX gst-inspect contract (W22O 04.40 and 13.30, W23O 13.40, W24G 33.20); W24G adds
 * queue-max-length; W22O ponytail (25) variable-speed-playback, queue (26) channel-map. Inherited
 * GstBaseSink properties omitted. */
enum {
    P_APP_TYPE = 1,
    P_AUDIO_BE_DELAY,
    P_AUDIO_ONLY,
    P_BASE_PLAYBACK_THRESHOLD,
#if LX_REL >= 26
    P_CHANNEL_MAP,
#endif
    P_CLOCK_SOURCE,
    P_CURRENT_PTS,
    P_DECODED_SIZE,
    P_DIRECTMEDIA_TOTAL_AUDIO_BUFFER_SIZE,
    P_DIRECTMEDIA_USED_AUDIO_BUFFER_SIZE,
    P_DISABLE_GSTSYNC,
    P_FADE,
    P_FADE_VOLUME,
    P_GAME_MODE,
    P_INDEX,
    P_LIPSYNC,
    P_LIPSYNC_OFFSET,
    P_MIXER,
    P_MUTE,
    P_NODELAY,
    P_NODELAY_FAST,
    P_NODELAY_FAST_FREQ,
    P_NODELAY_MODE,
    P_NODELAY_RECOVER,
    P_NODELAY_SKIP,
    P_PCM_BYPASS,
    P_PCM_MULTI_CH,
    P_PLAY_SPEED,
#if LX_REL >= 24
    P_QUEUE_MAX_LENGTH,
#endif
    P_RESOURCE_INFO,
    P_SEAMLESS,
    P_UNDECODED_SIZE,
#if LX_REL >= 25
    P_VARIABLE_SPEED_PLAYBACK,
#endif
    P_VERSION,
    N_PROPS = P_VERSION
};
static const struct prop_def props[N_PROPS] = {
    {"app-type", "app-type", G_TYPE_STRING, 0, "UNKNOWN", G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"audio-be-delay", "audio-be-delay", G_TYPE_UINT, 85, NULL, G_PARAM_READABLE, NULL, 0,
     4294967295, TRUE},
    {"audio-only", "audio-only", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"base-playback-threshold", "base-playback-threshold", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 200, TRUE},
#if LX_REL >= 26
    {"channel-map", "channel-map", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0, FALSE},
#endif
    {"clock-source", "clock-source", G_TYPE_STRING, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"current-pts", "current-pts", G_TYPE_UINT64, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"decoded-size", "decoded-size", G_TYPE_UINT64, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"directmedia-total-audio-buffer-size", "directmedia-total-audio-buffer-size", G_TYPE_INT, 0,
     NULL, G_PARAM_READABLE, NULL, 0, 2147483647, TRUE},
    {"directmedia-used-audio-buffer-size", "directmedia-used-audio-buffer-size", G_TYPE_INT, 0,
     NULL, G_PARAM_READABLE, NULL, 0, 2147483647, TRUE},
    {"disable-gstsync", "disable-gstsync", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0,
     FALSE},
    {"fade", "fade", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0, FALSE},
    {"fade-volume", "fade-volume", G_TYPE_INT, 100, NULL, G_PARAM_READABLE, NULL, 0, 100, TRUE},
    {"game-mode", "game-mode", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"index", "index", G_TYPE_UINT, 4294967295, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"lipsync", "lipsync", G_TYPE_BOOLEAN, 1, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"lipsync-offset", "lipsync-offset", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
    {"mixer", "mixer", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"mute", "mute", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"nodelay", "nodelay", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"nodelay-fast", "nodelay-fast", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 4294967295,
     TRUE},
    {"nodelay-fast-freq", "nodelay-fast-freq", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
    {"nodelay-mode", "nodelay-mode", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 4294967295,
     TRUE},
    {"nodelay-recover", "nodelay-recover", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
    {"nodelay-skip", "nodelay-skip", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 4294967295,
     TRUE},
    {"pcm-bypass", "pcm-bypass", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"pcm-multi-ch", "pcm-multi-ch", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"play-speed", "play-speed", G_TYPE_INT, 0, NULL, G_PARAM_READWRITE, NULL, -2147483648,
     2147483647, TRUE},
#if LX_REL >= 24
    {"queue-max-length", "queue-max-length", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
#endif
    {"resource-info", "resource-info", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0,
     FALSE},
    {"seamless", "seamless", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"undecoded-size", "undecoded-size", G_TYPE_UINT64, 0, NULL, G_PARAM_READABLE, NULL, 0, 0,
     FALSE},
#if LX_REL >= 25
    {"variable-speed-playback", "variable-speed-playback", G_TYPE_BOOLEAN, 0, NULL,
     G_PARAM_READWRITE, NULL, 0, 0, FALSE},
#endif
    {"version", "version", G_TYPE_UINT, 20180329, NULL, G_PARAM_READABLE, NULL, 0, 4294967295,
     TRUE},
};
