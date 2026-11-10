// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Queue NYX GAL's verified C boundary onto the existing software GAL engine.
 * Surfaces, palettes and controllers remain shared with older DILE clients.
 * The stock BSP stays a dependency for unrelated exports; these calls never
 * initialize its hardware SE command engine.
 */
#include "../libdile_gal/gal.h"
#define EXPORT __attribute__((visibility("default")))
#define FORWARD(name, args, values)                                                                \
    extern int DILE_GAL_##name args;                                                               \
    EXPORT int HAL_GAL_##name args                                                                 \
    {                                                                                              \
        return DILE_GAL_##name values;                                                             \
    }

FORWARD(Init, (void), ())
/* Queue KADP_GAL_Finalize itself returns zero without releasing hardware. The
 * shared software engine likewise lives for the process; DestroySurface owns
 * each surface's release, and other processes retain their allocations. */
EXPORT int HAL_GAL_Finalize(void)
{
    return GAL_OK;
}

extern int DILE_GAL_CreateSurface(unsigned, unsigned, unsigned, surface_t *);
EXPORT int HAL_GAL_CreateSurface(uint16_t w, uint16_t h, uint32_t f, surface_t *s)
{
    return DILE_GAL_CreateSurface(w, h, f, s);
}
FORWARD(DestroySurface, (surface_t * s), (s))
extern int DILE_GAL_FillRectangle(surface_t *, rect_t *, uint32_t, uint32_t *, draw_settings_t *);
EXPORT int HAL_GAL_FillRectangle(surface_t *s, rect_t *r, uint32_t c, uint32_t *f,
                                 draw_settings_t *d)
{
    if ((f && (*f & ~0x3fu)) ||
        (d && (d->srcBlend > BLEND_SRCALPHASAT || d->dstBlend > BLEND_SRCALPHASAT)))
        return GAL_ERR;
    return DILE_GAL_FillRectangle(s, r, c, f, d);
}
static int blit_supported(uint32_t *f, blit_settings_t *b)
{
    return !(f && (*f & ~0x00c006ffu)) &&
           !(b && (b->srcBlend > BLEND_SRCALPHASAT || b->dstBlend > BLEND_SRCALPHASAT));
}
extern int DILE_GAL_Blit(surface_t *, rect_t *, surface_t *, unsigned, unsigned, uint32_t *,
                         blit_settings_t *);
EXPORT int HAL_GAL_Blit(surface_t *s, rect_t *r, surface_t *d, uint16_t x, uint16_t y, uint32_t *f,
                        blit_settings_t *b)
{
    return blit_supported(f, b) ? DILE_GAL_Blit(s, r, d, x, y, f, b) : GAL_ERR;
}
extern int DILE_GAL_StretchBlit(surface_t *, rect_t *, surface_t *, rect_t *, uint32_t *,
                                blit_settings_t *);
EXPORT int HAL_GAL_StretchBlit(surface_t *s, rect_t *r, surface_t *d, rect_t *q, uint32_t *f,
                               blit_settings_t *b)
{
    return blit_supported(f, b) ? DILE_GAL_StretchBlit(s, r, d, q, f, b) : GAL_ERR;
}
FORWARD(SetSurfacePalette, (surface_t * s, uint32_t *p, int n), (s, p, n))
FORWARD(SetFrameBuffer, (surface_t * s, unsigned i), (s, i))
FORWARD(SetFBHWControllerVisibility, (uint32_t *p, unsigned n), (p, n))
FORWARD(GetFBHWControllerVisibility, (unsigned i, uint32_t *v), (i, v))
FORWARD(SetCursorPosition, (cursor_position_t * p), (p))
FORWARD(SetCursorResolution, (resolution_t c, resolution_t i), (c, i))
FORWARD(CaptureFrameBuffer, (surface_t * s), (s))
/* Software capabilities and allocation accounting must describe the same
 * engine when HAL users query them rather than returning the BSP's hardware. */
extern int DILE_GAL_GetDeviceCapability(capability_t *);
EXPORT int HAL_GAL_GetDeviceCapability(capability_t *c)
{
    int rc = DILE_GAL_GetDeviceCapability(c);
    if (!rc) {
        c->blitFlags &= ~0x00300000u;          /* source masks are not implemented */
        c->supportedImageFormat = 0;           /* NYX's GAL interface has no decoder */
        c->supportedFunctions &= ~0x00100000u; /* image decoding */
    }
    return rc;
}
FORWARD(GetGraphicMemInfo, (uint32_t *m), (m))
FORWARD(GetFrameBufferList, (surface_t * *s, unsigned *n), (s, n))
