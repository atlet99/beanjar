# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""virtio-gpu's page flips complete at a vblank timer (kernel patch 0022): the CRTCs have DRM's
vblank timer at their mode's refresh, the commit's event is armed at the flush rather than sent at
the host's answer, and the timer starts and stops with the CRTC. The rate itself is measured in a
booted guest (docs/dev/architecture/build-and-boot.md, "The display's refresh")."""

import re
from pathlib import Path

import pytest
from linuxsrc import linux_source

TOP = Path(__file__).resolve().parents[3]
PATCH = "0022-virtio-gpu-vblank-timer.patch"
DISPLAY = Path(linux_source()) / "drivers/gpu/drm/virtio/virtgpu_display.c"


def body(text, name):
    m = re.search(r"^static void " + name + r"\(.*?^\}", text, re.M | re.S)
    assert m, name
    return m.group(0)


def test_patch_is_applied_by_the_wrap():
    assert (TOP / "guest/kernel/patches" / PATCH).is_file()
    assert "linux/" + PATCH in (TOP / "subprojects/linux.wrap").read_text()


@pytest.mark.skipif(not DISPLAY.is_file(), reason=f"no unpacked kernel source: {DISPLAY}")
def test_flips_complete_at_the_vblank_timer():
    text = DISPLAY.read_text()
    funcs = re.search(r"virtio_gpu_crtc_funcs = \{(.*?)\};", text, re.S).group(1)
    assert "DRM_CRTC_VBLANK_TIMER_FUNCS" in funcs
    assert "drm_vblank_init(vgdev->ddev, vgdev->num_scanouts)" in text
    assert "drm_crtc_vblank_atomic_flush(crtc, state)" in body(text, "virtio_gpu_crtc_atomic_flush")
    assert "drm_crtc_vblank_on(crtc)" in body(text, "virtio_gpu_crtc_atomic_enable")
    disable = body(text, "virtio_gpu_crtc_atomic_disable")
    # off before the scanout goes, so a pending event is sent rather than left waiting
    assert disable.index("drm_crtc_vblank_off(crtc)") < disable.index("virtio_gpu_cmd_set_scanout")
