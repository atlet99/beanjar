// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The TEE as the firmware's own clients reach it (scripts/regress --tv, the tee step;
 * tests/regress/tv.sh tee): the GlobalPlatform TEE Client API of the firmware's libkadaptor (its
 * OP-TEE libteec, loaded at run time), against rtk.ko's /dev/tee0 and its stand-in TAs
 * (guest/kernel/modules/rtk/tee-ta.c). tee-probe LIB open UUID...    InitializeContext, OpenSession
 * of each UUID, CloseSession, FinalizeContext: open.UUID=RESULT ORIGIN tee-probe LIB sstore UUID
 * the secure-store TA's commands as libkadaptor's x* helpers call them (tee-ta.c), each in a
 * session of its own: sstore.NAME=RESULT and its outputs in hex; the round trips' outcome as
 *                                 sstore.NAME.roundtrip=ok|mismatch
 * A UUID is OP-TEE's TA name, 8-4-4-4+12 or 8-4-4-4-12 hex digits. One "name=value" a line, judged
 * on the host (tests/regress/lib/tvsuite.py judge_tee). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tee_client_api.h's types, 32-bit (the census: a 60-byte TEEC_Operation) */
typedef struct {
    uint32_t timeLow;
    uint16_t timeMid, timeHiAndVersion;
    uint8_t clockSeqAndNode[8];
} TEEC_UUID;
typedef struct {
    uint8_t opaque[64];
} TEEC_Context; /* the library's: fd, reg_mem, ... */
typedef struct {
    uint8_t opaque[64];
} TEEC_Session; /* ctx, session_id */
typedef union {
    struct {
        void *buffer;
        size_t size;
    } tmpref;
    struct {
        void *parent;
        size_t size, offset;
    } memref;
    struct {
        uint32_t a, b;
    } value;
} TEEC_Parameter;
typedef struct {
    uint32_t started, paramTypes;
    TEEC_Parameter params[4];
    TEEC_Session *session;
} TEEC_Operation;

#define NONE              0
#define VALUE_IN          1
#define VALUE_OUT         2
#define VALUE_INOUT       3
#define TMP_IN            5
#define TMP_OUT           6
#define TMP_INOUT         7
#define TYPES(a, b, c, d) ((a) | (b) << 4 | (c) << 8 | (d) << 12)

static uint32_t (*InitializeContext)(const char *, TEEC_Context *);
static void (*FinalizeContext)(TEEC_Context *);
static uint32_t (*OpenSession)(TEEC_Context *, TEEC_Session *, const TEEC_UUID *, uint32_t,
                               const void *, TEEC_Operation *, uint32_t *);
static void (*CloseSession)(TEEC_Session *);
static uint32_t (*InvokeCommand)(TEEC_Session *, uint32_t, TEEC_Operation *, uint32_t *);

static int parse_uuid(const char *s, TEEC_UUID *u)
{
    uint8_t b[16];
    int n = 0;

    for (; *s && n < 32; s++) {
        int v;

        if (*s == '-')
            continue;
        if (*s >= '0' && *s <= '9')
            v = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            v = *s - 'A' + 10;
        else
            return -1;
        if (n % 2)
            b[n / 2] |= v;
        else
            b[n / 2] = v << 4;
        n++;
    }
    if (n != 32 || *s)
        return -1;
    u->timeLow = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    u->timeMid = b[4] << 8 | b[5];
    u->timeHiAndVersion = b[6] << 8 | b[7];
    memcpy(u->clockSeqAndNode, b + 8, 8);
    return 0;
}

static void hex(const char *name, const void *p, size_t n)
{
    printf("%s=", name);
    for (size_t i = 0; i < n; i++)
        printf("%02x", ((const uint8_t *)p)[i]);
    printf("\n");
}

static int open_all(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        TEEC_Context ctx;
        TEEC_Session sess;
        TEEC_UUID u;
        uint32_t res, origin = 0;

        if (parse_uuid(argv[i], &u)) {
            printf("open.%s=bad-uuid\n", argv[i]);
            continue;
        }
        memset(&ctx, 0, sizeof(ctx));
        res = InitializeContext(NULL, &ctx);
        if (res) {
            printf("open.%s=init:%#x\n", argv[i], res);
            continue;
        }
        memset(&sess, 0, sizeof(sess));
        res = OpenSession(&ctx, &sess, &u, 0, NULL, NULL, &origin);
        printf("open.%s=%#x %u\n", argv[i], res, origin);
        if (!res)
            CloseSession(&sess);
        FinalizeContext(&ctx);
    }
    return 0;
}

