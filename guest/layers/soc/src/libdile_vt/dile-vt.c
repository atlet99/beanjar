// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libdile_vt.so.0 for the emulator: the video plane for tvservice's tv.capture.
 *
 * tvservice captures the video plane through LG's "VT" (video texture) interface
 * (_CAPTURE_VT_Capture, capture_vt.c): it opens a handle, asks the frame buffer capability, reads
 * every frame buffer's property, freezes the output, reads the current buffer's property, maps
 * each plane's physicalAddress through /dev/mem, copies the main window's picture from the
 * buffer's top-left corner, (window size) x (buffer size / 1920x1080), unfreezes, converts it to
 * RGB (ARGB, RGB or YUV422 semi-planar) and stretches it into the window's place
 * (_CAPTURE_VT_Stretch, through DILE_GAL: libdile_gal). On this model the picture is upside down
 * in the buffer: capture flips it vertically when OSA_MD_IsSupportEPI differs from
 * OSA_MD_IsSupportMirrorMode (_gbFlipH), which it does here (verified: an upright picture came
 * out of a VIDEO capture upside down). The eight functions it imports, types from tvservice's
 * DWARF (prebuilt/lg115x/usr/include/include/dile_vt.h):
 *   DILE_VT_Create(port)                               a handle, NULL on failure
 *   DILE_VT_Destroy(handle)
 *   DILE_VT_SetEventHandler(handle, callback, data)
 *   DILE_VT_GetVideoFrameBufferCapability(handle, CAPABILITY_INFO_T *)
 *                                                      {numOfVideoFrameBuffer, numOfPlane}
 *   DILE_VT_GetAllVideoFrameBufferProperty(handle, CAPABILITY_INFO_T *, PROPERTY_INFO_T *)
 *   DILE_VT_GetCurrentVideoFrameBufferProperty(handle, PROPERTY_INFO_T *, UINT32 *index)
 *                                                      {pixelFormat, stride, width, height,
 *                                                       ppPhysicalAddress[buffer][plane]}
 *   DILE_VT_SetVideoFrameOutputDeviceState(handle, mask, OUTPUT_DEVICE_STATE_INFO_T *)
 *                                                      {bEnabled, bFreezed, bAppliedPQ, ...}
 *   DILE_VT_Debug                                      the debug menu: LG's
 * Without LG's driver there is no frame. Here one ARGB buffer of the display's size lives in the
 * emulator's physical memory (common/physmem.c), and freezing draws into it what the viewer draws
 * for the main video window when it is shown (libhal_lg115x/hal-video.c; host/viewer/video.c):
 * its checkerboard, at the window's size. Nothing shown: a black frame (tvservice decides from its
 * own state whether video is on screen). When real decoding exists this must follow it. Everything
 * else in capture (sizes, cropping, scaling, colour conversion, blending over the OSD, the file
 * formats) is tvservice's own code. */
#include "dile-common.h"
#include <stdint.h>
#include <string.h>
#include "../../../../../include/webos-video.h"

#define VT_OK             0
#define VT_ERR            (-1)
#define FB_W              1920 /* the display: the VT buffer at the display's own size */
#define FB_H              1080
#define PIXEL_FORMAT_ARGB 10 /* DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_ARGB */
#define PLANE_INTERLEAVED 1

typedef struct {
    uint32_t numOfVideoFrameBuffer, numOfPlane;
} capability_t;
typedef struct {
    uint32_t pixelFormat, stride, width, height;
    uint32_t **ppPhysicalAddress;
} property_t;
typedef struct {
    uint8_t bEnabled, bFreezed, bAppliedPQ, pad;
    uint32_t framerateDivide;
} state_t;

typedef int fn4(void *, void *, void *, void *);

static struct vt {
    unsigned phys;
    uint32_t *pixels;
    int frozen;
} vt;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static fn4 *stock4(const char *name)
{
    return (fn4 *)dile_stock(name);
}

#define STOCK(name, a, b, c, d)                                                                    \
    do {                                                                                           \
        static fn4 *f;                                                                             \
        if (!f && !(f = stock4(name)))                                                             \
            return VT_ERR;                                                                         \
        return f((void *)(uintptr_t)(a), (void *)(uintptr_t)(b), (void *)(uintptr_t)(c),           \
                 (void *)(uintptr_t)(d));                                                          \
    } while (0)

/* called with lock held */
static int buffer(const struct dile_hal *hal)
{
    void *virt;
    if (vt.phys)
        return 1;
    vt.phys = hal->alloc(FB_W * FB_H * 4, &virt);
    vt.pixels = virt;
    if (vt.phys)
        dile_log("vt: frame buffer %ux%u ARGB at 0x%08x", FB_W, FB_H, vt.phys);
    return vt.phys != 0;
}

