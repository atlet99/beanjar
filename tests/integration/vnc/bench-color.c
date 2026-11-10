// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
/* Standalone, headless measurement. No thresholds or performance verdicts here. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/opt.h>
#include <libavutil/cpu.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <va/va_drmcommon.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

static void die(const char *what)
{
    fprintf(stderr, "%s\n", what);
    exit(1);
}

static void avcheck(int err, const char *what)
{
    if (err < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, msg, sizeof msg);
        fprintf(stderr, "%s: %s\n", what, msg);
        exit(1);
    }
}

static double now(clockid_t clock)
{
    struct timespec t;
    clock_gettime(clock, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

struct stamp {
    double wall, cpu;
};
static struct stamp stamp(void)
{
    return (struct stamp){now(CLOCK_MONOTONIC), now(CLOCK_PROCESS_CPUTIME_ID)};
}

static void add(struct stamp *sum, struct stamp start)
{
    struct stamp end = stamp();
    sum->wall += end.wall - start.wall;
    sum->cpu += end.cpu - start.cpu;
}

static AVFrame *frame(int w, int h, enum AVPixelFormat fmt)
{
    AVFrame *f = av_frame_alloc();
    if (!f)
        die("av_frame_alloc");
    f->width = w;
    f->height = h;
    f->format = fmt;
    int size = av_image_get_buffer_size(fmt, w, h, 1);
    avcheck(size, "frame buffer size");
    f->buf[0] = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!f->buf[0])
        die("frame buffer allocation");
    avcheck(av_image_fill_arrays(f->data, f->linesize, f->buf[0]->data, fmt, w, h, 1),
            "frame planes");
    return f;
}

/* Integer operations duplicated in the source shader, including frame motion.
 * Rows are deliberately defined in texture/readback order; no flip is timed. */
static void pattern(uint8_t *rgb, int w, int h, int n, int kind)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned r = (x + n * 3) & 255;
            unsigned g = (y + n * 2) & 255;
            unsigned b = ((x / 16 ^ y / 16) * 31 + n) & 255;
            if (kind == 1) {
                unsigned z = (unsigned)x * 1664525u + (unsigned)y * 1013904223u + n;
                z ^= z >> 13;
                z *= 2246822519u;
                r = z & 255;
                g = (z >> 8) & 255;
                b = (z >> 16) & 255;
            } else if (kind == 2) {
                r = ((x + y) & 1) ? 255 : 0;
                g = (x & 1) ? 255 : 0;
                b = (y & 1) ? 255 : 0;
            }
            size_t p = ((size_t)y * w + x) * 4;
            rgb[p] = r;
            rgb[p + 1] = g;
            rgb[p + 2] = b;
            rgb[p + 3] = 255;
        }
}

struct encoder {
    AVCodecContext *ctx;
    AVBufferRef *dev, *frames;
    AVPacket *pkt;
    int va;
    unsigned long long packets, bytes;
};

