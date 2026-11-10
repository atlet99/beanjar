// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* guest/layers/common/hal-common: hal_log, the "tvservice-hal:" syslog line of the libhal_lg115x
 * shim (libhal_lg115x/) and of tvservice's private view of the OS (guest/layers/common/tvservice).
 */
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "hal-common.h"

void hal_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    syslog(LOG_INFO, "tvservice-hal: %s", line);
}
