#!/usr/bin/env python3
"""Prepare a writable, pinned bb-patch reference from user-supplied game files."""

# SPDX-License-Identifier: GPL-3.0-only
# Patch locations and values follow bb-patch/bb-patch, revision f2312876412ee356f7496fd66d72f88364b73bbe.

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
from extract_native_assets import write_assets


ROOT = Path(__file__).resolve().parent.parent
BASE_SHA256 = "b30d7d0762dbd808407f0d95c6fdafa0b52ce67d0b0f20c8fbb7fa66a2213e89"
PATCH_REVISION = "f2312876412ee356f7496fd66d72f88364b73bbe"
LANGUAGES = ("english", "german", "french", "italian", "spanish")


def resolution(value):
    try:
        width, height = map(int, value.lower().split("x"))
        if not all(100 <= x <= 0xFFFFFFFF for x in (width, height)):
            raise ValueError()
        return width, height
    except ValueError:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT, with dimensions >= 100")


def prepare(args):
    source = args.source.resolve()
    output = args.output.resolve()
    patch_source = args.patch_source.resolve()
    # Read and validate everything before creating the output directory.
    image = bytearray((source / "BEASTS.EXE").read_bytes())
    if hashlib.sha256(image).hexdigest() != BASE_SHA256:
        raise ValueError("BEASTS.EXE is not the supported unpatched executable")
    resources = patch_source / "patch" / "resources"
    resource_data = {
        name: (resources / name).read_bytes()
        for name in ("ddraw.dll", "dxwrapper.dll", "dxwrapper.ini")
    }
    if output.exists():
        raise ValueError(f"output already exists; choose a fresh directory: {output}")
    if output.is_relative_to(source):
        raise ValueError("output must be outside the original game directory")

    assert image[0x2DE39:0x2DE3E] == bytes.fromhex("e9 62 fd ff ff")
    image[0x2DE39:0x2DE3E] = b"\x90" * 5
    if args.console:
        struct.pack_into("<I", image, 534691, 1)
    if args.resolution is not None:
        width, height = args.resolution
        for offset, value in (
            (0x82A44, width), (0x82A48, height),
            (0x29714, width - 100), (0x2972D, height - 100),
        ):
            struct.pack_into("<I", image, offset, value)

    output.mkdir(parents=True)
    for item in source.iterdir():
        if item.is_file():
            destination = output / item.name
            shutil.copyfile(item, destination)
            destination.chmod(0o644)
    (output / "BEASTS.EXE").write_bytes(image)
    native_assets = write_assets(output, image)
    for name, data in resource_data.items():
        (output / name).write_bytes(data)

    profile = {
        "original_sha256": BASE_SHA256,
        "patched_sha256": hashlib.sha256(image).hexdigest(),
        "native_assets_sha256": hashlib.sha256(native_assets.read_bytes()).hexdigest(),
        "bb_patch_revision": PATCH_REVISION,
        "language": args.language,
        "console": args.console,
        "resolution": list(args.resolution) if args.resolution else [640, 480],
        "resources": {name: hashlib.sha256(data).hexdigest()
                      for name, data in resource_data.items()},
    }
    (output / "bb-reference.json").write_text(json.dumps(profile, indent=2) + "\n")
    (output / "bumpkins.ini").write_text(
        "[reference]\n" + f"language={LANGUAGES.index(args.language)}\n"
    )
    print(f"Prepared {output}")
    print(f"Patched executable SHA256: {profile['patched_sha256']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "original" / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "work" / "patched")
    parser.add_argument("--patch-source", type=Path,
                        default=Path(os.environ.get("BB_PATCH_SOURCE", ROOT / "reference" / "bb-patch")))
    parser.add_argument("--language", choices=LANGUAGES, default="english")
    parser.add_argument("--console", action=argparse.BooleanOptionalAction, default=True)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--resolution", type=resolution, default=None)
    group.add_argument("--original-resolution", dest="resolution", action="store_const", const=None)
    args = parser.parse_args()
    try:
        prepare(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Cannot prepare reference: {error}\n")


if __name__ == "__main__":
    main()
