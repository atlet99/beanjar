// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_INPUT_H
#define VIEWER_INPUT_H
#include <gio/gio.h>
/* A rectangle in pixels, top-left origin unless a caller says otherwise. */
struct rect {
    int x, y, w, h;
};
/* Keyboard and absolute pointer input to QEMU's Console_0 (org.qemu.Display1.Keyboard/Mouse),
 * independent of the toolkit: the GTK front end (beanjar_ui.viewer) and the headless SDL one
 * translate their events into these calls. Calls are asynchronous; completion errors are
 * reported. */
#define INPUT_TOUCH_SLOTS 10 /* QEMU's INPUT_EVENT_SLOTS_MAX, and LG libim's */
struct input {
    GDBusConnection *bus;
    struct rect viewport;    /* the picture in logical window coordinates, top-left origin */
    int width, height;       /* the guest scanout */
    unsigned char held[256]; /* QEMU key numbers currently pressed */
    unsigned buttons;
    unsigned touches; /* touch slots down (bit per slot) */
    /* where each was last, in display pixels: its end is sent there */
    unsigned touch_at[INPUT_TOUCH_SLOTS][2];
    unsigned pending, errors, calls;
};
/* Press or release a QEMU key number (XT set 1, E0 as bit 7). Presses of a key already held
 * (autorepeat) and releases of a key not held are dropped. Nothing without a display connection. */
void input_key(struct input *in, unsigned qnum, int down);
/* The PC keyboard: a Linux evdev key code to the QEMU number the viewer sends for it, 0 if none.
 * Home, the GUI keys, Escape and the volume keys have none: they are the remote's buttons, sent
 * as IR (core.c, wv_key_evdev). */
unsigned input_evdev_qnum(unsigned evdev);
/* Pointer at logical window x, y: sends the tablet position when inside the picture, or clamped
 * to its edge while a button is held. Returns 1 if a position was sent. */
int input_position(struct input *in, int x, int y);
/* The tablet at x, y in the guest's display (scanout) pixels, as QEMU takes them: no viewport. */
void input_abs(struct input *in, unsigned x, unsigned y);
/* A button (0 left, 1 middle, 2 right) to down or up where the tablet is; nothing if it already is.
 */
void input_button_edge(struct input *in, unsigned button, int down);
/* button 0 left, 1 middle, 2 right. A press outside the picture is ignored. */
void input_button(struct input *in, unsigned button, int down, int x, int y);
/* Wheel notches, positive up. */
void input_wheel(struct input *in, int steps);
/* Touch (org.qemu.Display1.MultiTouch, the guest's touch screen where it has one): a contact of a
 * slot (0 to INPUT_TOUCH_SLOTS - 1) begins, moves or ends; a cancel ends it too. QEMU's kinds. */
enum { INPUT_TOUCH_BEGIN, INPUT_TOUCH_UPDATE, INPUT_TOUCH_END, INPUT_TOUCH_CANCEL };
/* At x, y in the guest's display (scanout) pixels. An update or end of a slot that is not down is
 * dropped, and so is a begin of one that is. Returns 1 if it was sent. */
int input_touch_abs(struct input *in, unsigned kind, unsigned slot, unsigned x, unsigned y);
/* At logical window x, y: a begin outside the picture is dropped; the rest clamped to its edge. */
int input_touch(struct input *in, unsigned kind, unsigned slot, int x, int y);
/* Ends every touch that is down where it is. */
void input_touch_release(struct input *in);
/* Releases every held key, button and touch (focus loss, exit). */
void input_release(struct input *in);
#endif
