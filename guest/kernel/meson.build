# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The arm64 guest kernel, build/Image: configs/webos_virt_defconfig (a savedefconfig file; see
# docs/dev/guides/build.md, "Guest kernel"), with the patches of subprojects/linux.wrap. Built by its own
# Kbuild with CROSS_COMPILE from the arm64_cross_compile option (meson/kbuild.sh); it re-runs when
# the unpacked source, the defconfig or the script change, and make keeps the re-run incremental.
# Its second output is the tree's own usr/gen_init_cpio, which packs the initramfs (guest/initramfs/),
# its third the defconfig's own =m modules as modules_install lays them out, with the modules.dep
# that depmod generated (<release>/...), which the initramfs carries in /lib/modules.
# The out-of-tree modules in modules/ are built against its object tree.
#
# Nothing of it depends on the target: it is built once, in <shared_dir>/kernel/<key> (meson/shared.py),
# whose key is the compiler prefix, the defconfig's name and the source tree, and every target's
# build directory takes the result from there: linux-obj/ (the object tree), the outputs, and a
# directory per out-of-tree module. A changed input (the tree's wrap key, the defconfig, the script,
# the compiler's version, a module's sources) runs make again in the same directory, once.
arm64_gcc = get_option('arm64_cross_compile') + 'gcc'
find_program(arm64_gcc, native: true)
# kernel_debug: configs/debug.config over the defconfig, as a defconfig of its own name
kernel_defconfig = files('configs/webos_virt_defconfig')
kernel_defconfig_name = 'webos_virt_defconfig'
if get_option('kernel_debug')
  kernel_defconfig = custom_target('webos_virt_debug_defconfig',
    input: ['configs/webos_virt_defconfig', 'configs/debug.config'],
    output: 'webos_virt_debug_defconfig',
    command: ['sh', '-c', 'cat "$1" "$2" > "$3"', 'sh', '@INPUT0@', '@INPUT1@', '@OUTPUT@'])
  kernel_defconfig_name = 'webos_virt_debug_defconfig'
endif
kernel_root = shared_dir / 'kernel' / run_command(python, shared_py, 'key',
  'cross_compile ' + get_option('arm64_cross_compile'), 'defconfig ' + kernel_defconfig_name,
  'source ' + sources['linux']['dir'], check: true).stdout().strip()
kernel_obj = kernel_root / 'linux-obj'
# shared.py run, for the kernel and each module: the compiler's version is an input of all of them
kernel_shared = [python, shared_py, 'run', '--tool', arm64_gcc]
kernel_outputs = custom_target('kernel',
  output: ['Image', 'gen_init_cpio', 'kernel-modules'],
  depfile: 'kernel.d',
  depends: sources['linux']['stamp'],
  depend_files: [kbuild, files('configs/webos_virt_defconfig', 'configs/debug.config')],
  command: kernel_shared + ['--dir', kernel_root, '--depfile', '@DEPFILE@',
            '--input', sources['linux']['dir'] / '.wrap-key', kbuild, kernel_defconfig,
            '--file', kernel_root / 'Image', '@OUTPUT0@',
            '--file', kernel_root / 'gen_init_cpio', '@OUTPUT1@',
            '--tree', kernel_root / 'modules', '@OUTPUT2@', '--',
            kbuild, 'linux', sources['linux']['dir'], kernel_obj,
            get_option('arm64_cross_compile'), kernel_defconfig, kernel_root / 'Image',
            kernel_root / 'gen_init_cpio', kernel_root / 'modules'],
  console: true,
  build_by_default: true)
kernel = kernel_outputs[0]
gen_init_cpio = kernel_outputs[1]
kernel_modules = kernel_outputs[2]
published += [['file', kernel.full_path(), 'Image']]
publish_depends += kernel_outputs

subdir('modules/lgemu')
subdir('modules/media-pool')
subdir('modules/mstar-utopia')
subdir('modules/lg115x')
subdir('modules/rtk')
subdir('modules/btmtk_usb')
subdir('modules/webos_tv')
subdir('modules/dtv_soc')
subdir('modules/lgfs')

# The modules /init loads with modprobe (boot.conf's PERIPHERAL_MODULES): the kernel's own
# (kernel_modules) with the out-of-tree ones that load among them in extra/, and depmod's
# modules.dep over all of them. The initramfs carries this tree in /lib/modules.
peripheral_modules = custom_target('peripheral-modules',
  output: 'peripheral-modules',
  depends: [kernel_outputs, lgfs_ko],
  depend_files: [kbuild],
  command: [kbuild, 'tree', sources['linux']['dir'], kernel_obj, get_option('arm64_cross_compile'),
            kernel_modules, '@OUTPUT@', lgfs_ko],
  build_by_default: true)