/* ---- the secure-store TA ---- */

static TEEC_UUID sstore_uuid;

/* one command in a session of its own, as libkadaptor's x* helpers do */
static uint32_t call(uint32_t cmd, TEEC_Operation *op)
{
    TEEC_Context ctx;
    TEEC_Session sess;
    uint32_t res, origin = 0;

    memset(&ctx, 0, sizeof(ctx));
    res = InitializeContext(NULL, &ctx);
    if (res)
        return res;
    memset(&sess, 0, sizeof(sess));
    res = OpenSession(&ctx, &sess, &sstore_uuid, 0, NULL, NULL, &origin);
    if (!res) {
        op->started = 1;
        res = InvokeCommand(&sess, cmd, op, &origin);
        CloseSession(&sess);
    }
    FinalizeContext(&ctx);
    return res;
}

static void tmp(TEEC_Operation *op, int i, void *buf, size_t size)
{
    op->params[i].tmpref.buffer = buf;
    op->params[i].tmpref.size = size;
}

#define NOT_SUPPORTED 0xffff000a
#define MAC_INVALID   0xffff3071
#define IMAGE_SIZE    4096
#define SLOT          0x1c

static int fails;

/* a round trip's outcome */
static void trip(const char *name, int ok)
{
    printf("sstore.%s.roundtrip=%s\n", name, ok ? "ok" : "mismatch");
    fails += !ok;
}

/* the 20-byte header of Encryption/Decryption: key id, mode, the inputs' lengths, 16 or 0 */
static void header(uint32_t *h, uint32_t id, uint32_t mode, uint32_t len)
{
    h[0] = id;
    h[1] = mode;
    h[2] = len;
    h[3] = 0;
    h[4] = 0;
}

/* MakeKey (0x201: p0 data, p1 out) or MakeClearKey (0x203: p0 type, p1 data, p2 out), then the
 * length and the data back (0x205/0x202 or 0x206/0x204) and an altered blob refused */
static void key_trip(const char *name, int clear, const char *data)
{
    TEEC_Operation op;
    uint8_t blob[512], back[512];
    size_t n = strlen(data);
    uint32_t res;
    char what[64];

    if (!clear) {
        /* KADP_SStore_SetKey's length probe: no buffer, size 0 */
        memset(&op, 0, sizeof(op));
        op.paramTypes = TYPES(TMP_IN, TMP_OUT, NONE, NONE);
        tmp(&op, 0, (void *)data, n);
        tmp(&op, 1, NULL, 0);
        res = call(0x201, &op);
        printf("sstore.%s.probe=%#x %zu\n", name, res, op.params[1].tmpref.size);
    }
    memset(&op, 0, sizeof(op));
    if (clear) {
        op.paramTypes = TYPES(TMP_IN, TMP_IN, TMP_OUT, NONE);
        tmp(&op, 0, "general", 7);
        tmp(&op, 1, (void *)data, n);
        tmp(&op, 2, blob, n + 176);
    } else {
        op.paramTypes = TYPES(TMP_IN, TMP_OUT, NONE, NONE);
        tmp(&op, 0, (void *)data, n);
        tmp(&op, 1, blob, sizeof(blob));
    }
    res = call(clear ? 0x203 : 0x201, &op);
    n = op.params[clear ? 2 : 1].tmpref.size;
    printf("sstore.%s=%#x %zu\n", name, res, n);
    if (res || n > sizeof(blob)) {
        trip(name, 0);
        return;
    }
    snprintf(what, sizeof(what), "sstore.%s.blob", name);
    hex(what, blob, n);

    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, VALUE_INOUT, NONE, NONE);
    tmp(&op, 0, blob, n);
    res = call(clear ? 0x206 : 0x205, &op);
    printf("sstore.%s.len=%#x %u\n", name, res, op.params[1].value.a);

    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_OUT, NONE, NONE);
    tmp(&op, 0, blob, n);
    tmp(&op, 1, back, sizeof(back));
    res = call(clear ? 0x204 : 0x202, &op);
    printf("sstore.%s.get=%#x %zu\n", name, res, op.params[1].tmpref.size);
    trip(name,
         !res && op.params[1].tmpref.size == strlen(data) && !memcmp(back, data, strlen(data)));

    blob[n / 2] ^= 1;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_OUT, NONE, NONE);
    tmp(&op, 0, blob, n);
    tmp(&op, 1, back, sizeof(back));
    res = call(clear ? 0x204 : 0x202, &op);
    printf("sstore.%s.altered=%#x\n", name, res);
}

