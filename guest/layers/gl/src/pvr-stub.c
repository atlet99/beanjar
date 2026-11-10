// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Empty stand-in for the PowerVR user-mode libraries libIMGegl.so, libsrv_um.so and
 * libglslcompiler.so. QtWebKit, netflix and the htvbrowser libraries list them as NEEDED
 * (the PVR libraries have no SONAME, so the unversioned file name is what gets recorded) but
 * import no symbol from them; they only have to exist. Built once per name by build-layer.sh.
 * The same empty object, linked with NEEDED entries on Mesa, stands in for a vendor's one GL
 * library (guest/layers/gl/meson.build, gl_vendor_gl: Mali's libmali.so). */
