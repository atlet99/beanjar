# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# run.sh's test programs, build/tests/gl, against the gl layer's libraries: the KMS client, and
# glyph-cache, the GL calls of the firmware's Qt Quick 5.0 distance-field glyph cache, and
# tex-formats, each image texture format uploaded, drawn and read back, and int-attribs, integer
# vertex attributes (glVertexAttribIPointer) read back through the vertex shader.
guest_programs += {
  'tests/gl/kms-gles': executable('kms-gles', 'kms-gles.c',
    dependencies: mesa_dep, link_args: ['-lEGL', '-lGLESv2', '-lgbm', '-ldrm']),
  'tests/gl/glyph-cache': executable('glyph-cache', 'glyph-cache.c',
    dependencies: mesa_dep, link_args: ['-lEGL', '-lGLESv2']),
  'tests/gl/tex-formats': executable('tex-formats', 'tex-formats.c',
    dependencies: mesa_dep, link_args: ['-lEGL', '-lGLESv2']),
  'tests/gl/int-attribs': executable('int-attribs', 'int-attribs.c',
    dependencies: mesa_dep, link_args: ['-lEGL', '-lGLESv2']),
}
