// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>

unsigned input_evdev_qnum(unsigned evdev)
{
    /* Linux evdev codes -> QEMU number (XT set 1, E0 represented by bit 7). */
    static const unsigned char codes[KEY_MAX + 1] = {
        [KEY_A] = 0x1e,
        [KEY_B] = 0x30,
        [KEY_C] = 0x2e,
        [KEY_D] = 0x20,
        [KEY_E] = 0x12,
        [KEY_F] = 0x21,
        [KEY_G] = 0x22,
        [KEY_H] = 0x23,
        [KEY_I] = 0x17,
        [KEY_J] = 0x24,
        [KEY_K] = 0x25,
        [KEY_L] = 0x26,
        [KEY_M] = 0x32,
        [KEY_N] = 0x31,
        [KEY_O] = 0x18,
        [KEY_P] = 0x19,
        [KEY_Q] = 0x10,
        [KEY_R] = 0x13,
        [KEY_S] = 0x1f,
        [KEY_T] = 0x14,
        [KEY_U] = 0x16,
        [KEY_V] = 0x2f,
        [KEY_W] = 0x11,
        [KEY_X] = 0x2d,
        [KEY_Y] = 0x15,
        [KEY_Z] = 0x2c,
        [KEY_1] = 2,
        [KEY_2] = 3,
        [KEY_3] = 4,
        [KEY_4] = 5,
        [KEY_5] = 6,
        [KEY_6] = 7,
        [KEY_7] = 8,
        [KEY_8] = 9,
        [KEY_9] = 10,
        [KEY_0] = 11,
        [KEY_ENTER] = 0x1c,
        [KEY_BACKSPACE] = 0x0e,
        [KEY_TAB] = 0x0f,
        [KEY_SPACE] = 0x39,
        [KEY_MINUS] = 0x0c,
        [KEY_EQUAL] = 0x0d,
        [KEY_LEFTBRACE] = 0x1a,
        [KEY_RIGHTBRACE] = 0x1b,
        [KEY_BACKSLASH] = 0x2b,
        [KEY_SEMICOLON] = 0x27,
        [KEY_APOSTROPHE] = 0x28,
        [KEY_GRAVE] = 0x29,
        [KEY_COMMA] = 0x33,
        [KEY_DOT] = 0x34,
        [KEY_SLASH] = 0x35,
        [KEY_CAPSLOCK] = 0x3a,
        [KEY_F1] = 0x3b,
        [KEY_F2] = 0x3c,
        [KEY_F3] = 0x3d,
        [KEY_F4] = 0x3e,
        [KEY_F5] = 0x3f,
        [KEY_F6] = 0x40,
        [KEY_F7] = 0x41,
        [KEY_F8] = 0x42,
        [KEY_F9] = 0x43,
        [KEY_F10] = 0x44,
        [KEY_F11] = 0x57,
        [KEY_F12] = 0x58,
        [KEY_INSERT] = 0xd2,
        [KEY_DELETE] = 0xd3,
        [KEY_END] = 0xcf,
        [KEY_PAGEUP] = 0xc9,
        [KEY_PAGEDOWN] = 0xd1,
        [KEY_UP] = 0xc8,
        [KEY_DOWN] = 0xd0,
        [KEY_LEFT] = 0xcb,
        [KEY_RIGHT] = 0xcd,
        [KEY_LEFTCTRL] = 0x1d,
        [KEY_RIGHTCTRL] = 0x9d,
        [KEY_LEFTSHIFT] = 0x2a,
        [KEY_RIGHTSHIFT] = 0x36,
        [KEY_LEFTALT] = 0x38,
        [KEY_RIGHTALT] = 0xb8,
        [KEY_KPENTER] = 0x9c,
        /* Home, Super, Escape and the volume keys are the remote's (core.c, wv_key_evdev) */
    };
    return evdev <= KEY_MAX ? codes[evdev] : 0;
}
static void done(GObject *source, GAsyncResult *result, gpointer data)
{
    struct input *in = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (error) {
        fprintf(stderr, "input: %s\n", error->message);
        in->errors++;
        g_error_free(error);
    }
    if (reply)
        g_variant_unref(reply);
    in->pending--;
}
static void send(struct input *in, const char *iface, const char *method, GVariant *args)
{
    if (!in->bus) {
        g_variant_unref(g_variant_ref_sink(args));
        return;
    }
    in->pending++;
    in->calls++;
    g_dbus_connection_call(in->bus, "org.qemu", "/org/qemu/Display1/Console_0", iface, method, args,
                           NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, done, in);
}
static void key(struct input *in, unsigned code, int down)
{
    send(in, "org.qemu.Display1.Keyboard", down ? "Press" : "Release", g_variant_new("(u)", code));
}
void input_key(struct input *in, unsigned qnum, int down)
{
    if (!qnum || qnum > 255)
        return;
    if (down && !in->held[qnum]) {
        key(in, qnum, 1);
        in->held[qnum] = 1;
    } else if (!down && in->held[qnum]) {
        key(in, qnum, 0);
        in->held[qnum] = 0;
    }
}
static void button(struct input *in, unsigned code, int down)
{
    send(in, "org.qemu.Display1.Mouse", down ? "Press" : "Release", g_variant_new("(u)", code));
}
void input_release(struct input *in)
{
    for (unsigned i = 0; i < 256; i++)
        if (in->held[i]) {
            key(in, i, 0);
            in->held[i] = 0;
        }
    for (unsigned i = 0; i < 3; i++)
        if (in->buttons & (1u << i))
            button(in, i, 0);
    in->buttons = 0;
    input_touch_release(in);
}
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
int input_position(struct input *in, int x, int y)
{
    struct rect v = in->viewport;
    if (!in->width || !in->height || v.w < 2 || v.h < 2)
        return 0;
    if (!in->buttons && (x < v.x || y < v.y || x >= v.x + v.w || y >= v.y + v.h))
        return 0;
    x = clamp(x - v.x, 0, v.w - 1);
    y = clamp(y - v.y, 0, v.h - 1);
    send(in, "org.qemu.Display1.Mouse", "SetAbsPosition",
         g_variant_new("(uu)", (unsigned)((gint64)x * (in->width - 1) / (v.w - 1)),
                       (unsigned)((gint64)y * (in->height - 1) / (v.h - 1))));
    return 1;
}
void input_abs(struct input *in, unsigned x, unsigned y)
{
    send(in, "org.qemu.Display1.Mouse", "SetAbsPosition", g_variant_new("(uu)", x, y));
}
void input_button_edge(struct input *in, unsigned code, int down)
{
    if (code > 2 || !down == !(in->buttons & (1u << code)))
        return;
    button(in, code, down);
    if (down)
        in->buttons |= 1u << code;
    else
        in->buttons &= ~(1u << code);
}
void input_button(struct input *in, unsigned code, int down, int x, int y)
{
    if (code > 2)
        return;
    if (down) {
        if (!input_position(in, x, y))
            return;
        if (!(in->buttons & (1u << code)))
            button(in, code, 1);
        in->buttons |= 1u << code;
    } else if (in->buttons & (1u << code)) {
        input_position(in, x, y);
        button(in, code, 0);
        in->buttons &= ~(1u << code);
    }
}
void input_wheel(struct input *in, int steps)
{
    steps = clamp(steps, -32, 32);
    unsigned code = steps > 0 ? 3 : 4;
    for (int i = 0; i < abs(steps); i++) {
        button(in, code, 1);
        button(in, code, 0);
    }
}
int input_touch_abs(struct input *in, unsigned kind, unsigned slot, unsigned x, unsigned y)
{
    if (slot >= INPUT_TOUCH_SLOTS || kind > INPUT_TOUCH_CANCEL)
        return 0;
    int down = !!(in->touches & (1u << slot));
    if (kind == INPUT_TOUCH_BEGIN ? down : !down)
        return 0;
    /* QEMU's console takes a cancel as a move (console_handle_touch_event): it would leave the
     * contact down in the guest */
    if (kind == INPUT_TOUCH_CANCEL)
        kind = INPUT_TOUCH_END;
    send(in, "org.qemu.Display1.MultiTouch", "SendEvent",
         g_variant_new("(utdd)", kind, (guint64)slot, (double)x, (double)y));
    in->touch_at[slot][0] = x;
    in->touch_at[slot][1] = y;
    if (kind == INPUT_TOUCH_BEGIN)
        in->touches |= 1u << slot;
    else if (kind == INPUT_TOUCH_END)
        in->touches &= ~(1u << slot);
    return 1;
}
int input_touch(struct input *in, unsigned kind, unsigned slot, int x, int y)
{
    struct rect v = in->viewport;
    if (!in->width || !in->height || v.w < 2 || v.h < 2)
        return 0;
    if (kind == INPUT_TOUCH_BEGIN && (x < v.x || y < v.y || x >= v.x + v.w || y >= v.y + v.h))
        return 0;
    x = clamp(x - v.x, 0, v.w - 1);
    y = clamp(y - v.y, 0, v.h - 1);
    return input_touch_abs(in, kind, slot, (unsigned)((gint64)x * (in->width - 1) / (v.w - 1)),
                           (unsigned)((gint64)y * (in->height - 1) / (v.h - 1)));
}
void input_touch_release(struct input *in)
{
    for (unsigned i = 0; i < INPUT_TOUCH_SLOTS; i++)
        if (in->touches & (1u << i))
            input_touch_abs(in, INPUT_TOUCH_END, i, in->touch_at[i][0], in->touch_at[i][1]);
}
