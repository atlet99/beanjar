// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The classes of Realtek's libgstomx.so on W19P, W20L and W21P, by the type-name gstomx.conf gives
 * each factory: their kind, metadata and pad templates as the stock plugin registers them
 * (gst_caps_to_string of the stock classes' templates, read with the firmware's own GStreamer
 * under qemu-user: class_init only, the instances need the video CPU). rtkomx.c registers a
 * factory for each section of the firmware's gstomx.conf whose type-name is here. */
#if RTK_REL == 19
/* W19P */
{"GstOMXMPEGVideoDec",
 OMX_VIDEO_DEC,
 "OpenMAX MPEG2/MPEG4/XVID Video Decoder",
 "Decode MPEG2/MPEG4/XVID video streams",
 "Realtek",
 "video/mpeg",
 "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
 "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
 "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
 "2147483647/1 ]"},
    {"GstOMXH263Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.263 Video Decoder",
     "Decode H.263 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h263, variant=(string)itu, parsed=(boolean)true",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH264Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.264 Video Decoder",
     "Decode H.264 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h264",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP8Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP8 Video Decoder",
     "Decode VP8 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp8, width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMJPEGDec",
     OMX_VIDEO_DEC,
     "OpenMAX MJPEG Video Decoder",
     "Decode MJPEG video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "image/jpeg, width=(int)[ 0, 1920 ], height=(int)[ 0, 1120 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXWMVDec",
     OMX_VIDEO_DEC,
     "OpenMAX WMV Video Decoder",
     "Decode WMV video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-wmv, wmvversion=(int)3, format=(string){ WMV3, WVC1, WMVA }, width=(int)[ 0, 1920 ], "
     "height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXRVDec",
     OMX_VIDEO_DEC,
     "OpenMAX RV Video Decoder",
     "Decode RV video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-pn-realvideo, width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH265Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.265 Video Decoder",
     "Decode H.265 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h265, parsed=(boolean)true, alignment=(string)au, stream-format=(string){ hvc1, "
     "hev1, byte-stream }, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAVSDec",
     OMX_VIDEO_DEC,
     "OpenMAX AVS Video Decoder",
     "Decode AVS video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-avs, parsed=(boolean)true, alignment=(string)au, stream-format=(string)byte-stream, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]; video/x-cavs, format=(string)avs2",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP9Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP9 Video Decoder",
     "Decode VP9 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp9, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXFLV1Dec",
     OMX_VIDEO_DEC,
     "OpenMAX Sorenson Spark Video Decoder",
     "Decode sorenson spark video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-flash-video, flvversion=(int)1",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAutoVideoDec",
     OMX_VIDEO_DEC,
     "OpenMAX Video Decoder",
     "Decode H.264/H.265/VC1 video streams",
     "Realtek Semiconductor Corp.",
     "video/x-h264, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]; video/x-h265, width=(int)[ 0, "
     "4096 ], height=(int)[ 0, 2304 ]; video/x-wmv, wmvversion=(int)3, format=(string)WVC1, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMP3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX MP3 Audio Decoder",
     "Decode MP3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int)1, layer=(int)[ 1, 3 ], mpegaudioversion=(int)[ 1, 3 ], "
     "rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 2 ], parsed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAutoAudioDec",
     OMX_AUDIO_DEC,
     "OpenMAX Auto Audio Decoder",
     "Decode audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ], framed=(boolean)true; audio/x-ac3, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ 0, 192000 ], "
     "channels=(int)[ 1, 9 ]; audio/x-dts, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; "
     "audio/x-dtsh, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; audio/x-dtsl, rate=(int)[ 0, "
     "192000 ], channels=(int)[ 1, 12 ]; audio/x-dtse, rate=(int)[ 0, 192000 ], channels=(int)[ 1, "
     "12 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAACDec",
     OMX_AUDIO_DEC,
     "OpenMAX AAC Audio Decoder",
     "Decode AAC audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 8000, 655350 ], channels=(int)[ 0, 9 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC3 Audio Decoder",
     "Decode AC3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-ac3, rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ "
     "8000, 48000 ], channels=(int)[ 1, 9 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXWMADec",
     OMX_AUDIO_DEC,
     "OpenMAX WMA Audio Decoder",
     "Decode WMA audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-wma, wmaversion=(int)[ 1, 2 ], block_align=(int)[ 0, 2147483647 ], bitrate=(int)[ 0, "
     "2147483647 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXVorbisDec",
     OMX_AUDIO_DEC,
     "OpenMAX Vorbis Audio Decoder",
     "Decode Vorbis audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-vorbis",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXFLACDec",
     OMX_AUDIO_DEC,
     "OpenMAX FLAC Audio Decoder",
     "Decode FLAC audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-flac, rate=(int)[ 0, 96000 ], channels=(int)[ 1, 6 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXDTSDec",
     OMX_AUDIO_DEC,
     "OpenMAX DTS Audio Decoder",
     "Decode DTS audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-dts, channels=(int)[ 0, 12 ], rate=(int)[ 0, 192000 ]; audio/x-dtsh, channels=(int)[ "
     "1, 12 ], rate=(int)[ 8000, 192000 ]; audio/x-dtsl, channels=(int)[ 1, 12 ], rate=(int)[ "
     "8000, 192000 ]; audio/x-dtse, channels=(int)[ 1, 12 ], rate=(int)[ 8000, 192000 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG726Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G726 Audio Decoder",
     "Decode G726 audio streams",
     "Realtek",
     "audio/x-g726",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG711Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G711 Audio Decoder",
     "Decode G711 audio streams",
     "Realtek",
     "audio/x-mulaw",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRNBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRNB Audio Decoder",
     "Decode AMRNB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRWB Audio Decoder",
     "Decode AMRWB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR-WB",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBPlusDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMR-WB+ Audio Decoder",
     "Decode AMR-WB+ audio streams",
     "Ivan Lee<ivanlee@realtek.com>",
     "audio/amr-wb+",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXRADec",
     OMX_AUDIO_DEC,
     "OpenMAX RA Audio Decoder",
     "Decode RA audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-pn-realaudio",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXOPUSDec",
     OMX_AUDIO_DEC,
     "OpenMAX OPUS Audio Decoder",
     "Decode OPUS audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-opus, channels=(int)[ 1, 6 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXMPEGHDec",
     OMX_AUDIO_DEC,
     "OpenMAX MPEGH Audio Decoder",
     "Decode MPEGH audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg-h",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC4Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC4 Audio Decoder",
     "Decode AC4 audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-ac4",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXLPCMDec",
     OMX_AUDIO_DEC,
     "OpenMAX LPCM Audio Decoder",
     "Decode LPCM audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-private2-lpcm",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
#endif
#if RTK_REL == 20
    /* W20L */
    {"GstOMXMPEGVideoDec",
     OMX_VIDEO_DEC,
     "OpenMAX MPEG2/MPEG4/XVID Video Decoder",
     "Decode MPEG2/MPEG4/XVID video streams",
     "Realtek",
     "video/mpeg",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH263Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.263 Video Decoder",
     "Decode H.263 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h263, variant=(string)itu, parsed=(boolean)true",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH264Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.264 Video Decoder",
     "Decode H.264 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h264",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP8Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP8 Video Decoder",
     "Decode VP8 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp8, width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMJPEGDec",
     OMX_VIDEO_DEC,
     "OpenMAX MJPEG Video Decoder",
     "Decode MJPEG video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "image/jpeg, width=(int)[ 0, 1920 ], height=(int)[ 0, 1120 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXWMVDec",
     OMX_VIDEO_DEC,
     "OpenMAX WMV Video Decoder",
     "Decode WMV video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-wmv, wmvversion=(int)3, format=(string){ WMV3, WVC1, WMVA }, width=(int)[ 0, 1920 ], "
     "height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH265Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.265 Video Decoder",
     "Decode H.265 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h265, parsed=(boolean)true, alignment=(string)au, stream-format=(string){ hvc1, "
     "hev1, byte-stream }, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAVSDec",
     OMX_VIDEO_DEC,
     "OpenMAX AVS Video Decoder",
     "Decode AVS video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-avs, parsed=(boolean)true, alignment=(string)au, stream-format=(string)byte-stream, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]; video/x-cavs, format=(string)avs2",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP9Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP9 Video Decoder",
     "Decode VP9 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp9, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXFLV1Dec",
     OMX_VIDEO_DEC,
     "OpenMAX Sorenson Spark Video Decoder",
     "Decode sorenson spark video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-flash-video, flvversion=(int)1",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAutoVideoDec",
     OMX_VIDEO_DEC,
     "OpenMAX Video Decoder",
     "Decode H.264/H.265/VC1 video streams",
     "Realtek Semiconductor Corp.",
     "video/x-h264, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]; video/x-h265, width=(int)[ 0, "
     "4096 ], height=(int)[ 0, 2304 ]; video/x-wmv, wmvversion=(int)3, format=(string)WVC1, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMP3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX MP3 Audio Decoder",
     "Decode MP3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int)1, layer=(int)[ 1, 3 ], mpegaudioversion=(int)[ 1, 3 ], "
     "rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 2 ], parsed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAutoAudioDec",
     OMX_AUDIO_DEC,
     "OpenMAX Auto Audio Decoder",
     "Decode audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ], framed=(boolean)true; audio/x-ac3, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ 0, 192000 ], "
     "channels=(int)[ 1, 9 ]; audio/x-dts, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; "
     "audio/x-dtsh, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; audio/x-dtsl, rate=(int)[ 0, "
     "192000 ], channels=(int)[ 1, 12 ]; audio/x-dtse, rate=(int)[ 0, 192000 ], channels=(int)[ 1, "
     "12 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAACDec",
     OMX_AUDIO_DEC,
     "OpenMAX AAC Audio Decoder",
     "Decode AAC audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 8000, 655350 ], channels=(int)[ 0, 9 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC3 Audio Decoder",
     "Decode AC3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-ac3, rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ "
     "8000, 48000 ], channels=(int)[ 1, 9 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXWMADec",
     OMX_AUDIO_DEC,
     "OpenMAX WMA Audio Decoder",
     "Decode WMA audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-wma, wmaversion=(int)[ 1, 2 ], block_align=(int)[ 0, 2147483647 ], bitrate=(int)[ 0, "
     "2147483647 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXVorbisDec",
     OMX_AUDIO_DEC,
     "OpenMAX Vorbis Audio Decoder",
     "Decode Vorbis audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-vorbis",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXFLACDec",
     OMX_AUDIO_DEC,
     "OpenMAX FLAC Audio Decoder",
     "Decode FLAC audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-flac, rate=(int)[ 0, 96000 ], channels=(int)[ 1, 6 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG726Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G726 Audio Decoder",
     "Decode G726 audio streams",
     "Realtek",
     "audio/x-g726",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG711Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G711 Audio Decoder",
     "Decode G711 audio streams",
     "Realtek",
     "audio/x-mulaw",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRNBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRNB Audio Decoder",
     "Decode AMRNB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRWB Audio Decoder",
     "Decode AMRWB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR-WB",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBPlusDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMR-WB+ Audio Decoder",
     "Decode AMR-WB+ audio streams",
     "Ivan Lee<ivanlee@realtek.com>",
     "audio/amr-wb+",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXOPUSDec",
     OMX_AUDIO_DEC,
     "OpenMAX OPUS Audio Decoder",
     "Decode OPUS audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-opus, channels=(int)[ 1, 6 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXMPEGHDec",
     OMX_AUDIO_DEC,
     "OpenMAX MPEGH Audio Decoder",
     "Decode MPEGH audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg-h",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC4Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC4 Audio Decoder",
     "Decode AC4 audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-ac4",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXLPCMDec",
     OMX_AUDIO_DEC,
     "OpenMAX LPCM Audio Decoder",
     "Decode LPCM audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-private2-lpcm",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAV1Dec",
     OMX_VIDEO_DEC,
     "OpenMAX AV1 Video Decoder",
     "Decode AV1 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-av1; video/x-dav1",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
#endif
#if RTK_REL == 21
    /* W21P */
    {"GstOMXMPEGVideoDec",
     OMX_VIDEO_DEC,
     "OpenMAX MPEG2/MPEG4/XVID Video Decoder",
     "Decode MPEG2/MPEG4/XVID video streams",
     "Realtek",
     "video/mpeg",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH263Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.263 Video Decoder",
     "Decode H.263 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h263, variant=(string)itu, parsed=(boolean)true",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH264Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.264 Video Decoder",
     "Decode H.264 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h264",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP8Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP8 Video Decoder",
     "Decode VP8 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp8, width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMJPEGDec",
     OMX_VIDEO_DEC,
     "OpenMAX MJPEG Video Decoder",
     "Decode MJPEG video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "image/jpeg, width=(int)[ 0, 1920 ], height=(int)[ 0, 1120 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXWMVDec",
     OMX_VIDEO_DEC,
     "OpenMAX WMV Video Decoder",
     "Decode WMV video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-wmv, wmvversion=(int)3, format=(string){ WMV3, WVC1, WMVA }, width=(int)[ 0, 1920 ], "
     "height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXH265Dec",
     OMX_VIDEO_DEC,
     "OpenMAX H.265 Video Decoder",
     "Decode H.265 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-h265, parsed=(boolean)true, alignment=(string)au, stream-format=(string){ hvc1, "
     "hev1, byte-stream }, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAVSDec",
     OMX_VIDEO_DEC,
     "OpenMAX AVS Video Decoder",
     "Decode AVS video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-avs, parsed=(boolean)true, alignment=(string)au, stream-format=(string)byte-stream, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]; video/x-cavs, format=(string)avs2",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXVP9Dec",
     OMX_VIDEO_DEC,
     "OpenMAX VP9 Video Decoder",
     "Decode VP9 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-vp9, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXFLV1Dec",
     OMX_VIDEO_DEC,
     "OpenMAX Sorenson Spark Video Decoder",
     "Decode sorenson spark video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-flash-video, flvversion=(int)1",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXAutoVideoDec",
     OMX_VIDEO_DEC,
     "OpenMAX Video Decoder",
     "Decode H.264/H.265/VC1 video streams",
     "Realtek Semiconductor Corp.",
     "video/x-h264, width=(int)[ 0, 4096 ], height=(int)[ 0, 2304 ]; video/x-h265, width=(int)[ 0, "
     "4096 ], height=(int)[ 0, 2304 ]; video/x-wmv, wmvversion=(int)3, format=(string)WVC1, "
     "width=(int)[ 0, 1920 ], height=(int)[ 0, 1088 ]",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
    {"GstOMXMP3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX MP3 Audio Decoder",
     "Decode MP3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int)1, layer=(int)[ 1, 3 ], mpegaudioversion=(int)[ 1, 3 ], "
     "rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 2 ], parsed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAutoAudioDec",
     OMX_AUDIO_DEC,
     "OpenMAX Auto Audio Decoder",
     "Decode audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ], framed=(boolean)true; audio/x-ac3, "
     "rate=(int)[ 0, 192000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ 0, 192000 ], "
     "channels=(int)[ 1, 9 ]; audio/x-dts, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; "
     "audio/x-dtsh, rate=(int)[ 0, 192000 ], channels=(int)[ 1, 12 ]; audio/x-dtsl, rate=(int)[ 0, "
     "192000 ], channels=(int)[ 1, 12 ]; audio/x-dtse, rate=(int)[ 0, 192000 ], channels=(int)[ 1, "
     "12 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAACDec",
     OMX_AUDIO_DEC,
     "OpenMAX AAC Audio Decoder",
     "Decode AAC audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/mpeg, mpegversion=(int){ 2, 4 }, stream-format=(string){ raw, adts, adif, loas }, "
     "rate=(int)[ 8000, 655350 ], channels=(int)[ 0, 9 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC3Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC3 Audio Decoder",
     "Decode AC3 audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-ac3, rate=(int)[ 8000, 48000 ], channels=(int)[ 1, 9 ]; audio/x-eac3, rate=(int)[ "
     "8000, 48000 ], channels=(int)[ 1, 9 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXWMADec",
     OMX_AUDIO_DEC,
     "OpenMAX WMA Audio Decoder",
     "Decode WMA audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-wma, wmaversion=(int)[ 1, 2 ], block_align=(int)[ 0, 2147483647 ], bitrate=(int)[ 0, "
     "2147483647 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXVorbisDec",
     OMX_AUDIO_DEC,
     "OpenMAX Vorbis Audio Decoder",
     "Decode Vorbis audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/x-vorbis",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXFLACDec",
     OMX_AUDIO_DEC,
     "OpenMAX FLAC Audio Decoder",
     "Decode FLAC audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-flac, rate=(int)[ 0, 96000 ], channels=(int)[ 1, 6 ], framed=(boolean)true",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG726Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G726 Audio Decoder",
     "Decode G726 audio streams",
     "Realtek",
     "audio/x-g726",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXG711Dec",
     OMX_AUDIO_DEC,
     "OpenMAX G711 Audio Decoder",
     "Decode G711 audio streams",
     "Realtek",
     "audio/x-mulaw",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRNBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRNB Audio Decoder",
     "Decode AMRNB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMRWB Audio Decoder",
     "Decode AMRWB audio streams",
     "Sebastian Dröge <sebastian@centricular.com>",
     "audio/AMR-WB",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAMRWBPlusDec",
     OMX_AUDIO_DEC,
     "OpenMAX AMR-WB+ Audio Decoder",
     "Decode AMR-WB+ audio streams",
     "Ivan Lee<ivanlee@realtek.com>",
     "audio/amr-wb+",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXOPUSDec",
     OMX_AUDIO_DEC,
     "OpenMAX OPUS Audio Decoder",
     "Decode OPUS audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-opus, channels=(int)[ 1, 6 ]",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXMPEGHDec",
     OMX_AUDIO_DEC,
     "OpenMAX MPEGH Audio Decoder",
     "Decode MPEGH audio streams",
     "Realtek Semiconductor Corp.",
     "audio/mpeg-h",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAC4Dec",
     OMX_AUDIO_DEC,
     "OpenMAX AC4 Audio Decoder",
     "Decode AC4 audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-ac4",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXLPCMDec",
     OMX_AUDIO_DEC,
     "OpenMAX LPCM Audio Decoder",
     "Decode LPCM audio streams",
     "Realtek Semiconductor Corp.",
     "audio/x-private2-lpcm",
     "audio/x-raw, rate=(int)[ 1, 2147483647 ], channels=(int)[ 1, 16 ], layout=(string){ "
     "interleaved, non-interleaved }, format=(string){ S8, U8, S16LE, S16BE, U16LE, U16BE, "
     "S24_32LE, S24_32BE, U24_32LE, U24_32BE, S32LE, S32BE, U32LE, U32BE, S24LE, S24BE, U24LE, "
     "U24BE, S20LE, S20BE, U20LE, U20BE, S18LE, S18BE, U18LE, U18BE, F32LE, F32BE, F64LE, F64BE }"},
    {"GstOMXAV1Dec",
     OMX_VIDEO_DEC,
     "OpenMAX AV1 Video Decoder",
     "Decode AV1 video streams",
     "Sebastian Dröge <sebastian.droege@collabora.co.uk>",
     "video/x-av1; video/x-dav1",
     "video/x-raw(memory:dvo), format=(string){ NV12, BGRA }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "
     "video/x-raw(memory:DMABuf), format=(string){ NV12 }, width=(int)[ 0, 2147483647 ], "
     "height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; video/x-raw, "
     "width=(int)[ 0, 2147483647 ], height=(int)[ 0, 2147483647 ], framerate=(fraction)[ 0/1, "
     "2147483647/1 ]"},
#endif
