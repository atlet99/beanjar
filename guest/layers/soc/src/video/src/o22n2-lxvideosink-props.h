// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Actual O22N2 33.20.23 gst-inspect contract; inherited GstBaseSink properties omitted. W22O
 * ponytail (LX_REL 25) drops interleaving-type and the thumbnail pair and adds
 * variable-speed-playback; queue (26) adds transfer-until-pts and video-sink-offset. */
enum {
    P_APP_TYPE = 1,
    P_ATSC3_MODE,
    P_BASE_PLAYBACK_THRESHOLD,
    P_CLOCK_SOURCE,
    P_CONSTANT_DELAY,
    P_CURRENT_PTS,
    P_DIRECTMEDIA_FRAME_DROP_THRESHOLD,
    P_DIRECTMEDIA_RENDER_BUFFER_LENGTH,
    P_DISPLAY_PREROLL,
    P_DISPLAYED_FRAMES,
    P_DROPPED_FRAMES,
#if LX_REL < 25
    P_INTERLEAVING_TYPE,
#endif
    P_IS_SVP,
    P_LIPSYNC_OFFSET,
    P_LOW_DELAY,
    P_NON_FLUSHABLE_DISPLAYED_FRAMES,
    P_NON_FLUSHABLE_DROPPED_FRAMES,
    P_PREROLL_PTS,
    P_RESOURCE_INFO,
    P_SVP_VERSION,
#if LX_REL < 25
    P_THUMBNAIL_MODE,
    P_THUMBNAIL_STRETCH,
#endif
#if LX_REL >= 26
    P_TRANSFER_UNTIL_PTS,
#endif
#if LX_REL >= 25
    P_VARIABLE_SPEED_PLAYBACK,
#endif
#if LX_REL >= 26
    P_VIDEO_SINK_OFFSET,
#endif
    P_VSINK_ASYNC_HANDLING,
    P_VSINK_SYNC_ON_CLOCK,
    P_WINDOW_ID,
    N_PROPS = P_WINDOW_ID
};
static const struct prop_def props[N_PROPS] = {
    {"app-type", "app-type", G_TYPE_STRING, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"atsc3-mode", "atsc3-mode", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"base-playback-threshold", "base-playback-threshold", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 200, TRUE},
    {"clock-source", "clock-source", G_TYPE_STRING, 0, "default", G_PARAM_READWRITE, NULL, 0, 0,
     FALSE},
    {"constant-delay", "constant-delay", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
    {"current-pts", "current-pts", G_TYPE_UINT64, 0, NULL, G_PARAM_READABLE, NULL, 0, 0, FALSE},
    {"directmedia-frame-drop-threshold", "directmedia-frame-drop-threshold", G_TYPE_INT, 0, NULL,
     G_PARAM_WRITABLE, NULL, 0, 2147483647, TRUE},
    {"directmedia-render-buffer-length", "directmedia-render-buffer-length", G_TYPE_INT, 0, NULL,
     G_PARAM_READABLE, NULL, 0, 2147483647, TRUE},
    {"display-preroll", "display-preroll", G_TYPE_BOOLEAN, 1, NULL, G_PARAM_READWRITE, NULL, 0, 0,
     FALSE},
    {"displayed-frames", "displayed-frames", G_TYPE_UINT, 0, NULL, G_PARAM_READABLE, NULL, 0,
     4294967295, TRUE},
    {"dropped-frames", "dropped-frames", G_TYPE_UINT, 0, NULL, G_PARAM_READABLE, NULL, 0,
     4294967295, TRUE},
#if LX_REL < 25
    {"interleaving-type", "interleaving-type", G_TYPE_INT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     2147483647, TRUE},
#endif
    {"is-svp", "is-svp", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 4294967295, TRUE},
    {"lipsync-offset", "lipsync-offset", G_TYPE_UINT, 400, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
    {"low-delay", "low-delay", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0, FALSE},
    {"non-flushable-displayed-frames", "non-flushable-displayed-frames", G_TYPE_UINT, 0, NULL,
     G_PARAM_READABLE, NULL, 0, 4294967295, TRUE},
    {"non-flushable-dropped-frames", "non-flushable-dropped-frames", G_TYPE_UINT, 0, NULL,
     G_PARAM_READABLE, NULL, 0, 4294967295, TRUE},
    {"preroll-pts", "preroll-pts", G_TYPE_UINT64, 0, NULL, G_PARAM_READABLE, NULL, 0, 0, FALSE},
    {"resource-info", "resource-info", PROP_STRUCTURE, 0, NULL, G_PARAM_WRITABLE, NULL, 0, 0,
     FALSE},
    {"svp-version", "svp-version", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 4294967295,
     TRUE},
#if LX_REL < 25
    {"thumbnail-mode", "thumbnail-mode", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0, 0,
     FALSE},
    {"thumbnail-stretch", "thumbnail-stretch", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     0, FALSE},
#endif
#if LX_REL >= 26
    {"transfer-until-pts", "transfer-until-pts", G_TYPE_INT64, -1, NULL, G_PARAM_READWRITE, NULL, 0,
     0, FALSE},
#endif
#if LX_REL >= 25
    {"variable-speed-playback", "variable-speed-playback", G_TYPE_BOOLEAN, 0, NULL,
     G_PARAM_READWRITE, NULL, 0, 0, FALSE},
#endif
#if LX_REL >= 26
    {"video-sink-offset", "video-sink-offset", G_TYPE_UINT, 0, NULL, G_PARAM_READWRITE, NULL, 0,
     4294967295, TRUE},
#endif
    {"vsink-async-handling", "vsink-async-handling", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE,
     NULL, 0, 0, FALSE},
    {"vsink-sync-on-clock", "vsink-sync-on-clock", G_TYPE_BOOLEAN, 0, NULL, G_PARAM_READWRITE, NULL,
     0, 0, FALSE},
    {"window-id", "window-id", G_TYPE_INT, 0, NULL, G_PARAM_READWRITE, NULL, 0, 3, TRUE},
};
