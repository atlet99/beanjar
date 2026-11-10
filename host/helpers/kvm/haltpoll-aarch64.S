// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
.text
.p2align 2
.global ioctl
.type ioctl, %function
ioctl:
    b haltpoll_ioctl
.size ioctl, .-ioctl
.section .note.GNU-stack,"",%progbits