/* called with lock held: the main window's picture at the buffer's top-left corner, bottom row
 * first (the orientation the capture expects on this model, see above) */
static void draw(const struct dile_hal *hal)
{
    struct webos_video_context shown[2];
    unsigned n = hal->shown(shown, 2), w = 0, h = 0;
    if (n) {
        w = shown[0].w > FB_W ? FB_W : shown[0].w;
        h = shown[0].h > FB_H ? FB_H : shown[0].h;
    }
    for (unsigned y = 0; y < FB_H; y++) {
        uint32_t *row = vt.pixels + (size_t)y * FB_W;
        for (unsigned x = 0; x < FB_W; x++)
            row[x] = x < w && y < h ? dile_pattern_argb((int)x, (int)(h - 1 - y)) : 0xFF000000u;
    }
    dile_log("vt: frame of %s (%ux%u)", n ? shown[0].context : "no video", w, h);
}

static void fill(property_t *p, unsigned buffers, unsigned planes)
{
    p->pixelFormat = PIXEL_FORMAT_ARGB;
    p->stride = FB_W * 4;
    p->width = FB_W;
    p->height = FB_H;
    for (unsigned i = 0; p->ppPhysicalAddress && i < buffers; i++)
        for (unsigned k = 0; p->ppPhysicalAddress[i] && k < planes; k++)
            p->ppPhysicalAddress[i][k] = vt.phys;
}

void *DILE_VT_Create(unsigned port)
{
    const struct dile_hal *hal = dile_hal();
    int ok;
    if (!hal) {
        static void *(*f)(unsigned);
        if (!f && !(f = (void *(*)(unsigned))dile_stock("DILE_VT_Create")))
            return NULL;
        return f(port);
    }
    pthread_mutex_lock(&lock);
    ok = buffer(hal);
    pthread_mutex_unlock(&lock);
    return ok ? &vt : NULL;
}

int DILE_VT_Destroy(void *handle)
{
    if (handle != &vt)
        STOCK("DILE_VT_Destroy", handle, 0, 0, 0);
    pthread_mutex_lock(&lock);
    vt.frozen = 0;
    pthread_mutex_unlock(&lock);
    return VT_OK;
}

int DILE_VT_SetEventHandler(void *handle, void *callback, void *data)
{
    if (handle != &vt)
        STOCK("DILE_VT_SetEventHandler", handle, callback, data, 0);
    return VT_OK;
}

int DILE_VT_GetVideoFrameBufferCapability(void *handle, capability_t *cap)
{
    if (handle != &vt)
        STOCK("DILE_VT_GetVideoFrameBufferCapability", handle, cap, 0, 0);
    if (!cap)
        return VT_ERR;
    cap->numOfVideoFrameBuffer = 1;
    cap->numOfPlane = PLANE_INTERLEAVED;
    return VT_OK;
}

int DILE_VT_GetAllVideoFrameBufferProperty(void *handle, capability_t *cap, property_t *prop)
{
    if (handle != &vt)
        STOCK("DILE_VT_GetAllVideoFrameBufferProperty", handle, cap, prop, 0);
    if (!cap || !prop)
        return VT_ERR;
    fill(prop, cap->numOfVideoFrameBuffer, cap->numOfPlane);
    return VT_OK;
}

int DILE_VT_GetCurrentVideoFrameBufferProperty(void *handle, property_t *prop, uint32_t *index)
{
    if (handle != &vt)
        STOCK("DILE_VT_GetCurrentVideoFrameBufferProperty", handle, prop, index, 0);
    if (!prop)
        return VT_ERR;
    fill(prop, 1, PLANE_INTERLEAVED);
    if (index)
        *index = 0;
    return VT_OK;
}

int DILE_VT_SetVideoFrameOutputDeviceState(void *handle, unsigned mask, state_t *state)
{
    const struct dile_hal *hal = dile_hal();
    if (handle != &vt || !hal)
        STOCK("DILE_VT_SetVideoFrameOutputDeviceState", handle, mask, state, 0);
    if (!state)
        return VT_ERR;
    pthread_mutex_lock(&lock);
    /* freezing takes the picture; the buffer stays as it is until the next freeze */
    if (state->bFreezed && !vt.frozen)
        draw(hal);
    vt.frozen = state->bFreezed;
    pthread_mutex_unlock(&lock);
    return VT_OK;
}
