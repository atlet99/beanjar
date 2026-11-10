# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pixel elements from the optional, externally installed go-textshot CLI."""

import json
import math
import os
import re
import shutil
import struct
import subprocess
import tempfile

from .core import Error

INSTALL_HINT = "Install it with: go install github.com/devcell-sh/go-textshot/cmd/textshot@latest"


def executable():
    """Resolve the user's textshot before capturing a picture or contacting a host."""
    name = os.environ.get("BEANJAR_TEXTSHOT") or "textshot"
    binary = shutil.which(name)
    if binary is None:
        raise Error("go-textshot is not installed\n" + INSTALL_HINT)
    return os.path.abspath(binary)


def read(image, binary=None, out=None):
    """Read one PNG on this machine. Upstream owns its cache, runtime and model downloads."""
    binary = binary or executable()
    image = os.path.abspath(image)
    try:
        with open(image, "rb") as f:
            header = f.read(24)
        if len(header) != 24 or header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR":
            raise Error(f"{image}: not a PNG")
        width, height = struct.unpack(">II", header[16:24])
        if not width or not height:
            raise Error(f"{image}: invalid PNG size")
        # Resolve explicit relative cache/library paths before entering the scratch cwd.
        env = dict(os.environ)
        for key in ("TEXTSHOT_CACHE_DIR", "TEXTSHOT_ONNXRUNTIME_LIB"):
            if env.get(key):
                env[key] = os.path.abspath(env[key])
        if env.get("LD_LIBRARY_PATH"):
            env["LD_LIBRARY_PATH"] = ":".join(
                os.path.abspath(p or ".") for p in env["LD_LIBRARY_PATH"].split(":")
            )
        # ORT can leave a :memory:.ses scratch file in its working directory.
        # Keep inference scratch away from the user's image and the checkout.
        with tempfile.TemporaryDirectory(prefix="beanjar-elements-") as tmp:
            result = subprocess.run(
                [binary, "-json", "-threads", "4", image],
                cwd=tmp,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=180,
            )
    except subprocess.TimeoutExpired:
        raise Error("go-textshot timed out after 180 s") from None
    except OSError as e:
        raise Error(f"go-textshot: {e}") from None
    if result.returncode:
        raise Error(result.stderr.strip() or f"go-textshot exited with status {result.returncode}")
    if out and result.stderr.strip():
        out(result.stderr.rstrip())
    try:
        data = json.loads(result.stdout)
        return frame(data, width, height)
    except (ValueError, TypeError, KeyError) as e:
        raise Error(f"go-textshot returned invalid element JSON: {e}") from None


def frame(data, width, height):
    """Preserve upstream fields and reading order; add convenient geometry and a unit alias."""
    if not isinstance(data, list):
        raise ValueError("expected an array")
    elements = []
    for e in data:
        if not isinstance(e, dict):
            raise ValueError("expected an element object")
        if type(e["id"]) is not int or e["kind"] not in ("button", "icon", "text", "progress"):
            raise ValueError("invalid id or kind")
        box = [e[k] for k in ("x0", "y0", "x1", "y1")]
        if not all(type(v) is int for v in box):
            raise ValueError("expected integer box coordinates")
        if not isinstance(e["style"], dict) or not isinstance(e.get("text", ""), str):
            raise ValueError("invalid style or text")
        if e.get("value") is not None and (
            type(e["value"]) not in (float, int) or not math.isfinite(e["value"])
        ):
            raise ValueError("invalid value")
        elements.append(
            {
                **e,
                "box": box,
                "centre": [(box[0] + box[2]) / 2, (box[1] + box[3]) / 2],
                "text": e.get("text", ""),
                "value": e.get("value"),
                "unit": e.get("uom", ""),
            }
        )
    # The CLI exposes neither theme nor backdrop. Do not infer them from styles or logs.
    return {"width": width, "height": height, "elements": elements}


def quote(text):
    """JSON-style double quotes, with YAML line breaks and controls also escaped."""
    return re.sub(
        r"[\x7f-\x9f\u2028\u2029]",
        lambda m: f"\\u{ord(m[0]):04x}",
        json.dumps(text, ensure_ascii=False),
    )


def yaml_key(text):
    """Quote the whole scalar when label punctuation would create YAML syntax.

    Playwright's ariaSnapshotRenderer also applies yamlEscapeKeyIfNeeded after
    quoting a name. Our keys always start with a role, so only interior hazards apply.
    """
    if re.search(r"[:](?:\s|$)|\s#|[{}`]", text):
        return "'" + text.replace("'", "''") + "'"
    return text


def snapshot(data):
    """Flat, aria-snapshot-like lines; refs are upstream IDs for this frame only."""
    lines = [f"- screen [size={data['width']}x{data['height']}]"]
    for e in data["elements"]:
        line = e["kind"]
        if e["text"]:
            line += " " + quote(e["text"])
        line += f" [ref=e{e['id']}] [box={','.join(str(v) for v in e['box'])}]"
        if e["value"] is not None:
            line += f" [value={json.dumps(e['value'])}]"
        if e["unit"]:
            line += f" [unit={quote(e['unit'])}]"
        # Upstream "disabled" is low contrast, not state: webOS draws unfocused buttons grey.
        for flag, name in (
            ("disabled", "dimmed"),
            ("highlighted", "highlighted"),
            ("focused", "focused"),
        ):
            if e["style"].get(flag) is True:
                line += f" [{name}]"
        lines.append("- " + yaml_key(line))
    return "\n".join(lines)
