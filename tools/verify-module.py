#!/usr/bin/env python3
"""Check a packaged Zygisk module without installing or executing its code."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

ARCHITECTURES = {'x86': (1, 3), 'x86_64': (2, 62),
                 'armeabi-v7a': (1, 40), 'arm64-v8a': (2, 183)}


def readelf_path(explicit):
    if explicit:
        return str(explicit)
    for executable in ['llvm-readelf', 'readelf']:
        if path := shutil.which(executable):
            return path
    candidates = []
    for variable in ['ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT', 'ANDROID_NDK']:
        if value := os.environ.get(variable):
            candidates.extend(Path(value).glob('toolchains/llvm/prebuilt/*/bin/llvm-readelf'))
    for variable in ['ANDROID_HOME', 'ANDROID_SDK_ROOT']:
        if value := os.environ.get(variable):
            candidates.extend(Path(value).glob('ndk/*/toolchains/llvm/prebuilt/*/bin/llvm-readelf'))
    if candidates:
        return str(sorted(candidates)[-1])
    raise ValueError('An ELF inspector is required; pass --readelf or configure the NDK')


def verify(path, abis, inspector):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        duplicates = [name for name, count in Counter(names).items() if count > 1]
        if duplicates:
            raise ValueError('Duplicate ZIP entries: ' + ', '.join(duplicates))
        for name in names:
            item = PurePosixPath(name)
            if item.is_absolute() or '..' in item.parts or '\\' in name:
                raise ValueError('Unsafe ZIP path: ' + name)
            base = item.name.lower()
            if base.startswith(('custom.', 'keybox')) or base in ['pif.prop', 'pif.json']:
                raise ValueError('Private/device-specific input packaged: ' + name)
        required = ['module.prop', 'classes.dex', 'customize.sh', 'LICENSE',
                    'THIRD-PARTY-NOTICES.md', 'licenses/frida-gum-COPYING',
                    'licenses/GNU-Library-GPL-2.0.txt']
        for name in required:
            if name not in names or not archive.read(name):
                raise ValueError('Missing/empty package input: ' + name)
        if not archive.read('classes.dex').startswith(b'dex\n'):
            raise ValueError('classes.dex does not have a DEX header')
        properties = dict(line.split('=', 1) for line in archive.read('module.prop').decode().splitlines()
                          if line and not line.startswith('#') and '=' in line)
        if properties.get('id') != 'playintegrityfix':
            raise ValueError('Unexpected module ID')
        if 'updateJson' in properties:
            raise ValueError('Fork package must not auto-update to an upstream ARM-only build')
        entries = {name.removeprefix('zygisk/').removesuffix('.so'): name for name in names
                   if name.startswith('zygisk/') and name.endswith('.so')}
        if set(entries) != set(abis):
            raise ValueError(f'ABI set mismatch: expected {sorted(abis)}, got {sorted(entries)}')
        report = {'module_version': properties['version'], 'architectures': {}}
        for abi, name in entries.items():
            blob = archive.read(name)
            elf_class, machine = ARCHITECTURES[abi]
            if len(blob) < 64 or blob[:4] != b'\x7fELF' or blob[4] != elf_class or blob[5] != 1:
                raise ValueError('Invalid ELF identity for ' + abi)
            if struct.unpack_from('<HH', blob, 16) != (3, machine):
                raise ValueError('Wrong ELF type/machine for ' + abi)
            with tempfile.TemporaryDirectory() as temporary:
                binary = Path(temporary)/(abi + '.so')
                binary.write_bytes(blob)
                symbols = subprocess.check_output([inspector, '--wide', '--dyn-syms', str(binary)], text=True)
                for entry in ['zygisk_module_entry', 'zygisk_companion_entry']:
                    if not any(line.split()[-1:] == [entry] and ' UND ' not in line
                               for line in symbols.splitlines()):
                        raise ValueError('Missing defined entry ' + entry + ' in ' + abi)
                if re.search(r'\bDobby(?:Hook|SymbolResolver|CodePatch)\b', symbols):
                    raise ValueError('Unexpected Dobby exports in ' + abi)
            report['architectures'][abi] = {'elf_machine': machine, 'bytes': len(blob),
                                            'module_and_companion_entries': True}
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--abi', action='append', choices=ARCHITECTURES)
    parser.add_argument('--readelf', type=Path)
    args = parser.parse_args()
    report = verify(args.package, args.abi or ['x86', 'x86_64'], readelf_path(args.readelf))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