/* 0x2 then 0x1 (plain), or 0x20 then 0x40 (the container: the data after its 32 bytes) */
static void cipher_trip(const char *name, int box)
{
    TEEC_Operation op;
    uint32_t h[5];
    const char *inputs = "tee-probe inputs";
    uint8_t data[72], enc[72], dec[72];
    size_t n = sizeof(data);
    uint32_t res;
    char what[64];

    for (size_t i = 0; i < n; i++)
        data[i] = box && i < 32 ? 0 : (uint8_t)(i * 13 + 1);
    header(h, 1, 12, strlen(inputs));
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_IN, TMP_IN, TMP_OUT);
    tmp(&op, 0, h, sizeof(h));
    tmp(&op, 1, (void *)inputs, strlen(inputs));
    tmp(&op, 2, data, n);
    tmp(&op, 3, enc, n);
    res = call(box ? 0x20 : 0x2, &op);
    printf("sstore.%s.encrypt=%#x %zu\n", name, res, op.params[3].tmpref.size);
    snprintf(what, sizeof(what), "sstore.%s.data", name);
    hex(what, data, n);
    snprintf(what, sizeof(what), "sstore.%s.enc", name);
    hex(what, enc, n);

    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_IN, TMP_IN, TMP_OUT);
    tmp(&op, 0, h, sizeof(h));
    tmp(&op, 1, (void *)inputs, strlen(inputs));
    tmp(&op, 2, enc, n);
    tmp(&op, 3, dec, n);
    res = call(box ? 0x40 : 0x1, &op);
    printf("sstore.%s.decrypt=%#x %zu\n", name, res, op.params[3].tmpref.size);
    trip(name, !res && !memcmp(dec + (box ? 32 : 0), data + (box ? 32 : 0), n - (box ? 32 : 0)));
    if (box) {
        enc[40] ^= 1;
        memset(&op, 0, sizeof(op));
        op.paramTypes = TYPES(TMP_IN, TMP_IN, TMP_IN, TMP_OUT);
        tmp(&op, 0, h, sizeof(h));
        tmp(&op, 1, (void *)inputs, strlen(inputs));
        tmp(&op, 2, enc, n);
        tmp(&op, 3, dec, n);
        printf("sstore.%s.altered=%#x\n", name, call(0x40, &op));
    }
}

/* 0x8, the store image's MAC into its slot, then 0x10 on it, and on it altered */
static void store_trip(void)
{
    static uint8_t image[IMAGE_SIZE];
    TEEC_Operation op;
    uint8_t mac[32];
    uint32_t res;

    for (size_t i = 0; i < sizeof(image); i++)
        image[i] = (uint8_t)(i * 7 + 3);
    image[0] = 'S';
    image[1] = 'E';
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_OUT, NONE, NONE);
    tmp(&op, 0, image, sizeof(image));
    tmp(&op, 1, mac, sizeof(mac));
    res = call(0x8, &op);
    printf("sstore.storemac=%#x %zu\n", res, op.params[1].tmpref.size);
    hex("sstore.storemac.mac", mac, sizeof(mac));
    memcpy(image + SLOT, mac, sizeof(mac));
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, NONE, NONE, NONE);
    tmp(&op, 0, image, sizeof(image));
    res = call(0x10, &op);
    printf("sstore.storecheck=%#x\n", res);
    trip("store", !res);
    image[100] ^= 1;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, NONE, NONE, NONE);
    tmp(&op, 0, image, sizeof(image));
    printf("sstore.storecheck.altered=%#x\n", call(0x10, &op));
}

/* 0x80, KADP_SStore_GetHMAC's (sub-op 4), then 0x100 on it and on another */
static void hmac_trip(void)
{
    TEEC_Operation op;
    const char *data = "tee-probe hmac data";
    uint8_t p2[32], tag[32];
    uint32_t res;

    memset(p2, 0x5a, sizeof(p2));
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(VALUE_IN, TMP_IN, TMP_INOUT, TMP_OUT);
    op.params[0].value.a = 4;
    tmp(&op, 1, (void *)data, strlen(data));
    tmp(&op, 2, p2, sizeof(p2));
    tmp(&op, 3, tag, sizeof(tag));
    res = call(0x80, &op);
    printf("sstore.hmac=%#x %zu\n", res, op.params[3].tmpref.size);
    hex("sstore.hmac.tag", tag, sizeof(tag));
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(VALUE_IN, TMP_IN, TMP_IN, TMP_IN);
    op.params[0].value.a = 4;
    tmp(&op, 1, (void *)data, strlen(data));
    tmp(&op, 2, p2, sizeof(p2));
    tmp(&op, 3, tag, sizeof(tag));
    res = call(0x100, &op);
    printf("sstore.hmaccheck=%#x\n", res);
    trip("hmac", !res);
    tag[0] ^= 1;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(VALUE_IN, TMP_IN, TMP_IN, TMP_IN);
    op.params[0].value.a = 4;
    tmp(&op, 1, (void *)data, strlen(data));
    tmp(&op, 2, p2, sizeof(p2));
    tmp(&op, 3, tag, sizeof(tag));
    printf("sstore.hmaccheck.altered=%#x\n", call(0x100, &op));
}

