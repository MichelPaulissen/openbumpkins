#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract user-owned embedded resources for the native C runtime."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile

BASE_SHA256 = 'b30d7d0762dbd808407f0d95c6fdafa0b52ce67d0b0f20c8fbb7fa66a2213e89'
ASSET_NAME = 'bumpkins-assets.bin'
MAGIC = b'BBASSET1'
HEADER = struct.Struct('<8s4I')
RECORD = struct.Struct('<4I32s')


def extract(image):
    """Accept the supported original or pinned patch profiles; retain no code."""
    if len(image) != 916992:
        raise ValueError('unsupported BEASTS.EXE size')
    u16 = lambda offset: struct.unpack_from('<H', image, offset)[0]
    u32 = lambda offset: struct.unpack_from('<I', image, offset)[0]
    width, height = u32(0x82a44), u32(0x82a48)
    console = u32(534691)
    if width < 100 or height < 100 or console not in (0, 1):
        raise ValueError('unsupported resolution or console setting')
    if image[0x2de39:0x2de3e] not in (bytes.fromhex('e962fdffff'), b'\x90'*5):
        raise ValueError('unsupported patch profile')
    if (u32(0x29714), u32(0x2972d)) != (width-100, height-100):
        raise ValueError('inconsistent resolution patch')
    normalized = bytearray(image)
    normalized[0x2de39:0x2de3e] = bytes.fromhex('e962fdffff')
    for offset, value in [(534691, 0), (0x82a44, 640), (0x82a48, 480),
                          (0x29714, 540), (0x2972d, 380)]:
        struct.pack_into('<I', normalized, offset, value)
    if hashlib.sha256(normalized).hexdigest() != BASE_SHA256:
        raise ValueError('BEASTS.EXE is not the supported original/pinned patch')
    nt = u32(0x3c)
    section_table = nt + 24 + u16(nt+20)
    sections = [struct.unpack_from('<4I', image, section_table+40*n+8)
                for n in range(u16(nt+6))]

    def file_offset(rva):
        for virtual_size, start, raw_size, raw_start in sections:
            if start <= rva < start+raw_size:
                return raw_start+rva-start
        raise ValueError('resource RVA outside file data')

    resource_rva = u32(nt+24+112)
    resource_root = file_offset(resource_rva)

    def key(value):
        if value & 0x80000000:
            offset = resource_root+(value & 0x7fffffff)
            length = u16(offset)
            return image[offset+2:offset+2+length*2].decode('utf-16le')
        return value

    def children(directory):
        offset = resource_root+directory
        count = u16(offset+12)+u16(offset+14)
        return [struct.unpack_from('<II', image, offset+16+8*n)
                for n in range(count)]

    resources = []
    for type_id, directory in children(0):
        if type_id & 0x80000000:
            raise ValueError('unsupported named resource type')
        for name_id, languages in children(directory & 0x7fffffff):
            variants = children(languages & 0x7fffffff)
            if len(variants) != 1:
                raise ValueError('expected one language per resource')
            language, data_entry = variants[0]
            rva, size = struct.unpack_from('<II', image, resource_root+data_entry)
            offset = file_offset(rva)
            data = image[offset:offset+size]
            if len(data) != size:
                raise ValueError('truncated resource')
            resources.append((type_id, key(name_id), data))
    payload = bytearray()
    table = bytearray()
    start = HEADER.size+RECORD.size*len(resources)
    for kind, name, data in resources:
        named = isinstance(name, str)
        text = name.encode('ascii') if named else b''
        if len(text) >= 32:
            raise ValueError('resource name too long')
        table += RECORD.pack(kind, 0xffffffff if named else name,
                             start+len(payload), len(data), text)
        payload += data
        payload += bytes(-len(payload) % 4)
    return HEADER.pack(MAGIC, width, height, console, len(resources))+table+payload


def write_assets(directory, image=None):
    directory = Path(directory)
    if image is None:
        image = (directory/'BEASTS.EXE').read_bytes()
    data = extract(image)
    destination = directory/ASSET_NAME
    with tempfile.NamedTemporaryFile(dir=directory, prefix='.bumpkins-assets-',
                                     delete=False) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    try:
        temporary_path.chmod(0o644)
        os.replace(temporary_path, destination)
    finally:
        temporary_path.unlink(missing_ok=True)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, default=Path('work/patched'))
    args = parser.parse_args()
    try:
        destination = write_assets(args.game_dir)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'Cannot extract native assets: {error}\n')
    print(f'Extracted native resources to {destination}')


if __name__ == '__main__':
    main()
