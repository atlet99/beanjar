// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libdile_gal: what the parts of the emulator's libdile_gal share (dile-gal.c, gfx-memory.c,
 * gal-draw.c, gal-image.c).
 *
 * The DILE_GAL ABI of firmware 05.06.10.01: libdile_gal.so.1.0.2 passes every call through to
 * libhalgal unchanged (CreateSurface copies the HAL surface field by field), so the types are
 * the HAL's; names from the webOS DILE header (dile_gal.h), sizes checked against gm, libgm and
 * tvservice's DWARF. W16R's (05.70.50.01, MStar M2) is the same, checked in the disassembly of
 * its libdile_gal.so.1.0.2 (the same copies, the 1056-byte surface), bsppart's libhalgal and
 * libkadaptor (gfx_GetDeviceCapability fills these 72 bytes; the HAL reads the cursor, frame
 * buffer property, blit and draw settings at these offsets) and libim's and gm's call sites;
 * it adds SetCursorPosition, SetCursorResolution and SetGraphicOutputResolution. */
#ifndef GAL_H
#define GAL_H
#define _GNU_SOURCE
#include <stdint.h>

#define GAL_OK      0
#define GAL_ERR     (-1)
#define GAL_INVALID (-2)

enum { FMT_ARGB = 0, FMT_LUT8, FMT_ARGB1555, FMT_RGB16, FMT_ARGB4444, FMT_A8, FMT_COUNT };

/* DILE_GAL_SURFACE_INFO_T */
typedef struct {
    uint32_t offset; /* from the start of the graphics memory */
    uint32_t physicalAddress;
    uint16_t pitch, bpp, width, height;
    uint32_t pixelFormat;
    uint32_t palette[256]; /* paletteInfo: colours and length */
    uint32_t paletteLength;
    uint32_t property;
    uint32_t vendorData;
} surface_t;
_Static_assert(sizeof(surface_t) == 1056, "DILE_GAL_SURFACE_INFO_T");

typedef struct {
    uint16_t x, y, w, h;
} rect_t;

/* DILE_GAL_DEVICE_CAPABILITY_INFO_T */
typedef struct {
    uint8_t bAllocatorSupported, pad[3];
    uint32_t blitFlags, drawFlags, supportedFunctions;
    uint8_t maxNumOfLayers, maxNumOfFBHWControllers, pad2[2];
    uint32_t physicalAddrOfBase, gfxMemorySize, virtualAddrOfBase, supportedImageFormat;
    uint32_t hwDecodeMinLimitWidth, hwDecodeMinLimitHeight;
    uint16_t hwDecodeMaxLimitWidth, hwDecodeMaxLimitHeight;
    uint32_t hwDecodeMaxLimitArea, maxScaleUpFactor, minScaleDownFactor, swBlitArea;
    uint16_t hwBlitLimitWidth, hwBlitLimitHeight;
    uint32_t hwBlitLimitArea;
} capability_t;
_Static_assert(sizeof(capability_t) == 72, "DILE_GAL_DEVICE_CAPABILITY_INFO_T");

/* DILE_GAL_DRAW_SETTINGS_T, DILE_GAL_BLIT_SETTINGS_T (the leading fields: the source mask that
 * follows in newer headers is not read) */
typedef struct {
    uint32_t srcBlend, dstBlend, dstColorkey;
} draw_settings_t;
typedef struct {
    uint32_t srcBlend, dstBlend, alpha, color, srcColorkey, dstColorkey;
} blit_settings_t;

