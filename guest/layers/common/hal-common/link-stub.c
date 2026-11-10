// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Link-time stand-in for a stock library a shim wraps (guest/layers/soc/src/<library>/meson.build
 * builds it under the stock file's name): its SONAME is the stock file's name
 * (libhal_lg115x.so.0.0.0, libdile_vt.so.0.1.0, libdile_gal.so.1.0.2, libhal_crypto.so.0.1.0), so
 * the shim records NEEDED on it. Never installed. */
void hal_link_stub(void);
void hal_link_stub(void)
{
}
