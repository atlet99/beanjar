// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libdile_gal: DILE_GAL_DecodeImage's PNG decoder, with the firmware's libpng16 (its simplified
 * API, libpng 1.6), loaded on first use so that the many processes that load libdile_gal without
 * decoding anything (libim's users) do not map it. JPEG is not offered (capability
 * supportedImageFormat is PNG only); callers then decode in software themselves. */
#include "gal.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* png_image of png.h, PNG_IMAGE_VERSION 1 */
struct png_image {
    void *opaque;
    uint32_t version, width, height, format, flags, colormap_entries, warning_or_error;
    char message[64];
};
#define PNG_IMAGE_VERSION 1
#define PNG_FORMAT_BGRA   0x13u /* BGR | COLOR | ALPHA: B,G,R,A bytes = ARGB32 little-endian */

static struct {
    int (*from_file)(struct png_image *, const char *);
    int (*from_memory)(struct png_image *, const void *, size_t);
    int (*finish)(struct png_image *, const void *background, void *buffer, int32_t row_stride,
                  void *colormap);
    void (*free)(struct png_image *);
} png;
static pthread_once_t png_once = PTHREAD_ONCE_INIT;

static void png_load(void)
{
    void *h = dlopen("libpng16.so.16", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        gal_log("image: libpng16: %s", dlerror());
        return;
    }
    png.from_file =
        (int (*)(struct png_image *, const char *))dlsym(h, "png_image_begin_read_from_file");
    png.from_memory = (int (*)(struct png_image *, const void *, size_t))dlsym(
        h, "png_image_begin_read_from_memory");
    png.finish = (int (*)(struct png_image *, const void *, void *, int32_t, void *))dlsym(
        h, "png_image_finish_read");
    png.free = (void (*)(struct png_image *))dlsym(h, "png_image_free");
    if (!png.from_file || !png.from_memory || !png.finish || !png.free) {
        gal_log("image: libpng16 lacks the simplified API");
        png.from_file = NULL;
    }
}

uint32_t *gal_decode_png(const char *path, const void *data, unsigned length, unsigned *width,
                         unsigned *height)
{
    struct png_image img;
    uint32_t *pixels;
    pthread_once(&png_once, png_load);
    if (!png.from_file)
        return NULL;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!(data ? png.from_memory(&img, data, length) : png.from_file(&img, path))) {
        gal_log("image: %s: %s", data ? "(memory)" : path, img.message);
        return NULL;
    }
    img.format = PNG_FORMAT_BGRA;
    pixels = malloc((size_t)img.width * img.height * 4);
    if (!pixels || !png.finish(&img, NULL, pixels, 0, NULL)) {
        gal_log("image: %s: %s", data ? "(memory)" : path, pixels ? img.message : "no memory");
        png.free(&img);
        free(pixels);
        return NULL;
    }
    *width = img.width;
    *height = img.height;
    return pixels;
}
