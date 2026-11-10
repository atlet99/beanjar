#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Run a virgl test (build/tests/gl, from the guest build) inside webOS against the gl layer's Mesa
# (build/layers/<chip>-<release>/gl/usr/lib), whether or not the layer is active. Put them into the
# VM's share first (on the host: host/scripts/vm-launcher put tests/regress/virgl/run.sh build/tests/gl
# build/layers/$(host/scripts/vm-launcher config TARGET)/gl/usr/lib),
# then from the debug shell:
#   webos-enter sh /mnt/host/tests/regress/virgl/run.sh gles2-triangle
#   webos-enter sh /mnt/host/tests/regress/virgl/run.sh kms-gles argb 20
#   webos-enter sh /mnt/host/tests/regress/virgl/run.sh tex-formats -v
H=/mnt/host
for M in "$H"/build/layers/*/gl/usr/lib; do break; done      # the one target put there
t=$1; shift
exec env LD_LIBRARY_PATH=$M GBM_BACKENDS_PATH=$M/gbm EGL_LOG_LEVEL=${EGL_LOG_LEVEL:-warning} \
	"$H/build/tests/gl/$t" "$@"