static struct encoder encoder_open(int w, int h, int va)
{
    struct encoder e = {.va = va};
    const AVCodec *codec = avcodec_find_encoder_by_name(va ? "h264_vaapi" : "libx264");
    if (!codec)
        die("encoder absent");
    e.ctx = avcodec_alloc_context3(codec);
    AVCodecContext *c = e.ctx;
    c->width = w;
    c->height = h;
    c->time_base = (AVRational){1, 60};
    c->framerate = (AVRational){60, 1};
    c->max_b_frames = 0;
    c->gop_size = 600;
    c->rc_max_rate = 10000000;
    c->rc_buffer_size = 5000000;
    c->colorspace = AVCOL_SPC_SMPTE170M;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_IEC61966_2_1;
    c->color_range = AVCOL_RANGE_MPEG;
    if (va) {
        avcheck(
            av_hwdevice_ctx_create(&e.dev, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", NULL, 0),
            "VA device");
        e.frames = av_hwframe_ctx_alloc(e.dev);
        AVHWFramesContext *fc = (void *)e.frames->data;
        fc->format = AV_PIX_FMT_VAAPI;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = w;
        fc->height = h;
        fc->initial_pool_size = 4;
        avcheck(av_hwframe_ctx_init(e.frames), "VA frames");
        c->hw_frames_ctx = av_buffer_ref(e.frames);
        c->pix_fmt = AV_PIX_FMT_VAAPI;
        c->bit_rate = 5000000;
        av_opt_set(c->priv_data, "rc_mode", "VBR", 0);
        av_opt_set(c->priv_data, "profile", "main", 0);
        av_opt_set_int(c->priv_data, "async_depth", 1, 0);
        AVHWDeviceContext *dc = (void *)e.dev->data;
        AVVAAPIDeviceContext *vc = dc->hwctx;
        fprintf(stderr, "VA vendor: %s\n", vaQueryVendorString(vc->display));
    } else {
        c->pix_fmt = AV_PIX_FMT_YUV420P;
        c->thread_count = 0;
        c->qmin = 10;
        av_opt_set(c->priv_data, "preset", "ultrafast", 0);
        av_opt_set(c->priv_data, "tune", "zerolatency", 0);
        av_opt_set(c->priv_data, "profile", "baseline", 0);
        av_opt_set_int(c->priv_data, "crf", 23, 0);
        av_opt_set_int(c->priv_data, "forced-idr", 1, 0);
    }
    avcheck(avcodec_open2(c, codec, NULL), "encoder open");
    e.pkt = av_packet_alloc();
    fprintf(stderr, "encoder=%s threads=%d swscale=%u avcodec=%u\n", codec->name, c->thread_count,
            swscale_version(), avcodec_version());
    return e;
}

static void encode(struct encoder *e, AVFrame *f, int pts)
{
    f->pts = pts;
    avcheck(avcodec_send_frame(e->ctx, f), "send frame");
    int err;
    while ((err = avcodec_receive_packet(e->ctx, e->pkt)) >= 0) {
        e->packets++;
        e->bytes += e->pkt->size;
        av_packet_unref(e->pkt);
    }
    if (err != AVERROR(EAGAIN) && err != AVERROR_EOF)
        avcheck(err, "receive packet");
}

static void encoder_close(struct encoder *e)
{
    av_packet_free(&e->pkt);
    avcodec_free_context(&e->ctx);
    av_buffer_unref(&e->frames);
    av_buffer_unref(&e->dev);
}

struct target {
    GLuint tex, fbo;
    int w, h;
};
struct gpu {
    EGLDisplay display;
    EGLContext context;
    GLuint vao, source_program, convert_program;
    struct target source, atlas;
    int w, h;
};

static GLuint shader(GLenum type, const char *text)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        die(log);
    }
    return s;
}

static GLuint program(const char *fragment)
{
    const char *vertex = "#version 300 es\n"
                         "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                         "gl_Position=vec4(p*2.0-1.0,0,1);}";
    GLuint v = shader(GL_VERTEX_SHADER, vertex), f = shader(GL_FRAGMENT_SHADER, fragment);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        die(log);
    }
    return p;
}

static struct target target_format(int w, int h, GLenum format)
{
    struct target t = {.w = w, .h = h};
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("incomplete FBO");
    return t;
}

static void bind_target(struct target *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
}

static void glcheck(void)
{
    GLenum err = glGetError();
    if (err) {
        fprintf(stderr, "GL error 0x%x\n", err);
        exit(1);
    }
}

