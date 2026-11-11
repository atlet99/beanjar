# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Initial O22 profile: standard EGL through the stock Mali soname.
# The stock cursor initializer waits for actual GAL surfaces before input reads.
# Its 1056-byte surfaces and 76-byte capability match the software GAL backend.
# Keep all other stock HAL/media code until runtime establishes its contracts.
# libdile_crypto: O22's DILE_CRYPTO_ReadMskToSecureStore is a libsestore read only (the item by
# TV region), and the emulator's secure store holds no model secret, so SM_SA_GetNDUID failed and
# nyx made a random NDUID at every open. The shim answers that read, as on K7LP (O22N and O22N2
# inherit it).
# libaibsp, libaibspLib: LG's AI picture libraries drive an NPU and display registers the VM lacks.
# nn-proxy's AIBSP_Initialize failed at /dev/video241 and its teardown crashed; pqcontroller's
# workers retried /dev/mem forever and W24G's AIBSP_PQ_Enable read a failed mmap. Every imported
# entry succeeds as a no-op. Spacebuns has both too.
hal_stock = {
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libdile_crypto': ['libdile_crypto.so.0', 'libdile_crypto.so.0.1.1', []],
  'libaibsp': ['libaibsp.so.1', 'libaibsp.so.1.0.0', []],
  'libaibspLib': ['libaibspLib.so.1', 'libaibspLib.so.1.0.0', []],
}
# The media elements under the stock modern LX names: the stock ones need /dev/vo and an AIMC
# audio device, so the sink failed to start and the AAC decoder crashed on a NULL AIMC handle.
gst_plugin_dir = 'usr/lib/gstreamer-1.0'
# From ombre on, the stock elements follow the release, not the chip (their build ids): every
# O22-family ombre has W24G ombre's (W22O's and W23O's are builds of the same source), every ponytail
# W22O ponytail's video elements and every queue W22O queue's (gst-inspect of those); a later
# release takes queue's (webos_reached: the release order, docs/dev/architecture/build-and-boot.md).
# Before ombre each chip has its own (o22n).
gst_elements = 'lx-o22'
if 'queue' in webos_reached
  gst_elements = 'lx-o22-queue'
elif 'ponytail' in webos_reached
  gst_elements = 'lx-o22-ponytail'
elif 'ombre' in webos_reached
  gst_elements = 'lx-o22n2'
endif
# W22O's ponytail and queue audio builds dropped DTS; W23O's, W24G's and W24O's of those releases
# keep it (dts-seamless and dts_audiodec, gst-inspect of W24G 33.31 and 43.21 and W23O 44.00). O22N3's
# and O24N's ponytail audio builds are W22O's.
if soc in ['o22n', 'o22n2', 'o24'] and gst_elements in ['lx-o22-ponytail', 'lx-o22-queue']
  gst_elements += '-dts'
endif
# libsestore (guest/layers/soc/meson.build adds it wherever nyx's secure store reads it): queue's
# libsecretagent reads the model secret through nyx (nyxCrypto, nyxSecureStore), which asks
# libsestore for the SDP_MODEL item with no DILE call between, so the libdile_crypto shim never saw
# it: SA_getDeviceID failed and sdx sent LG's login without X-Device-ID. On this chip the shim
# then answers the regional model items too.
sestore_model_items_nyx = true
# libkadaptor: after LG's login, sdx sets the server time on the secure clock (libsecuretime, nyxDrm,
# libdrmddi, LGSOC_DRM_SetSecureClock), which ends in PlayReady's TA. With no TEE that call failed,
# sdx stayed unauthenticated and eula-service never started: the User Agreements had no terms.
# The shim answers the TA call TEEC_ERROR_NOT_SUPPORTED, which KADP takes as success. KADP is
# bsppart's; its .so.0 names, links to the stock file there, are made links to the shim. Wherever
# the firmware's secure clock goes through nyx's DRM module (nyxDrm.module: queue and spacebuns).
fw_questions += 'path usr/lib/nyx/modules/nyxDrm.module'
if fw_path.get('usr/lib/nyx/modules/nyxDrm.module', false)
  hal_stock += {'libkadaptor': ['libkadaptor.so.2', 'libkadaptor.so.2.0.1',
    ['libkadaptor.so.0', 'libkadaptor.so.0.1.0'], 'bsppart']}
  kadaptor_source = 'kadaptor-securetime.c'
endif
# The Qt EGLFS Starfish setup (starfish/): wherever the firmware's compositor is Qt with LG's
# eglfs_starfish integration (QT_QPA_EGLFS_INTEGRATION=eglfs_starfish in product.env, which ships
# that plugin with it): W23O's, W24G's, W24O's, W25G's and W25O's ombre and ponytail. W22O's ombre
# and ponytail have neither (the legacy starfish QPA through libegl-kms.so, gl layer), and the
# queue firmwares keep the plugin and env file beside a Weston compositor (weston.ini), which
# never reads them. The layer's usr/share/beanjar/kernel.cmdline gives the launcher the kernel
# words the plugin needs (virtio_gpu.modifiers=1).
starfish_plugin = 'usr/lib/plugins/egldeviceintegrations/libeglfs-starfish-integration.so' # usr/lib/qt5 links to usr/lib
weston_ini = 'usr/share/weston/weston.ini'
fw_questions += ['path ' + starfish_plugin, 'path ' + weston_ini]
if fw_path.get(starfish_plugin, false) and not fw_path.get(weston_ini, false)
  soc_layer_trees += ['--tree', meson.current_source_dir() / 'starfish', '.']
endif
soc_phys_base = '0xA0000000u' # emulator scratch arena, not a retail memory layout
gl_vendor_stubs = []
gl_vendor_gl = 'mali'
gl_vendor_gl_soname = 'libmali.so.0'
gl_vendor_gl_needs = ['EGL', 'GLESv2', 'GLESv1_CM', 'wayland-egl']
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
