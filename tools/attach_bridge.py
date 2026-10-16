#!/usr/bin/env python3
"""Load the C bridge through an extra PE import, preserving original import addresses."""
# SPDX-License-Identifier: GPL-3.0-only

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import pefile


def align(value, boundary):
    return (value + boundary - 1) & ~(boundary - 1)


def attach(args):
    source = args.source.resolve()
    output = args.output.resolve()
    dll = args.dll.resolve()
    profile = json.loads((source / "bb-reference.json").read_text())
    image = bytearray((source / "BEASTS.EXE").read_bytes())
    if hashlib.sha256(image).hexdigest() != profile["patched_sha256"]:
        raise ValueError("source executable does not match its reference manifest")
    bridge = pefile.PE(str(dll))
    if bridge.FILE_HEADER.Machine != 0x14C or not bridge.FILE_HEADER.Characteristics & 0x2000:
        raise ValueError("bridge must be an i386 DLL")
    if b"bb_bridge_start" not in {s.name for s in bridge.DIRECTORY_ENTRY_EXPORT.symbols}:
        raise ValueError("bridge DLL does not export bb_bridge_start")
    if output.exists() or output.is_relative_to(source):
        raise ValueError("choose a fresh output directory outside the source directory")

    pe = pefile.PE(data=image)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        raise ValueError("expected the pinned i386 executable at image base 0x400000")
    section_header = pe.sections[-1].get_file_offset() + 40
    if section_header + 40 > pe.OPTIONAL_HEADER.SizeOfHeaders or any(image[section_header:section_header + 40]):
        raise ValueError("no room for an additional PE section header")

    rva = align(pe.OPTIONAL_HEADER.SizeOfImage, pe.OPTIONAL_HEADER.SectionAlignment)
    blob = bytearray()
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        start = entry.struct.get_file_offset()
        blob += image[start:start + 20]
    descriptor = len(blob)
    blob += bytes(40)  # New descriptor and the terminating descriptor.
    descriptor_size = len(blob)
    name = len(blob)
    blob += b"bumpkins_bridge.dll\0"
    blob += bytes(align(len(blob), 2) - len(blob))
    symbol = len(blob)
    blob += b"\0\0bb_bridge_start\0"
    blob += bytes(align(len(blob), 4) - len(blob))
    lookup = len(blob)
    blob += struct.pack("<II", rva + symbol, 0)
    iat = len(blob)
    blob += struct.pack("<II", rva + symbol, 0)
    struct.pack_into("<IIIII", blob, descriptor, rva + lookup, 0, 0, rva + name, rva + iat)

    raw = align(len(image), pe.OPTIONAL_HEADER.FileAlignment)
    raw_size = align(len(blob), pe.OPTIONAL_HEADER.FileAlignment)
    struct.pack_into("<8sIIIIIIHHI", image, section_header, b".bbhook\0", len(blob), rva,
                     raw_size, raw, 0, 0, 0, 0, 0xC0000040)
    struct.pack_into("<H", image, pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections"),
                     pe.FILE_HEADER.NumberOfSections + 1)
    struct.pack_into("<I", image, pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfImage"),
                     align(rva + len(blob), pe.OPTIONAL_HEADER.SectionAlignment))
    struct.pack_into("<I", image, pe.OPTIONAL_HEADER.get_field_absolute_offset("SizeOfInitializedData"),
                     pe.OPTIONAL_HEADER.SizeOfInitializedData + raw_size)
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    struct.pack_into("<II", image, directory.get_file_offset(), rva, descriptor_size)
    image += bytes(raw - len(image)) + blob + bytes(raw_size - len(blob))
    checksum = pefile.PE(data=image).generate_checksum()
    struct.pack_into("<I", image, pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum"), checksum)

    shutil.copytree(source, output)
    (output / "BEASTS.EXE").write_bytes(image)
    shutil.copyfile(dll, output / "bumpkins_bridge.dll")
    profile["bridged_sha256"] = hashlib.sha256(image).hexdigest()
    profile["bridge_dll_sha256"] = hashlib.sha256(dll.read_bytes()).hexdigest()
    profile["reimplemented_functions"] = ["0x00406a60"]
    (output / "bb-reference.json").write_text(json.dumps(profile, indent=2) + "\n")
    print(f"Prepared hybrid executable in {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("work/patched"))
    parser.add_argument("--output", type=Path, default=Path("work/bridged"))
    parser.add_argument("--dll", type=Path, default=Path("build/windows/bumpkins_bridge.dll"))
    args = parser.parse_args()
    try:
        attach(args)
    except (OSError, ValueError, KeyError, pefile.PEFormatError) as error:
        parser.exit(1, f"Cannot attach bridge: {error}\n")


if __name__ == "__main__":
    main()