static void misc(void)
{
    TEEC_Operation op;
    uint8_t table[0x3c], in[64], out[64], blob[16 + 4];
    const char *data = "tee-probe new";
    uint32_t res;
    static const uint32_t unsupported[] = {0x1619, 0x400, 0x401, 0x10000};

    memset(&op, 0, sizeof(op));
    op.paramTypes = 0;
    printf("sstore.checkvalid=%#x\n", call(0x200, &op));

    /* xReEncyptSstoreInInit: the record table, the store in and out */
    memset(table, 0, sizeof(table));
    for (size_t i = 0; i < sizeof(in); i++)
        in[i] = (uint8_t)(255 - i);
    memset(out, 0, sizeof(out));
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(TMP_IN, TMP_IN, TMP_OUT, NONE);
    tmp(&op, 0, table, sizeof(table));
    tmp(&op, 1, in, sizeof(in));
    tmp(&op, 2, out, sizeof(out));
    res = call(0x4, &op);
    printf("sstore.reencrypt=%#x %zu\n", res, op.params[2].tmpref.size);
    trip("reencrypt", !res && !memcmp(in, out, sizeof(in)));

    /* xMakeNewSecureData: value 7, the data, its length + 4 back */
    memset(&op, 0, sizeof(op));
    op.paramTypes = TYPES(VALUE_IN, TMP_IN, TMP_OUT, NONE);
    op.params[0].value.a = 7;
    tmp(&op, 1, (void *)data, strlen(data));
    tmp(&op, 2, blob, strlen(data) + 4);
    res = call(0x800, &op);
    printf("sstore.newdata=%#x %zu\n", res, op.params[2].tmpref.size);
    hex("sstore.newdata.out", blob, strlen(data) + 4);

    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        memset(&op, 0, sizeof(op));
        op.paramTypes = 0;
        printf("sstore.unsupported.%#x=%#x\n", unsupported[i], call(unsupported[i], &op));
    }
}

static int sstore(void)
{
    misc();
    key_trip("clearkey", 1, "tee-probe clear key");
    key_trip("key", 0, "tee-probe protected key");
    cipher_trip("cipher", 0);
    cipher_trip("box", 1);
    store_trip();
    hmac_trip();
    printf("sstore.fails=%d\n", fails);
    return fails != 0;
}

int main(int argc, char **argv)
{
    void *lib;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 3) {
        fprintf(stderr, "usage: tee-probe LIB open UUID... | tee-probe LIB sstore UUID\n");
        return 2;
    }
    lib = dlopen(argv[1], RTLD_LAZY | RTLD_GLOBAL);
    if (!lib) {
        printf("lib=%s\n", dlerror());
        return 1;
    }
    InitializeContext = dlsym(lib, "TEEC_InitializeContext");
    FinalizeContext = dlsym(lib, "TEEC_FinalizeContext");
    OpenSession = dlsym(lib, "TEEC_OpenSession");
    CloseSession = dlsym(lib, "TEEC_CloseSession");
    InvokeCommand = dlsym(lib, "TEEC_InvokeCommand");
    if (!InitializeContext || !FinalizeContext || !OpenSession || !CloseSession || !InvokeCommand) {
        printf("lib=no TEEC_* in %s\n", argv[1]);
        return 1;
    }
    printf("lib=%s\n", argv[1]);
    if (!strcmp(argv[2], "open"))
        return open_all(argc - 3, argv + 3);
    if (!strcmp(argv[2], "sstore") && argc == 4) {
        if (parse_uuid(argv[3], &sstore_uuid)) {
            printf("sstore=bad-uuid\n");
            return 1;
        }
        return sstore();
    }
    fprintf(stderr, "tee-probe: unknown %s\n", argv[2]);
    return 2;
}
