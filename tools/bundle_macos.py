#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle Homebrew dylibs and their notices, then verify a relocated runtime."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True)


def dependencies(path):
    return [line.strip().split(' (', 1)[0]
            for line in run('otool', '-L', str(path)).splitlines()[1:]]


def system_library(name):
    return name.startswith(('/usr/lib/', '/System/Library/'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, required=True)
    args = parser.parse_args()
    directory = args.directory.resolve()
    libraries = directory / 'lib'
    libraries.mkdir(exist_ok=True)
    bundled = {}

    def bundle(source, name=None):
        source = Path(source)
        actual = source.resolve(strict=True)
        if actual in bundled:
            return bundled[actual]
        name = name or source.name
        target = libraries / name
        if target.exists():
            raise RuntimeError(f'conflicting library basename: {name}')
        shutil.copy2(actual, target)
        target.chmod(0o755)
        bundled[actual] = name
        parts = actual.parts
        if 'Cellar' not in parts:
            raise RuntimeError(f'no Homebrew license origin for {name}')
        index = parts.index('Cellar')
        keg = Path(*parts[:index + 3])
        notices = [p for p in keg.iterdir() if p.is_file() and
                   p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE'))]
        if not notices:
            raise RuntimeError(f'missing library notices for {name}')
        destination = directory / 'LICENSES' / parts[index + 1]
        destination.mkdir(parents=True, exist_ok=True)
        for notice in notices:
            shutil.copy2(notice, destination / notice.name)
        # SDL2-compat opens SDL3 dynamically, so otool alone cannot find it.
        # Its macOS loader searches for libSDL3.dylib beside its own library.
        if 'sdl2-compat' in parts:
            prefix = Path(run('brew', '--prefix', 'sdl3').strip())
            bundle(prefix / 'lib/libSDL3.dylib', 'libSDL3.dylib')
        for dependency in dependencies(actual)[1:]:  # first entry is its ID
            if system_library(dependency):
                continue
            child = Path(dependency)
            if not child.is_absolute():
                raise RuntimeError(f'unresolved dylib dependency: {dependency}')
            child_name = bundle(child)
            subprocess.run(['install_name_tool', '-change', dependency,
                            '@loader_path/' + child_name, str(target)], check=True)
        subprocess.run(['install_name_tool', '-id', '@loader_path/' + name,
                        str(target)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
        return name

    def relocate(executable):
        executable.chmod(0o755)
        for dependency in dependencies(executable):
            if system_library(dependency):
                continue
            source = Path(dependency)
            if not source.is_absolute():
                raise RuntimeError(f'unresolved executable dependency: {dependency}')
            name = bundle(source)
            subprocess.run(['install_name_tool', '-change', dependency,
                            '@executable_path/lib/' + name, str(executable)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', str(executable)], check=True)
        subprocess.run(['codesign', '--verify', '--strict', str(executable)], check=True)

    relocate(directory / 'bumpkins')
    probe = directory / 'runtime-smoke-check'
    try:
        shutil.copy2(args.test_binary, probe)
        relocate(probe)
        environment = dict(os.environ, SDL_VIDEODRIVER='dummy',
                           SDL_AUDIODRIVER='dummy', DYLD_PRINT_LIBRARIES='1',
                           DYLD_FALLBACK_LIBRARY_PATH='')
        environment.pop('DYLD_LIBRARY_PATH', None)
        result = subprocess.run([str(probe)], env=environment,
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        if '/opt/homebrew/' in result.stderr or '/usr/local/' in result.stderr:
            raise RuntimeError('packaged runtime still loaded a Homebrew library')
        if str(libraries) not in result.stderr:
            raise RuntimeError('dyld did not report the bundled libraries')
        print(result.stdout.strip())
        print(f'Bundled {len(bundled)} libraries; relocated runtime uses no Homebrew dylibs')
    finally:
        probe.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
