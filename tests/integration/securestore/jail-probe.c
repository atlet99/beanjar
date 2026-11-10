// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Test the production device exposure hook without replacing the probe's libc chroot. */
#define chroot beanjar_probe_jail_hook
#include "../../../guest/layers/common/src/jail-trust.c"
int probe_expose_store(const char *jail)
{
    return sestore_device(jail);
}
void probe_clear_legacy_store(const char *jail)
{
    remove_legacy_store(jail);
}
