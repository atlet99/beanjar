// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* X keysyms -> Linux evdev codes (keysym.h): what a US PC keyboard sends for each keysym. The
 * shifted symbols map to their key; the client sends Shift itself. */
#include "keysym.h"
#include <linux/input-event-codes.h>
#include <stddef.h>

static const struct {
    unsigned short keysym, evdev;
} table[] = {
    /* printable ASCII: unshifted, then shifted */
    {'1', KEY_1},
    {'2', KEY_2},
    {'3', KEY_3},
    {'4', KEY_4},
    {'5', KEY_5},
    {'6', KEY_6},
    {'7', KEY_7},
    {'8', KEY_8},
    {'9', KEY_9},
    {'0', KEY_0},
    {'!', KEY_1},
    {'@', KEY_2},
    {'#', KEY_3},
    {'$', KEY_4},
    {'%', KEY_5},
    {'^', KEY_6},
    {'&', KEY_7},
    {'*', KEY_8},
    {'(', KEY_9},
    {')', KEY_0},
    {' ', KEY_SPACE},
    {'-', KEY_MINUS},
    {'_', KEY_MINUS},
    {'=', KEY_EQUAL},
    {'+', KEY_EQUAL},
    {'[', KEY_LEFTBRACE},
    {'{', KEY_LEFTBRACE},
    {']', KEY_RIGHTBRACE},
    {'}', KEY_RIGHTBRACE},
    {'\\', KEY_BACKSLASH},
    {'|', KEY_BACKSLASH},
    {';', KEY_SEMICOLON},
    {':', KEY_SEMICOLON},
    {'\'', KEY_APOSTROPHE},
    {'"', KEY_APOSTROPHE},
    {'`', KEY_GRAVE},
    {'~', KEY_GRAVE},
    {',', KEY_COMMA},
    {'<', KEY_COMMA},
    {'.', KEY_DOT},
    {'>', KEY_DOT},
    {'/', KEY_SLASH},
    {'?', KEY_SLASH},
    /* TTY functions, cursor and editing (0xff..) */
    {0xff08, KEY_BACKSPACE},
    {0xff09, KEY_TAB},
    {0xfe20, KEY_TAB} /* ISO_Left_Tab */,
    {0xff0d, KEY_ENTER},
    {0xff13, KEY_PAUSE},
    {0xff14, KEY_SCROLLLOCK},
    {0xff15, KEY_SYSRQ},
    {0xff1b, KEY_ESC},
    {0xffff, KEY_DELETE},
    {0xff50, KEY_HOME},
    {0xff51, KEY_LEFT},
    {0xff52, KEY_UP},
    {0xff53, KEY_RIGHT},
    {0xff54, KEY_DOWN},
    {0xff55, KEY_PAGEUP},
    {0xff56, KEY_PAGEDOWN},
    {0xff57, KEY_END},
    {0xff61, KEY_SYSRQ} /* Print */,
    {0xff63, KEY_INSERT},
    {0xff67, KEY_COMPOSE} /* Menu */,
    {0xff7f, KEY_NUMLOCK},
    /* keypad */
    {0xff8d, KEY_KPENTER},
    {0xffaa, KEY_KPASTERISK},
    {0xffab, KEY_KPPLUS},
    {0xffad, KEY_KPMINUS},
    {0xffae, KEY_KPDOT},
    {0xffaf, KEY_KPSLASH},
    {0xffb0, KEY_KP0},
    {0xffb1, KEY_KP1},
    {0xffb2, KEY_KP2},
    {0xffb3, KEY_KP3},
    {0xffb4, KEY_KP4},
    {0xffb5, KEY_KP5},
    {0xffb6, KEY_KP6},
    {0xffb7, KEY_KP7},
    {0xffb8, KEY_KP8},
    {0xffb9, KEY_KP9},
    {0xff95, KEY_KP7},
    {0xff96, KEY_KP4},
    {0xff97, KEY_KP8},
    {0xff98, KEY_KP6},
    {0xff99, KEY_KP2},
    {0xff9a, KEY_KP9},
    {0xff9b, KEY_KP3},
    {0xff9c, KEY_KP1},
    {0xff9d, KEY_KP5},
    {0xff9e, KEY_KP0},
    {0xff9f, KEY_KPDOT},
    /* function keys */
    {0xffbe, KEY_F1},
    {0xffbf, KEY_F2},
    {0xffc0, KEY_F3},
    {0xffc1, KEY_F4},
    {0xffc2, KEY_F5},
    {0xffc3, KEY_F6},
    {0xffc4, KEY_F7},
    {0xffc5, KEY_F8},
    {0xffc6, KEY_F9},
    {0xffc7, KEY_F10},
    {0xffc8, KEY_F11},
    {0xffc9, KEY_F12},
    /* modifiers; Super (and Meta, which some clients send for it) is the remote's Home */
    {0xffe1, KEY_LEFTSHIFT},
    {0xffe2, KEY_RIGHTSHIFT},
    {0xffe3, KEY_LEFTCTRL},
    {0xffe4, KEY_RIGHTCTRL},
    {0xffe5, KEY_CAPSLOCK},
    {0xffe7, KEY_LEFTMETA},
    {0xffe8, KEY_RIGHTMETA},
    {0xffe9, KEY_LEFTALT},
    {0xffea, KEY_RIGHTALT},
    {0xffeb, KEY_LEFTMETA},
    {0xffec, KEY_RIGHTMETA},
    {0xfe03, KEY_RIGHTALT} /* ISO_Level3_Shift */,
};

/* XF86 media keys (0x1008ff..), outside the table's 16 bits */
static unsigned xf86(unsigned keysym)
{
    switch (keysym) {
    case 0x1008ff11:
        return KEY_VOLUMEDOWN;
    case 0x1008ff12:
        return KEY_MUTE;
    case 0x1008ff13:
        return KEY_VOLUMEUP;
    case 0x1008ff14:
        return KEY_PLAYPAUSE;
    case 0x1008ff15:
        return KEY_STOPCD;
    case 0x1008ff16:
        return KEY_PREVIOUSSONG;
    case 0x1008ff17:
        return KEY_NEXTSONG;
    case 0x1008ff26:
        return KEY_BACK;
    case 0x1008ff31:
        return KEY_PAUSECD;
    }
    return 0;
}

unsigned keysym_evdev(unsigned keysym)
{
    static const unsigned char letters[26] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
        KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    };
    if (keysym >= 'a' && keysym <= 'z')
        return letters[keysym - 'a'];
    if (keysym >= 'A' && keysym <= 'Z')
        return letters[keysym - 'A'];
    if (keysym > 0xffff)
        return xf86(keysym);
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].keysym == keysym)
            return table[i].evdev;
    return 0;
}
