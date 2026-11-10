// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Loaded only by the enabled compositor bootstrap after Qt initialization.
 * QByteArray/QGuiApplication are public Qt 5 APIs. The QPA native-interface
 * declaration in the build SDK is identical to upstream Qt 5.12.12's
 * src/gui/kernel/qplatformnativeinterface.h, matching the supplied firmware.
 * No Qt object storage, cursor object size or screen fields are reproduced.
 */
#include <QtCore/QByteArray>
#include <QtGui/QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <fcntl.h>

extern "C" __attribute__((visibility("default")))
void beanjar_native_input_bind_cursor(void *input_module)
{
    auto set_device = reinterpret_cast<int (*)(int, int)>(
        dlsym(input_module, "IM_SetCursorDrmDevice"));
    if (!set_device) {
        std::fprintf(stderr, "kms-native-input: IM cursor DRM export missing\n");
        return;
    }
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!native || !screen) {
        std::fprintf(stderr, "kms-native-input: Qt native interface/screen unavailable\n");
        return;
    }
    /* Supplied QEglFSKmsIntegration encodes the integers in the pointer value;
     * these are not addresses. Reject unavailable or out-of-range resources.
     * A null fd resource cannot distinguish unavailable from fd 0, so it is
     * conservatively rejected. Do not open, duplicate or close Qt's descriptor.
     */
    const uintptr_t fd_value = reinterpret_cast<uintptr_t>(
        native->nativeResourceForIntegration(QByteArray("dri_fd")));
    const uintptr_t crtc_value = reinterpret_cast<uintptr_t>(
        native->nativeResourceForScreen(QByteArray("dri_crtcid"), screen));
    if (!fd_value || fd_value > INT_MAX || !crtc_value || crtc_value > INT_MAX) {
        std::fprintf(stderr, "kms-native-input: Qt cursor DRM resources unavailable\n");
        return;
    }
    const int fd = static_cast<int>(fd_value);
    if (fcntl(fd, F_GETFD) < 0) {
        std::fprintf(stderr, "kms-native-input: Qt cursor DRM descriptor invalid: errno=%d\n", errno);
        return;
    }
    /* The stock QStarfishIMCursor(int,int) uses this exact call before input
     * startup. IM borrows the descriptor for its GBM device; its own cursor
     * task initializes resources and the default shape. A zero return only
     * confirms acceptance: libim can also log a failed gbm_create_device and
     * return zero. Rendering must therefore be validated separately.
     */
    const int result = set_device(fd, static_cast<int>(crtc_value));
    std::fprintf(stderr, "kms-native-input: stock IM cursor DRM binding result=%d\n", result);
}
