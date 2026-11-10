// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The properties of Realtek's GstOMXVideoDec and GstOMXAudioDec on W19P, W20L and W21P, with
 * those its GstVideoDecoder and GstAudioDecoder add, in their installation order (the stock
 * classes' GParamSpecs, read under qemu-user as for rtkomxclasses.h). */
#if RTK_REL == 19
/* W19P */
static const struct prop_def vprops[] = {
    {"change-buffer-meta", "If disable, Do not change the meta of buffer such as pts, duration",
     G_TYPE_BOOLEAN, 1, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of video decoder element's decoded video es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "video decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "The application type", G_TYPE_STRING, 0, NULL, 0},
    {"svp-version", "The version of secure video path", G_TYPE_UINT, 0, NULL, 0},
    {"server-side-trick", "Server side trick playing; Decodes key frames only", G_TYPE_BOOLEAN, 0,
     NULL, 0},
    {"is-svp", "Decode video into secure video path", G_TYPE_BOOLEAN, 0, NULL, 0},
};
static const struct prop_def aprops[] = {
    {"min-latency", "Aggregate output data to a minimum of latency time (ns)", G_TYPE_INT64, 0,
     NULL, 0},
    {"tolerance", "Perfect ts while timestamp jitter/imperfection within tolerance (ns)",
     G_TYPE_INT64, 0, NULL, 0},
    {"plc", "Perform packet loss concealment (if supported)", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of audio decoder element's decoded audio es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "audio decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "Set application type", G_TYPE_STRING, 0, NULL, 0},
    {"dts-seamless", "Specific if DTS audio codec stream's data block size is not constant",
     G_TYPE_BOOLEAN, 0, NULL, 0},
    {"dual-mono", "Specific the output mode if it's dual-mono stream", G_TYPE_UINT, 0, NULL, 0},
    {"mpegh-ui-xml", "XML description of MPEG-H audio UI", G_TYPE_STRING, 0, NULL, 0},
};
#endif
#if RTK_REL == 20
/* W20L */
static const struct prop_def vprops[] = {
    {"change-buffer-meta", "If disable, Do not change the meta of buffer such as pts, duration",
     G_TYPE_BOOLEAN, 1, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of video decoder element's decoded video es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "video decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "The application type", G_TYPE_STRING, 0, NULL, 0},
    {"svp-version", "The version of secure video path", G_TYPE_UINT, 0, NULL, 0},
    {"server-side-trick", "Server side trick playing; Decodes key frames only", G_TYPE_BOOLEAN, 0,
     NULL, 0},
    {"is-svp", "Decode video into secure video path", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"render-mode", "Specific render mode for gav mode/video mode", G_TYPE_STRING, 0, "VIDEO_MODE",
     0},
    {"total-decode-time", "Time value that accumulates decoding time of all frames (in ms)",
     G_TYPE_UINT64, 0, NULL, R},
};
static const struct prop_def aprops[] = {
    {"min-latency", "Aggregate output data to a minimum of latency time (ns)", G_TYPE_INT64, 0,
     NULL, 0},
    {"tolerance", "Perfect ts while timestamp jitter/imperfection within tolerance (ns)",
     G_TYPE_INT64, 0, NULL, 0},
    {"plc", "Perform packet loss concealment (if supported)", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of audio decoder element's decoded audio es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "audio decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "Set application type", G_TYPE_STRING, 0, NULL, 0},
    {"dts-seamless", "Specific if DTS audio codec stream's data block size is not constant",
     G_TYPE_BOOLEAN, 0, NULL, 0},
    {"dual-mono", "Specific the output mode if it's dual-mono stream", G_TYPE_UINT, 0, NULL, 0},
    {"mpegh-ui-xml", "XML description of MPEG-H audio UI", G_TYPE_STRING, 0, NULL, 0},
    {"directmedia-total-audio-buffer-size", "The total size of audio buffer in DirectMedia",
     G_TYPE_INT, 0, NULL, R},
    {"directmedia-used-audio-buffer-size", "The size of used audio buffer in DirectMedia",
     G_TYPE_INT, 0, NULL, R},
    {"buffer-pull-threshold", "The size limitation of audio decoder pull buffer from mq",
     G_TYPE_INT, 0, NULL, 0},
};
#endif
#if RTK_REL == 21
/* W21P */
static const struct prop_def vprops[] = {
    {"change-buffer-meta", "If disable, Do not change the meta of buffer such as pts, duration",
     G_TYPE_BOOLEAN, 1, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of video decoder element's decoded video es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "video decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "The application type", G_TYPE_STRING, 0, NULL, 0},
    {"svp-version", "The version of secure video path", G_TYPE_UINT, 0, NULL, 0},
    {"server-side-trick", "Server side trick playing; Decodes key frames only", G_TYPE_BOOLEAN, 0,
     NULL, 0},
    {"is-svp", "Decode video into secure video path", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"render-mode", "Specific render mode for gav mode/video mode", G_TYPE_STRING, 0, "VIDEO_MODE",
     0},
    {"advr-type", "Specific advr type for dvr file", G_TYPE_STRING, 0, "default", 0},
    {"heap-name", "The heap name of DMA buffer", G_TYPE_STRING, 0, "carveout", 0},
    {"total-decode-time", "Time value that accumulates decoding time of all frames (in ms)",
     G_TYPE_UINT64, 0, NULL, R},
    {"use-irc", "Specific IRC mode for instant_rate_change", G_TYPE_BOOLEAN, 0, NULL, 0},
};
static const struct prop_def aprops[] = {
    {"min-latency", "Aggregate output data to a minimum of latency time (ns)", G_TYPE_INT64, 0,
     NULL, 0},
    {"tolerance", "Perfect ts while timestamp jitter/imperfection within tolerance (ns)",
     G_TYPE_INT64, 0, NULL, 0},
    {"plc", "Perform packet loss concealment (if supported)", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"resource-info", "Hold resource values for resource manager query", PROP_STRUCTURE, 0, NULL,
     0},
    {"decoded-size",
     "The total amount of audio decoder element's decoded audio es after constructing pipeline or "
     "flushing pipeline",
     G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "audio decoder element's total undecoded data", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "Set application type", G_TYPE_STRING, 0, NULL, 0},
    {"dts-seamless", "Specific if DTS audio codec stream's data block size is not constant",
     G_TYPE_BOOLEAN, 0, NULL, 0},
    {"dual-mono", "Specific the output mode if it's dual-mono stream", G_TYPE_UINT, 0, NULL, 0},
    {"mpegh-ui-xml", "XML description of MPEG-H audio UI", G_TYPE_STRING, 0, NULL, 0},
    {"directmedia-total-audio-buffer-size", "The total size of audio buffer in DirectMedia",
     G_TYPE_INT, 0, NULL, R},
    {"directmedia-used-audio-buffer-size", "The size of used audio buffer in DirectMedia",
     G_TYPE_INT, 0, NULL, R},
    {"use-irc", "Specific IRC mode for instant_rate_change", G_TYPE_BOOLEAN, 0, NULL, 0},
    {"buffer-pull-threshold", "The size limitation of audio decoder pull buffer from mq",
     G_TYPE_INT, 0, NULL, 0},
    {"advr-type", "Specific advr type for dvr file", G_TYPE_STRING, 0, "default", 0},
    {"drc-mode", "Dolby Drc mode for media", G_TYPE_INT, 0, NULL, 0},
    {"default-prl", "Setting Default Prl", G_TYPE_INT, 0, NULL, 0},
    {"pcm-bypass", "PCM channel mapping L|R|C|LFE|Ls|Rs", G_TYPE_INT, 0, NULL, W},
    {"pcm-multi-ch", "PCM channel mapping L|R|LFE|C|Ls|Rs", G_TYPE_INT, 0, NULL, W},
};
#endif
