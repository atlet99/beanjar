// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhalgal: HAL_GAL_DecodeImage's PNG decoder, the firmware's own libpng 1.2 (SS1A's opensrc pak,
 * libpng12.so.0), loaded on first use. The vendor's HAL decoded in MStar's hardware (GPD); this is
 * the same picture in software. libpng 1.2 has no simplified API: its classic one, through dlsym,
 * with errors returned through the error callback (a longjmp out of libpng, as libpng expects of
 * an error_fn), never png_jmpbuf (which needs libpng's struct layout). */
#define _GNU_SOURCE
#include "decode.h"
#include <dlfcn.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_LIBPNG_VER_STRING  "1.2.29" /* SS1A's libpng12.so.0.29.0 */
#define PNG_COLOR_TYPE_PALETTE 3
#define PNG_COLOR_MASK_COLOR   2
#define PNG_COLOR_MASK_ALPHA   4
#define PNG_INFO_tRNS          0x0010
#define PNG_FILLER_AFTER       1

typedef void *png_p;
static struct {
    png_p (*create_read)(const char *, void *, void (*)(png_p, const char *),
                         void (*)(png_p, const char *));
    png_p (*create_info)(png_p);
    void (*destroy_read)(png_p *, png_p *, png_p *);
    void *(*get_error_ptr)(png_p);
    void (*set_read_fn)(png_p, void *, void (*)(png_p, unsigned char *, size_t));
    void *(*get_io_ptr)(png_p);
    void (*init_io)(png_p, FILE *);
    void (*read_info)(png_p, png_p);
    unsigned (*get_IHDR)(png_p, png_p, unsigned long *, unsigned long *, int *, int *, int *, int *,
                         int *);
    unsigned (*get_valid)(png_p, png_p, unsigned);
    void (*set_expand)(png_p);
    void (*set_tRNS_to_alpha)(png_p);
    void (*set_strip_16)(png_p);
    void (*set_gray_to_rgb)(png_p);
    void (*set_bgr)(png_p);
    void (*set_filler)(png_p, unsigned, int);
    int (*set_interlace_handling)(png_p);
    void (*read_update_info)(png_p, png_p);
    void (*read_image)(png_p, unsigned char **);
    void (*read_end)(png_p, png_p);
} png;
static pthread_once_t png_once = PTHREAD_ONCE_INIT;
static const char *png_error_text;

#define SYM(f, n) (*(void **)&png.f = dlsym(h, n))
static void png_load(void)
{
    void *h = dlopen("libpng12.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        png_error_text = dlerror();
        return;
    }
    if (!SYM(create_read, "png_create_read_struct") ||
        !SYM(create_info, "png_create_info_struct") ||
        !SYM(destroy_read, "png_destroy_read_struct") || !SYM(get_error_ptr, "png_get_error_ptr") ||
        !SYM(set_read_fn, "png_set_read_fn") || !SYM(get_io_ptr, "png_get_io_ptr") ||
        !SYM(init_io, "png_init_io") || !SYM(read_info, "png_read_info") ||
        !SYM(get_IHDR, "png_get_IHDR") || !SYM(get_valid, "png_get_valid") ||
        !SYM(set_expand, "png_set_expand") || !SYM(set_tRNS_to_alpha, "png_set_tRNS_to_alpha") ||
        !SYM(set_strip_16, "png_set_strip_16") || !SYM(set_gray_to_rgb, "png_set_gray_to_rgb") ||
        !SYM(set_bgr, "png_set_bgr") || !SYM(set_filler, "png_set_filler") ||
        !SYM(set_interlace_handling, "png_set_interlace_handling") ||
        !SYM(read_update_info, "png_read_update_info") || !SYM(read_image, "png_read_image") ||
        !SYM(read_end, "png_read_end")) {
        png_error_text = "libpng12 lacks a function";
        png.create_read = NULL;
    }
}

struct source {
    jmp_buf jb;
    const unsigned char *data;
    size_t length, at;
    char message[80];
};

static void on_error(png_p p, const char *msg)
{
    struct source *s = png.get_error_ptr(p);
    snprintf(s->message, sizeof s->message, "%s", msg ? msg : "libpng error");
    longjmp(s->jb, 1);
}

static void on_warning(png_p p, const char *msg)
{
    (void)p;
    (void)msg;
}

static void from_memory(png_p p, unsigned char *out, size_t n)
{
    struct source *s = png.get_io_ptr(p);
    if (n > s->length - s->at)
        on_error(p, "image data ends early");
    memcpy(out, s->data + s->at, n);
    s->at += n;
}

uint32_t *decode_png(const char *path, const void *data, size_t length, unsigned *width,
                     unsigned *height, char *why, size_t why_len)
{
    struct source src = {.data = data, .length = length};
    png_p volatile p = NULL;
    png_p volatile info = NULL;
    FILE *volatile f = NULL;
    uint32_t *volatile pixels = NULL;
    unsigned char **volatile rows = NULL;
    unsigned long w, h;
    int depth, type, interlace;

    pthread_once(&png_once, png_load);
    if (!png.create_read) {
        snprintf(why, why_len, "libpng12: %s", png_error_text ? png_error_text : "not loaded");
        return NULL;
    }
    if (!data && !(f = fopen(path, "rb"))) {
        snprintf(why, why_len, "%s: cannot open", path);
        return NULL;
    }
    p = png.create_read(PNG_LIBPNG_VER_STRING, &src, on_error, on_warning);
    if (p)
        info = png.create_info(p);
    if (!p || !info) {
        snprintf(why, why_len, "libpng12: no read struct");
        goto out;
    }
    if (setjmp(src.jb)) {
        snprintf(why, why_len, "%s", src.message);
        free(pixels);
        pixels = NULL;
        goto out;
    }
    if (data)
        png.set_read_fn(p, &src, from_memory);
    else
        png.init_io(p, f);
    png.read_info(p, info);
    png.get_IHDR(p, info, &w, &h, &depth, &type, &interlace, NULL, NULL);
    /* to 8-bit B, G, R, A bytes: ARGB32 on this little-endian CPU, straight alpha */
    png.set_expand(p);
    if (png.get_valid(p, info, PNG_INFO_tRNS))
        png.set_tRNS_to_alpha(p);
    if (depth == 16)
        png.set_strip_16(p);
    if (!(type & PNG_COLOR_MASK_COLOR))
        png.set_gray_to_rgb(p);
    png.set_bgr(p);
    if (!(type & PNG_COLOR_MASK_ALPHA) && !png.get_valid(p, info, PNG_INFO_tRNS))
        png.set_filler(p, 0xff, PNG_FILLER_AFTER);
    png.set_interlace_handling(p);
    png.read_update_info(p, info);
    if (!w || !h || w > 8192 || h > 8192)
        on_error(p, "image size out of range");
    pixels = malloc((size_t)w * h * 4);
    rows = malloc(h * sizeof(*rows));
    if (!pixels || !rows)
        on_error(p, "no memory");
    for (unsigned long y = 0; y < h; y++)
        rows[y] = (unsigned char *)(pixels + y * w);
    png.read_image(p, rows);
    png.read_end(p, NULL);
    *width = (unsigned)w;
    *height = (unsigned)h;
out:
    if (p) {
        png_p pp = p, ip = info;
        png.destroy_read(&pp, ip ? &ip : NULL, NULL);
    }
    free(rows);
    if (f)
        fclose(f);
    return pixels;
}
