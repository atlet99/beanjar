#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract LM14's pure MAdp_SYS_GetMMAP switch into mmapger records.

The stock leaf stores constant address/length pairs. This recognises that compiler
pattern only, failing on different code, calls, relocated literals or bad ranges.
No firmware-derived data belongs in the source tree. The output belongs in build/.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

_installed = Path(__file__).resolve().parents[2]
sys.path.insert(
    0,
    str(
        _installed / "core"
        if (_installed / "installation.toml").is_file()
        else Path(__file__).resolve().parents[3] / "host/core"
    ),
)
from beanjar.memory import binary_records


class Ambiguous(ValueError):
    pass


def _extract(data):
    from elftools.elf.elffile import ELFFile

    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_ARM":
        raise Ambiguous("expected little-endian ELF32 ARM")
    dynsym = elf.get_section_by_name(".dynsym")
    if dynsym is None:
        raise Ambiguous("missing dynamic symbols")
    syms = [
        s
        for s in dynsym.iter_symbols()
        if s.name == "MAdp_SYS_GetMMAP" and s["st_shndx"] != "SHN_UNDEF"
    ]
    if len(syms) != 1 or syms[0]["st_value"] & 3:
        raise Ambiguous("expected one ARM GetMMAP definition")
    start, size = syms[0]["st_value"], syms[0]["st_size"]
    if not 64 <= size <= 65536:
        raise Ambiguous("unbounded GetMMAP definition")
    end = start + size
    segments = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    relocated = {}
    for section in elf.iter_sections():
        if section["sh_type"] in ("SHT_REL", "SHT_RELA"):
            for r in section.iter_relocations():
                relocated[r["r_offset"]] = (r["r_info_type"], r["r_info_sym"])

    def word(addr, relative=False):
        reloc = relocated.get(addr)
        if (
            not start <= addr <= end - 4
            or addr & 3
            or (reloc and not (relative and reloc == (23, 0)))
        ):  # R_ARM_RELATIVE link VA
            raise Ambiguous(f"outside leaf or relocated word at {addr:#x}")
        for s in segments:
            if s["p_vaddr"] <= addr <= s["p_vaddr"] + s["p_filesz"] - 4:
                return struct.unpack_from("<I", data, s["p_offset"] + addr - s["p_vaddr"])[0]
        raise Ambiguous("unmapped leaf word")

    prefix = (0xE52DB004, 0xE28DB000, 0xE24DD014, 0xE50B0008, 0xE50B100C, 0xE50B2010, 0xE51B3008)
    if tuple(word(start + 4 * i) for i in range(7)) != prefix:
        raise Ambiguous("unknown GetMMAP prologue")
    cmp = word(start + 28)
    if cmp & 0xFFFFFF00 != 0xE3530000 or word(start + 32) != 0x979FF103:
        raise Ambiguous("unknown switch bound or dispatch")
    count = (cmp & 255) + 1
    if count > 256:
        raise Ambiguous("too many records")
    table = start + 40
    if table + count * 4 > end:
        raise Ambiguous("truncated jump table")

    def branch(pc, op):
        if op & 0xFF000000 != 0xEA000000:
            raise Ambiguous(f"expected unconditional branch at {pc:#x}")
        delta = op & 0xFFFFFF
        if delta & 0x800000:
            delta -= 1 << 24
        return pc + 8 + delta * 4

    def constant(pc):
        op = word(pc)
        if op & 0xFFFFF000 == 0xE3A02000:  # mov r2, ARM rotated immediate
            value, rotate = op & 255, ((op >> 8) & 15) * 2
            return ((value >> rotate) | (value << (32 - rotate))) & 0xFFFFFFFF
        if op & 0xFFFFF000 == 0xE59F2000:  # ldr r2, [pc, #literal]
            return word(pc + 8 + (op & 0xFFF))
        raise Ambiguous(f"nonconstant output at {pc:#x}")

    epilogue = (0xE1A00003, 0xE28BD000, 0xE8BD0800, 0xE12FFF1E)

    def false_return(pc):
        if word(pc) != 0xE3A03000:
            raise Ambiguous("unknown false return value")
        target = branch(pc + 4, word(pc + 4))
        if tuple(word(target + 4 * i) for i in range(4)) != epilogue:
            raise Ambiguous("unknown false return epilogue")

    false_return(branch(start + 36, word(start + 36)))

    def row(pc):
        values = []
        for offset in (12, 16):
            if word(pc) != 0xE51B3000 | offset:
                if not values and word(pc) == 0xE3A03000:
                    false_return(pc)
                    return (0, 0)
                raise Ambiguous(f"unknown output pointer at {pc:#x}")
            values.append(constant(pc + 4))
            if word(pc + 8) != 0xE5832000:
                raise Ambiguous("unknown constant store")
            pc += 12
        target = branch(pc, word(pc))
        if tuple(word(target + 4 * i) for i in range(5)) != (
            0xE3A03001,
            0xE1A00003,
            0xE28BD000,
            0xE8BD0800,
            0xE12FFF1E,
        ):
            raise Ambiguous("unknown true return")
        return tuple(values)

    return [row(word(table + 4 * i, relative=True)) for i in range(count)]


