// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Native application lifecycle probe, with no display or hardware dependencies.
 * Build with the webOS SDK; pass its path as NATIVE_BINARY to check-devmode.sh.
 * The marker proves SAM executed the packaged ARM binary inside the jail.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    FILE *out = fopen("/media/developer/temp/devmode-native-status", "w");
    if (!out)
        return 1;
    fprintf(out, "NATIVE-PASS uid=%lu pid=%lu\n", (unsigned long)getuid(), (unsigned long)getpid());
    if (fclose(out))
        return 1;
    /* Stay alive for dev/running and closeByAppId, with a failsafe for failed tests. */
    sleep(120);
    return 0;
}
