# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# What every module uses: the build's own scripts, and the unpacked source trees of the wraps.
python = find_program('python3')
meson_cmd = find_program('meson')      # the Meson running this build (the system's)
stage_py = files('stage.py')           # assemble a tree (layer, SSH binaries, test programs)
publish_py = files('publish.py')       # copy results to publish_dir without disturbing a VM
initramfs_py = files('initramfs.py')   # pack the initramfs with the kernel's gen_init_cpio
layer_script = [python, files('layer-script.py')]   # layers made by their own build-layer.sh
kbuild = files('kbuild.sh')            # the kernel's and busybox's own Kbuild
venv_sh = files('venv.sh')             # a venv from a hash-pinned requirements file

# subprojects/<name>.wrap, unpacked into subprojects/<directory> by Meson's own wrap handling
# (meson/fetch.py runs `meson subprojects download` and redoes it when the wrap or a patch
# changes). None is configured as a Meson subproject: the trees are built by their own build
# systems from custom targets, which depend on the 'stamp' of the tree they use. A tree is fetched
# only when something that is built needs it.
fetch_py = files('fetch.py')
sources = {}
foreach name : ['linux', 'busybox', 'gstreamer', 'gstreamer18', 'gstreamer112', 'gstreamer1141', 'gstreamer1144', 'gstreamer1162', 'dropbear', 'openssh', 'libffi', 'libdrm',
                'wayland', 'wayland120', 'wayland122', 'wayland-protocols', 'mesa', 'qemu', 'usbipdcpp']
  directory = ''
  foreach line : fs.read(top / 'subprojects' / name + '.wrap').split('\n')
    if line.startswith('directory')
      directory = line.split('=')[1].strip()
    endif
  endforeach
  sources += {name: {
    'dir': top / 'subprojects' / directory,
    'version': directory.split(name + '-')[-1],     # linux-6.18.53 -> 6.18.53
    'stamp': custom_target('fetch-' + name,
      output: name + '.fetched',
      depfile: name + '.fetched.d',
      command: [python, fetch_py, meson_cmd, top, name, '@OUTPUT@', '@DEPFILE@']),
  }}
endforeach

if not guest_lane
  subdir('coverage')                     # -Db_coverage=true: docs/dev/guides/coverage.md
  # The GTK 4 windows, the viewer's and the VM manager's (host/ui, beanjar_ui), are Python
  # (PyGObject) with their layouts in Blueprint files (host/ui/data/<window>/*.blp), compiled
  # into a GResource bundle each. GTK 4 and libadwaita for the system Python (python3-gi,
  # gir1.2-gtk-4.0, gir1.2-adw-1) are what they run with, and blueprint-compiler runs with them
  # too: it reads the installed typelibs (through GIRepository) to check every class and property,
  # so a .blp using something newer than this host's GTK or libadwaita does not build. The code
  # needs GTK 4.12 (GtkGLArea.set_allowed_apis, GtkCssProvider.load_from_string, EGL on X11 by
  # default) and libadwaita 1.7 (AdwInlineViewSwitcher); Debian 13 has 4.18 and 1.7. Without
  # them the bundles are not built: the viewer runs headless only, the manager's CLI works and
  # its GUI does not.
  gtk_python = find_program('/usr/bin/python3', 'python3')   # the system Python, with PyGObject
  gtk_check = run_command(gtk_python, '-c',
    'import warnings; warnings.simplefilter("ignore"); ' +
    'import gi; gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1"); ' +
    'from gi.repository import Adw, Gtk; from gi.repository import GIRepository; ' +
    'print("%d.%d.%d %d.%d.%d" % (Gtk.get_major_version(), Gtk.get_minor_version(), ' +
    'Gtk.get_micro_version(), Adw.get_major_version(), Adw.get_minor_version(), Adw.get_micro_version()))',
    check: false)
  gtk_versions = gtk_check.stdout().strip().split(' ')
  have_gtk = (gtk_check.returncode() == 0 and gtk_versions.length() == 2 and
    gtk_versions[0].version_compare('>= 4.12') and gtk_versions[1].version_compare('>= 1.7'))
  if have_gtk
    message('GTK ' + gtk_versions[0] + ' and libadwaita ' + gtk_versions[1] +
            ' through PyGObject: the GTK windows are built')
  else
    message('no GTK >= 4.12 and libadwaita >= 1.7 for Python (python3-gi, gir1.2-gtk-4.0, ' +
            'gir1.2-adw-1): the viewer runs headless only, the manager has no GUI')
  endif
  # Debian 13's blueprint-compiler (0.16; the Python script runs with the system python3).
  blueprint = find_program('blueprint-compiler', version: '>= 0.16', required: have_gtk)
  gnome = import('gnome')
endif
