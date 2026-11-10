// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BEANJAR_VNC_KEYSYM_H
#define BEANJAR_VNC_KEYSYM_H
/* A VNC client's X keysym as the PC key that types it on a US layout (Linux evdev code), for
 * clients without QEMU's extended key events, which carry the key itself. The core maps evdev
 * codes to the remote's buttons and the keyboard (wv_key_evdev). 0: no such key. */
unsigned keysym_evdev(unsigned keysym);
#endif