/* DILE_GAL_BLIT_FLAGS_T */
#define BLIT_BLEND_ALPHACHANNEL 0x00000001u
#define BLIT_BLEND_COLORALPHA   0x00000002u
#define BLIT_COLORIZE           0x00000004u
#define BLIT_SRC_COLORKEY       0x00000008u
#define BLIT_DST_COLORKEY       0x00000010u
#define BLIT_SRC_PREMULTIPLY    0x00000020u
#define BLIT_DST_PREMULTIPLY    0x00000040u
#define BLIT_DEMULTIPLY         0x00000080u
#define BLIT_SRC_PREMULTCOLOR   0x00000200u
#define BLIT_XOR                0x00000400u
#define BLIT_VERTICAL_MIRROR    0x00400000u
#define BLIT_HORIZONTAL_MIRROR  0x00800000u
/* DILE_GAL_DRAW_FLAGS_T */
#define DRAW_BLEND           0x00000001u
#define DRAW_DST_COLORKEY    0x00000002u
#define DRAW_SRC_PREMULTIPLY 0x00000004u
#define DRAW_DST_PREMULTIPLY 0x00000008u
#define DRAW_DEMULTIPLY      0x00000010u
#define DRAW_XOR             0x00000020u
/* DILE_GAL_BLEND_FUNCTION_T */
enum {
    BLEND_UNKNOWN = 0,
    BLEND_ZERO,
    BLEND_ONE,
    BLEND_SRCCOLOR,
    BLEND_INVSRCCOLOR,
    BLEND_SRCALPHA,
    BLEND_INVSRCALPHA,
    BLEND_DESTALPHA,
    BLEND_INVDESTALPHA,
    BLEND_DESTCOLOR,
    BLEND_INVDESTCOLOR,
    BLEND_SRCALPHASAT
};

/* DILE_GAL_FRAMEBUFFER_PROPERTY_FLAGS_T */
#define FB_PROP_VIEWRECT    0x01u
#define FB_PROP_DISPRECT    0x10u
#define FB_PROP_STEREOSCOPE 0x20u

/* DILE_GAL_CURSOR_PROPERTY_INFO_T: position, panel size, drawn size, hotspot type and position */
typedef struct {
    int16_t x, y;
    uint32_t panelWidth, panelHeight, width, height, hotspot;
    int16_t hotX, hotY;
} cursor_t;
_Static_assert(sizeof(cursor_t) == 28, "DILE_GAL_CURSOR_PROPERTY_INFO_T");

/* DILE_GAL_RESOLUTION_T, passed by value */
typedef struct {
    uint16_t width, height;
} resolution_t;
_Static_assert(sizeof(resolution_t) == 4, "DILE_GAL_RESOLUTION_T");

/* DILE_GAL_CURSOR_POSITION_INFO_T (DILE_GAL_SetCursorPosition, the cursor API of webOS 3's libim
 * in place of MoveCursor): position, drawn size, hotspot type and position. The panel size is
 * DILE_GAL_SetCursorResolution's coordinate resolution instead. */
typedef struct {
    int16_t x, y;
    uint16_t width, height;
    uint32_t hotspot;
    int16_t hotX, hotY;
} cursor_position_t;
_Static_assert(sizeof(cursor_position_t) == 16, "DILE_GAL_CURSOR_POSITION_INFO_T");

/* The controllers gm drives: frame buffer controller 0 is the OSD plane, 2 the cursor plane
 * (DILE_CURSOR_INDEX; gm's GM_WINDOW_GetCursorFBHWControllerIndex). */
#define GFX_CONTROLLERS 4
#define GFX_OSD         0
#define GFX_CURSOR      2

/* ---- pixels (gal-draw.c) ---- */

/* A rectangle of pixels in any format: a surface's memory or a buffer of the shim's own. */
struct pixmap {
    uint8_t *base;
    unsigned pitch, width, height, format;
    const uint32_t *palette; /* LUT8: ARGB colours */
    unsigned palette_length;
};

int gal_fill(struct pixmap *dst, const rect_t *r, uint32_t argb, uint32_t flags,
             const draw_settings_t *settings);
/* src rectangle stretched onto the dst rectangle (nearest sample); both already clipped. */
int gal_blit(const struct pixmap *src, const rect_t *sr, struct pixmap *dst, const rect_t *dr,
             uint32_t flags, const blit_settings_t *settings);
unsigned gal_bpp(unsigned format);

