// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_IR_H
#define VIEWER_IR_H
/* The TV remote's infrared to the guest's micom (include/webos-ir.h): button edges on the
 * webos.ir port's socket, ir.sock next to the D-Bus socket (--ir-socket). The connection is made
 * at start and again whenever a send finds it closed (a restarted VM). One button is down at a
 * time, as on a remote: pressing another releases it first. */
struct ir {
    int fd;
    char *path;
    int held;       /* the LG data code down, or -1 */
    int local_held; /* the local key code down, or -1 */
    unsigned sent, errors;
};
void ir_init(struct ir *ir, const char *path);
/* A button edge with LG's custom code; presses of the button already down and releases of one
 * that is not are dropped. Returns 0 when written. */
int ir_button(struct ir *ir, unsigned code, int down);
/* A local key edge (the set's own button, WEBOS_LOCAL_*): the micom times a short or long press.
 * One local key is down at a time; it is independent of the remote's button. */
int ir_local_key(struct ir *ir, unsigned code, int down);
/* Releases the button and the local key down, if any (focus loss, exit). */
void ir_release(struct ir *ir);
void ir_close(struct ir *ir);
#endif