static struct gpu gpu_open(int w, int h)
{
    struct gpu g = {.w = w, .h = h};
    g.display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint major, minor;
    if (!eglInitialize(g.display, &major, &minor))
        die("surfaceless EGL initialization failed");
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES3_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_NONE};
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(g.display, attrs, &config, 1, &count) || !count)
        die("no EGL ES3 config");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g.context = eglCreateContext(g.display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (g.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g.display, EGL_NO_SURFACE, EGL_NO_SURFACE, g.context))
        die("EGL context failed");
    fprintf(stderr, "EGL %d.%d renderer=%s version=%s\n", major, minor, glGetString(GL_RENDERER),
            glGetString(GL_VERSION));
    const char *ext = eglQueryString(g.display, EGL_EXTENSIONS);
    fprintf(stderr, "EGL dma_buf_export=%d dma_buf_import=%d\n",
            !!strstr(ext, "EGL_MESA_image_dma_buf_export"),
            !!strstr(ext, "EGL_EXT_image_dma_buf_import"));
    if (strstr((const char *)glGetString(GL_RENDERER), "llvmpipe"))
        die("software renderer selected");
    glDisable(GL_DITHER);
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    g.source = target_format(w, h, GL_RGBA8);
    g.atlas = target_format(w / 4, h * 3 / 2, GL_RGBA8);
    g.source_program =
        program("#version 300 es\nprecision highp float; precision highp int;\n"
                "uniform int frame_number,kind; out vec4 color;\n"
                "void main(){int x=int(gl_FragCoord.x),y=int(gl_FragCoord.y),n=frame_number;"
                "uint r=uint((x+n*3)&255),g=uint((y+n*2)&255),b=uint(((x/16^y/16)*31+n)&255);"
                "if(kind==1){uint z=uint(x)*1664525u+uint(y)*1013904223u+uint(n);"
                "z^=z>>13;z*=2246822519u;r=z&255u;g=(z>>8)&255u;b=(z>>16)&255u;}"
                "else if(kind==2){r=((x+y)&1)!=0?255u:0u;g=(x&1)!=0?255u:0u;b=(y&1)!=0?255u:0u;}"
                "color=vec4(vec3(r,g,b)/255.0,1.0);}");
    g.convert_program =
        program("#version 300 es\nprecision highp float; precision highp int;\n"
                "uniform highp sampler2D rgb; uniform int plane, native_plane; out vec4 color;\n"
                "vec3 sample_rgb(ivec2 p){return "
                "texelFetch(rgb,clamp(p,ivec2(0),textureSize(rgb,0)-1),0).rgb;}"
                "float value(int x,int y,int p){"
                "vec3 c;if(p==0)c=sample_rgb(ivec2(x,y));"
                "else{ivec2 "
                "q=ivec2(x*2,y*2);c=((sample_rgb(q+ivec2(0,-1))+sample_rgb(q+ivec2(1,-1)))*0.125+"
                "(sample_rgb(q)+sample_rgb(q+ivec2(1,0))+sample_rgb(q+ivec2(0,1))+"
                "sample_rgb(q+ivec2(1,1)))*0.375+(sample_rgb(q+ivec2(0,2))+sample_rgb(q+ivec2(1,2))"
                ")*0.125)*0.5;}"
                "float z=p==0?16.0+dot(c,vec3(65.481,128.553,24.966)):"
                "p==1?128.0+dot(c,vec3(-37.797,-74.203,112.0)):"
                "128.0+dot(c,vec3(112.0,-93.786,-18.214));return floor(z+0.5)/255.0;}"
                "void main(){int x=int(gl_FragCoord.x),y=int(gl_FragCoord.y);"
                "if(native_plane>=0){color=native_plane==0?vec4(value(x,y,0),0,0,1):"
                "vec4(value(x,y,1),value(x,y,2),0,1);return;}"
                "x*=4;int h=textureSize(rgb,0).y,w=textureSize(rgb,0).x;"
                "int p=0;if(y>=h){if(plane==3){x/=2;y-=h;p=3;}"
                "else{int i=(y-h)*w+x;p=i>=w*h/4?2:1;if(p==2)i-=w*h/4;x=i%(w/2);y=i/(w/2);}}"
                "if(p==3)color=vec4(value(x,y,1),value(x,y,2),value(x+1,y,1),value(x+1,y,2));"
                "else color=vec4(value(x,y,p),value(x+1,y,p),value(x+2,y,p),value(x+3,y,p));}");
    glcheck();
    return g;
}

static void source(struct gpu *g, int n, int kind)
{
    bind_target(&g->source);
    glUseProgram(g->source_program);
    glUniform1i(glGetUniformLocation(g->source_program, "frame_number"), n);
    glUniform1i(glGetUniformLocation(g->source_program, "kind"), kind);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    /* Exclude the common source production, but include all conversion work. */
    glFinish();
    glcheck();
}

static void read_target(struct target *t, uint8_t *data, int stride)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / 4);
    glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