def extract(data):
    from elftools.common.exceptions import ELFError

    try:
        return _extract(data)
    except (ELFError, struct.error, IndexError) as e:
        raise Ambiguous(f"malformed ELF: {e}") from e


def live_rows(path):
    rows = []
    try:
        for index, line in enumerate(path.read_text().splitlines()):
            fields = line.split()
            if len(fields) != 3 or int(fields[0]) != index:
                raise Ambiguous("live dump IDs must be consecutive from zero")
            rows.append(tuple(int(v, 16) for v in fields[1:]))
    except ValueError as e:
        raise Ambiguous(f"invalid live dump: {e}") from e
    return rows


def records(rows, interval, length):
    if not 0 < length <= interval <= 0xFFFFFFFF:
        raise Ambiguous("invalid MIU geometry")
    out = bytearray()
    for addr, size in rows:
        miu = int(addr >= interval)
        offset = addr - miu * interval
        if offset > length or size > length - offset:
            raise Ambiguous("GetMMAP record outside MIUs")
        out += struct.pack("<5I", offset, offset, 0, size, 4 | miu)
    return bytes(out)


REPO = Path(__file__).resolve().parents[3]


def cached(fw, name, interval, length, count=None):
    """build/mstar/<name>/mmap.bin of the firmware directory fw (its vendor pak's libmadp.so),
    reused while the source hash, MIU geometry, record count and output digest agree.
    Older metadata without an output digest is regenerated on a host with pyelftools. A host with the cache
    copied from another needs no pyelftools."""
    import subprocess
    from beanjar import paths

    tree = paths.Tree(_installed if (_installed / "installation.toml").is_file() else REPO)
    r = subprocess.run(
        ["unsquashfs", "-cat", "-no-wildcards", str(Path(fw) / "vend.pak"), "libmadp.so"],
        stdout=subprocess.PIPE,
    )
    if r.returncode != 0 or not r.stdout:
        raise Ambiguous(f"no libmadp.so in {fw}/vend.pak")
    data = r.stdout
    out = Path(tree.cache("mstar", name, "mmap.bin"))
    want = {
        "source": "MAdp_SYS_GetMMAP",
        "sha256": hashlib.sha256(data).hexdigest(),
        "miu_interval": interval,
        "miu_length": length,
    }
    try:
        have = json.loads(out.with_suffix(".json").read_text())
        if (
            isinstance(have, dict)
            and all(have.get(k) == v for k, v in want.items())
            and out.is_file()
        ):
            encoded = out.read_bytes()
            actual_count = have.get("count")
            if (
                isinstance(actual_count, int)
                and actual_count > 0
                and (count is None or actual_count == count)
                and have.get("output_sha256") == hashlib.sha256(encoded).hexdigest()
            ):
                binary_records(encoded, actual_count, length)
                return out
    except (OSError, ValueError):
        pass
    rows = extract(data)
    if count is not None and len(rows) != count:
        raise Ambiguous(f"GetMMAP has {len(rows)} records; configured mmap_ids is {count}")
    encoded = records(rows, interval, length)
    binary_records(encoded, len(rows), length)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(encoded)
    out.with_suffix(".json").write_text(
        json.dumps(
            {**want, "count": len(rows), "output_sha256": hashlib.sha256(encoded).hexdigest()},
            indent=2,
        )
        + "\n"
    )
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("library", type=Path, help="libmadp.so, or with --cache the firmware directory")
    p.add_argument("--output", type=Path)
    p.add_argument(
        "--cache",
        action="store_true",
        help="write/reuse build/mstar/<name>/mmap.bin; print its path",
    )
    p.add_argument("--name", help="the firmware name for --cache (default: the directory's)")
    p.add_argument("--miu-interval", type=lambda x: int(x, 0), required=True)
    p.add_argument("--miu-length", type=lambda x: int(x, 0), required=True)
    p.add_argument("--verify-live", type=Path)
    p.add_argument("--count", type=int, help="required configured mmap_ids count")
    args = p.parse_args()
    if args.cache:
        print(
            cached(
                args.library,
                args.name or args.library.resolve().name,
                args.miu_interval,
                args.miu_length,
                args.count,
            )
        )
        return
    if not args.output:
        p.error("--output is required without --cache")
    data = args.library.read_bytes()
    rows = extract(data)
    if args.count is not None and len(rows) != args.count:
        raise Ambiguous(f"GetMMAP has {len(rows)} records; configured mmap_ids is {args.count}")
    if args.verify_live:
        live = live_rows(args.verify_live)
        if rows != live:
            raise Ambiguous("static table differs from live GetMMAP")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    encoded = records(rows, args.miu_interval, args.miu_length)
    binary_records(encoded, len(rows), args.miu_length)
    args.output.write_bytes(encoded)
    args.output.with_suffix(".json").write_text(
        json.dumps(
            {
                "source": "MAdp_SYS_GetMMAP",
                "sha256": hashlib.sha256(data).hexdigest(),
                "count": len(rows),
                "miu_interval": args.miu_interval,
                "miu_length": args.miu_length,
                "output_sha256": hashlib.sha256(encoded).hexdigest(),
            },
            indent=2,
        )
        + "\n"
    )
    print(f"{len(rows)} firmware-derived mmapger records")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as e:
        raise SystemExit(f"mmap-table: {e}")