/* ---- images (gal-image.c) ---- */

/* PNG (from a file or memory) as straight-alpha ARGB32, width x height pixels, malloc'd. */
uint32_t *gal_decode_png(const char *path, const void *data, unsigned length, unsigned *width,
                         unsigned *height);

/* ---- the graphics memory and the display engine (gfx-memory.c) ---- */

/* The mapping of the whole graphics memory in this process (NULL without /dev/gfx). */
uint8_t *gfx_memory(void);
/* A new surface's memory (zeroed, page-aligned): its offset, or ~0u when there is no room. */
uint32_t gfx_alloc(uint32_t size);
int gfx_free(uint32_t offset);
void gfx_mem_info(uint32_t *total, uint32_t *allocated, uint32_t *free_total, uint32_t *largest);
/* The palette the engine keeps for a LUT8 surface: the colours in the shared registers, NULL
 * when none was set. */
int gfx_palette_set(uint32_t offset, const uint32_t *colors, unsigned length);
const uint32_t *gfx_palette(uint32_t offset, unsigned *length);
/* Clear a byte range to zero, giving whole pages back to the kernel. */
void gfx_zero(uint32_t offset, uint32_t length);

struct gfx_controller_state {
    uint32_t has_fb, visible, vsync, stereo, stereo_type;
    uint32_t updates; /* frame buffers set so far: a new picture, even in the same surface */
    surface_t fb;
    rect_t view, disp;
    /* when fb is gm's merge: where the compositor's picture went into it (gfx_merge_*) */
    int32_t comp_window; /* compositor window slot, -1 for none */
    rect_t comp_src, comp_dst;
    uint32_t comp_alpha, under;
};
void gfx_controller_set_fb(unsigned index, const surface_t *fb, uint32_t flags, const rect_t *view,
                           const rect_t *disp);
void gfx_controller_set_view(unsigned index, uint32_t flags, const rect_t *view,
                             const rect_t *disp);
void gfx_controller_set_visible(unsigned index, uint32_t visible);
void gfx_controller_set_vsync(unsigned index, uint32_t vsync);
void gfx_controller_set_stereo(unsigned index, uint32_t on, uint32_t type);
int gfx_controller_get(unsigned index, struct gfx_controller_state *out);
void gfx_cursor_move(const cursor_t *c);
/* The resolutions gm and libim set (webOS 3): the graphic output, the space cursor positions are
 * given in, and the one cursor images are made for. {0, 0} until set. */
enum { RES_OUTPUT, RES_CURSOR_COORDINATE, RES_CURSOR_IMAGE, RES_COUNT };
void gfx_resolution_set(unsigned which, resolution_t r);
resolution_t gfx_resolution(unsigned which);

/* The compositor's GM windows (include/webos-gfx.h): the slot whose surface this is, else -1;
 * and the DRM framebuffer with the slot's latest picture (0: none known). */
int gfx_compositor_slot(uint32_t offset);
uint32_t gfx_compositor_fb(int slot);
/* Is this the display engine's process (gm) writing into a surface it made itself: gm composing
 * its own windows, as opposed to a capture into a client's surface? */
int gfx_composing(uint32_t dst_offset);
int gfx_display_process(void);
/* gm's merges (in gm only): a write into a surface, and the compositor window's placeholder. */
enum { MERGE_CLEARED = 1, MERGE_REPLACED };
void gfx_merge_write(const surface_t *dst, int how);
/* what a frame buffer set on a controller holds of the compositor (comp_* and under). */
void gfx_merge_lookup(uint32_t offset, struct gfx_controller_state *out);
int gfx_merge_placeholder(int slot, const rect_t *sr, const surface_t *dst, const rect_t *dr,
                          uint32_t flags, const blit_settings_t *settings);
/* The display engine: in the process that drives the controllers (gm), a thread that shows the
 * cursor plane in the host viewer. */
void gfx_display_start(void);

void gal_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern int gal_trace;

#endif