static void convert_gpu(struct gpu *g, AVFrame *f)
{
    if (f->linesize[0] != g->w || f->data[1] != f->data[0] + (size_t)g->w * g->h ||
        (f->format == AV_PIX_FMT_YUV420P && f->data[2] != f->data[1] + (size_t)g->w * g->h / 4))
        die("GPU readback requires contiguous planes");
    bind_target(&g->atlas);
    glUseProgram(g->convert_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->source.tex);
    glUniform1i(glGetUniformLocation(g->convert_program, "rgb"), 0);
    glUniform1i(glGetUniformLocation(g->convert_program, "plane"),
                f->format == AV_PIX_FMT_NV12 ? 3 : 0);
    glUniform1i(glGetUniformLocation(g->convert_program, "native_plane"), -1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    read_target(&g->atlas, f->data[0], g->w);
    glcheck();
}

struct exported_plane {
    int fd, stride, offset, fourcc;
    uint64_t modifier;
};

static struct exported_plane export_plane(struct gpu *g, struct target *t, const char *name)
{
    struct exported_plane result = {.fd = -1};
    const EGLint attrs[] = {EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_NONE};
    PFNEGLCREATEIMAGEKHRPROC create = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy = (void *)eglGetProcAddress("eglDestroyImageKHR");
    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query =
        (void *)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    PFNEGLEXPORTDMABUFIMAGEMESAPROC export = (void *)eglGetProcAddress("eglExportDMABUFImageMESA");
    if (!create || !destroy || !query || !export)
        return result;
    EGLImageKHR image = create(g->display, g->context, EGL_GL_TEXTURE_2D_KHR,
                               (EGLClientBuffer)(uintptr_t)t->tex, attrs);
    fprintf(stderr, "EGL %s image_created=%d error=0x%x\n", name, image != EGL_NO_IMAGE_KHR,
            eglGetError());
    if (image == EGL_NO_IMAGE_KHR)
        return result;
    int planes = 0;
    EGLuint64KHR modifier = 0;
    EGLBoolean ok = query(g->display, image, &result.fourcc, &planes, &modifier);
    fprintf(stderr, "EGL %s export_query=%d error=0x%x planes=%d fourcc=0x%x modifier=0x%llx\n",
            name, ok, eglGetError(), planes, result.fourcc, (unsigned long long)modifier);
    result.modifier = modifier;
    if (ok && planes == 1) {
        ok = export(g->display, image, &result.fd, &result.stride, &result.offset);
        struct stat st;
        int valid = result.fd >= 0 && !fstat(result.fd, &st);
        fprintf(stderr, "EGL %s export=%d error=0x%x valid_fd=%d fd_value=%d stride=%d offset=%d\n",
                name, ok, eglGetError(), valid, result.fd, result.stride, result.offset);
        if (!ok || !valid) {
            if (result.fd >= 0)
                close(result.fd);
            result.fd = -1;
        }
    }
    destroy(g->display, image);
    return result;
}

static void sharing_probe(struct gpu *g, struct encoder *e, AVFrame *out)
{
    /* Native NV12 planes in the composition context. Probe actual exports; never
     * infer successful interop from an extension string or fabricate an fd. */
    struct target targets[] = {target_format(g->w, g->h, GL_R8),
                               target_format(g->w / 2, g->h / 2, GL_RG8)};
    struct exported_plane exports[2];
    for (int p = 0; p < 2; p++) {
        bind_target(&targets[p]);
        glUseProgram(g->convert_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g->source.tex);
        glUniform1i(glGetUniformLocation(g->convert_program, "rgb"), 0);
        glUniform1i(glGetUniformLocation(g->convert_program, "native_plane"), p);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        glcheck();
        exports[p] = export_plane(g, &targets[p], p ? "UV_RG8" : "Y_R8");
    }
    AVHWDeviceContext *dc = (void *)e->dev->data;
    AVVAAPIDeviceContext *vc = dc->hwctx;
    VAConfigID config;
    VAStatus status =
        vaCreateConfig(vc->display, VAProfileH264Main, VAEntrypointEncSlice, NULL, 0, &config);
    fprintf(stderr, "VA create_config=%d (%s)\n", status, vaErrorStr(status));
    if (status == VA_STATUS_SUCCESS) {
        unsigned count = 0;
        status = vaQuerySurfaceAttributes(vc->display, config, NULL, &count);
        if (status == VA_STATUS_SUCCESS) {
            VASurfaceAttrib *attrs = calloc(count, sizeof *attrs);
            status = vaQuerySurfaceAttributes(vc->display, config, attrs, &count);
            fprintf(stderr, "VA query_surface_attributes=%d (%s)\n", status, vaErrorStr(status));
            if (status == VA_STATUS_SUCCESS)
                for (unsigned i = 0; i < count; i++)
                    if (attrs[i].type == VASurfaceAttribMemoryType)
                        fprintf(
                            stderr, "VA memory_types=0x%x DRM_PRIME_2=%d\n", attrs[i].value.value.i,
                            !!(attrs[i].value.value.i & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2));
            free(attrs);
        }
        vaDestroyConfig(vc->display, config);
    }
    AVFrame *hw = av_frame_alloc();
    avcheck(av_hwframe_get_buffer(e->frames, hw, 0), "sharing probe surface");
    avcheck(av_hwframe_transfer_data(hw, out, 0), "sharing probe upload");
    VASurfaceStatus surface_status;
    status =
        vaQuerySurfaceStatus(vc->display, (VASurfaceID)(uintptr_t)hw->data[3], &surface_status);
    fprintf(stderr, "VA surface_status=%d (%s)\n", status, vaErrorStr(status));
    VADRMPRIMESurfaceDescriptor desc = {0};
    status = vaExportSurfaceHandle(
        vc->display, (VASurfaceID)(uintptr_t)hw->data[3], VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        VA_EXPORT_SURFACE_READ_WRITE | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &desc);
    fprintf(stderr, "VA export_NV12=%d (%s)\n", status, vaErrorStr(status));
    if (status == VA_STATUS_SUCCESS)
        for (unsigned i = 0; i < desc.num_objects; i++)
            close(desc.objects[i].fd);
    av_frame_free(&hw);
    if (exports[0].fd >= 0 && exports[1].fd >= 0) {
        VADRMPRIMESurfaceDescriptor input = {.fourcc = VA_FOURCC_NV12,
                                             .width = g->w,
                                             .height = g->h,
                                             .num_objects = 2,
                                             .num_layers = 2};
        for (int p = 0; p < 2; p++) {
            struct stat st;
            if (fstat(exports[p].fd, &st))
                die("export fstat");
            input.objects[p].fd = exports[p].fd;
            input.objects[p].size = st.st_size;
            input.objects[p].drm_format_modifier = exports[p].modifier;
            input.layers[p].drm_format = exports[p].fourcc;
            input.layers[p].num_planes = 1;
            input.layers[p].object_index[0] = p;
            input.layers[p].offset[0] = exports[p].offset;
            input.layers[p].pitch[0] = exports[p].stride;
        }
        VASurfaceAttrib attrs[2] = {
            {.type = VASurfaceAttribMemoryType,
             .flags = VA_SURFACE_ATTRIB_SETTABLE,
             .value = {.type = VAGenericValueTypeInteger,
                       .value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2}},
            {.type = VASurfaceAttribExternalBufferDescriptor,
             .flags = VA_SURFACE_ATTRIB_SETTABLE,
             .value = {.type = VAGenericValueTypePointer, .value.p = &input}}};
        VASurfaceID imported;
        status =
            vaCreateSurfaces(vc->display, VA_RT_FORMAT_YUV420, g->w, g->h, &imported, 1, attrs, 2);
        fprintf(stderr, "VA import_NV12=%d (%s)\n", status, vaErrorStr(status));
        if (status == VA_STATUS_SUCCESS)
            vaDestroySurfaces(vc->display, &imported, 1);
    } else
        fprintf(stderr, "VA import_NV12=blocked_no_valid_export_fds\n");
    for (int p = 0; p < 2; p++) {
        if (exports[p].fd >= 0)
            close(exports[p].fd);
        glDeleteFramebuffers(1, &targets[p].fbo);
        glDeleteTextures(1, &targets[p].tex);
    }
}

static int reference(const uint8_t *rgb, int w, int h, int x, int y, int plane)
{
    const double matrix[3][3] = {
        {65.481, 128.553, 24.966}, {-37.797, -74.203, 112}, {112, -93.786, -18.214}};
    const double weight[4] = {0.125, 0.375, 0.375, 0.125};
    double c[3] = {0};
    if (!plane) {
        for (int k = 0; k < 3; k++)
            c[k] = rgb[((size_t)y * w + x) * 4 + k];
    } else {
        for (int row = 0; row < 4; row++) {
            int sy = y * 2 + row - 1;
            if (sy < 0)
                sy = 0;
            if (sy >= h)
                sy = h - 1;
            for (int col = 0; col < 2; col++)
                for (int k = 0; k < 3; k++)
                    c[k] += rgb[((size_t)sy * w + x * 2 + col) * 4 + k] * weight[row] * 0.5;
        }
    }
    double value = plane ? 128 : 16;
    for (int k = 0; k < 3; k++)
        value += c[k] * matrix[plane][k] / 255;
    return (int)(value + 0.5);
}

static void correctness(struct gpu *g, struct SwsContext *sws, uint8_t *rgb, AVFrame *out)
{
    AVFrame *ref = frame(g->w, g->h, out->format);
    int maxdiff[3] = {0}, gpu_plain[3] = {0}, sws_plain[3] = {0};
    unsigned long long sums[3] = {0}, counts[3] = {0};
    const uint8_t *src[] = {rgb};
    int stride[] = {g->w * 4};
    for (int kind = 0; kind < 3; kind++) {
        source(g, 17, kind);
        read_target(&g->source, rgb, g->w * 4);
        sws_scale(sws, src, stride, 0, g->h, ref->data, ref->linesize);
        convert_gpu(g, out);
        int localmax[3] = {0};
        for (int p = 0; p < 3; p++) {
            int width = p ? g->w / 2 : g->w, height = p ? g->h / 2 : g->h;
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++) {
                    int plane_index = out->format == AV_PIX_FMT_NV12 && p ? 1 : p;
                    int column = out->format == AV_PIX_FMT_NV12 && p ? x * 2 + p - 1 : x;
                    int a = ref->data[plane_index][y * ref->linesize[plane_index] + column];
                    int b = out->data[plane_index][y * out->linesize[plane_index] + column];
                    int plain = reference(rgb, g->w, g->h, x, y, p);
                    int gp = abs(b - plain), sp = abs(a - plain);
                    if (gp > gpu_plain[p])
                        gpu_plain[p] = gp;
                    if (sp > sws_plain[p])
                        sws_plain[p] = sp;
                    int d = abs(a - b);
                    if (d > localmax[p])
                        localmax[p] = d;
                    if (d > maxdiff[p])
                        maxdiff[p] = d;
                    sums[p] += d;
                    counts[p]++;
                }
        }
        fprintf(stderr, "correctness kind=%d max_Y=%d max_U=%d max_V=%d\n", kind, localmax[0],
                localmax[1], localmax[2]);
    }
    fprintf(stderr,
            "correctness overall max_Y=%d max_U=%d max_V=%d mean_Y=%.6f mean_U=%.6f mean_V=%.6f\n",
            maxdiff[0], maxdiff[1], maxdiff[2], (double)sums[0] / counts[0],
            (double)sums[1] / counts[1], (double)sums[2] / counts[2]);
    fprintf(stderr,
            "plain_reference GPU_max_Y=%d GPU_max_U=%d GPU_max_V=%d "
            "SWS_max_Y=%d SWS_max_U=%d SWS_max_V=%d\n",
            gpu_plain[0], gpu_plain[1], gpu_plain[2], sws_plain[0], sws_plain[1], sws_plain[2]);
    av_frame_free(&ref);
}

