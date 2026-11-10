// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's keysym map (host/vnc/keysym.c): the keys a VNC client sends as keysyms reach the
 * core as the PC keys the GTK viewer would send (the remote's own: Super, Home, Escape, volume). */
#include "../../../host/vnc/keysym.h"
#include <linux/input-event-codes.h>
#include <stdio.h>

int main(void)
{
    static const struct {
        unsigned keysym, evdev;
    } cases[] = {
        {'a', KEY_A},
        {'A', KEY_A},
        {'z', KEY_Z},
        {'0', KEY_0},
        {')', KEY_0},
        {' ', KEY_SPACE},
        {'?', KEY_SLASH},
        {0xff0d, KEY_ENTER},
        {0xff1b, KEY_ESC},
        {0xff08, KEY_BACKSPACE},
        {0xff50, KEY_HOME},
        {0xff51, KEY_LEFT},
        {0xff54, KEY_DOWN},
        {0xffeb, KEY_LEFTMETA},
        {0xffec, KEY_RIGHTMETA},
        {0xffe1, KEY_LEFTSHIFT},
        {0xffbe, KEY_F1},
        {0xffc9, KEY_F12},
        {0x1008ff13, KEY_VOLUMEUP},
        {0x1008ff11, KEY_VOLUMEDOWN},
        {0x1008ff12, KEY_MUTE},
        {0xffb5, KEY_KP5},
        {0x20ac, 0} /* EuroSign */,
        {0, 0},
    };
    int failed = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        unsigned got = keysym_evdev(cases[i].keysym);
        if (got != cases[i].evdev) {
            fprintf(stderr, "keysym 0x%x: evdev %u, expected %u\n", cases[i].keysym, got,
                    cases[i].evdev);
            failed = 1;
        }
    }
    return failed;
}