static void gpu_close(struct gpu *g)
{
    struct target *targets[] = {&g->source, &g->atlas};
    for (unsigned i = 0; i < sizeof targets / sizeof *targets; i++) {
        glDeleteFramebuffers(1, &targets[i]->fbo);
        glDeleteTextures(1, &targets[i]->tex);
    }
    glDeleteProgram(g->source_program);
    glDeleteProgram(g->convert_program);
    glDeleteVertexArrays(1, &g->vao);
    eglMakeCurrent(g->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(g->display, g->context);
    eglTerminate(g->display);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s WIDTH HEIGHT FRAMES cpu|read-sws|gpu x264|vaapi\n", argv[0]);
        return 2;
    }
    int w = atoi(argv[1]), h = atoi(argv[2]), count = atoi(argv[3]);
    if (w < 8 || w % 8 || h < 2 || h % 2 || count < 1)
        die("invalid dimensions, frames, or mode");
    int mode = !strcmp(argv[4], "cpu")        ? 0
               : !strcmp(argv[4], "read-sws") ? 1
               : !strcmp(argv[4], "gpu")      ? 2
                                              : -1;
    if (mode < 0)
        die("invalid mode");
    int va = !strcmp(argv[5], "vaapi");
    if (!va && strcmp(argv[5], "x264"))
        die("invalid encoder");
    if (getenv("BENCH_SWS_SCALAR"))
        av_force_cpu_flags(0);
    fprintf(stderr, "av_cpu_flags=0x%x\n", av_get_cpu_flags());
    av_log_set_level(AV_LOG_ERROR);
    struct encoder enc = encoder_open(w, h, va);
    AVFrame *out = frame(w, h, va ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P);
    struct SwsContext *sws =
        sws_getContext(w, h, AV_PIX_FMT_RGB0, w, h, out->format, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws)
        die("sws_getContext");
    uint8_t *rgb = malloc((size_t)w * h * 4);
    if (!rgb)
        die("malloc");
    struct gpu gpu = {0};
    if (mode) {
        gpu = gpu_open(w, h);
        if (!getenv("BENCH_SKIP_CHECK")) {
            correctness(&gpu, sws, rgb, out);
            if (va)
                sharing_probe(&gpu, &enc, out);
        }
    }
    struct stamp read = {0}, sws_time = {0};
    struct stamp conv = {0}, upload = {0}, enc_time = {0}, total = {0};
    const int warm = 30;
    for (int i = -warm; i < count; i++) {
        if (mode)
            source(&gpu, i + warm, 0);
        else
            pattern(rgb, w, h, i + warm, 0);
        avcheck(av_frame_make_writable(out), "writable");
        struct stamp t0 = stamp(), t = t0;
        const uint8_t *src[] = {rgb};
        int strides[] = {w * 4};
        if (mode == 2)
            convert_gpu(&gpu, out);
        else {
            if (mode == 1)
                read_target(&gpu.source, rgb, w * 4);
            if (i >= 0)
                add(&read, t);
            struct stamp st = stamp();
            sws_scale(sws, src, strides, 0, h, out->data, out->linesize);
            if (i >= 0)
                add(&sws_time, st);
        }
        if (i >= 0)
            add(&conv, t);
        AVFrame *f = out, *hw = NULL;
        t = stamp();
        if (va) {
            hw = av_frame_alloc();
            avcheck(av_hwframe_get_buffer(enc.frames, hw, 0), "VA buffer");
            avcheck(av_hwframe_transfer_data(hw, out, 0), "VA upload");
            f = hw;
        }
        if (i >= 0)
            add(&upload, t);
        t = stamp();
        encode(&enc, f, i + warm);
        av_frame_free(&hw);
        if (i >= 0) {
            add(&enc_time, t);
            add(&total, t0);
        }
    }
    printf("width,height,frames,mode,encoder,convert_wall_ms,convert_cpu_ms,upload_wall_ms,"
           "upload_cpu_ms,encode_wall_ms,encode_cpu_ms,total_wall_ms,total_cpu_ms,fps,packets,"
           "bytes,read_wall_ms,read_cpu_ms,sws_wall_ms,sws_cpu_ms\n");
    double ms = 1000.0 / count;
    printf("%d,%d,%d,%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%llu,%llu,%.6f,%.6f,%.6f,%."
           "6f\n",
           w, h, count, argv[4], argv[5], conv.wall * ms, conv.cpu * ms, upload.wall * ms,
           upload.cpu * ms, enc_time.wall * ms, enc_time.cpu * ms, total.wall * ms, total.cpu * ms,
           count / total.wall, enc.packets, enc.bytes, read.wall * ms, read.cpu * ms,
           sws_time.wall * ms, sws_time.cpu * ms);
    if (mode)
        gpu_close(&gpu);
    free(rgb);
    sws_freeContext(sws);
    av_frame_free(&out);
    encoder_close(&enc);
    return 0;
}
