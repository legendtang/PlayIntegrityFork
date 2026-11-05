#!/usr/bin/env python3
"""Build source dex/native modules with an explicit NDK and Android API jar.

Avoids Gradle/AGP provisioning for a module that only needs a dex and Zygisk SOs.
Dependencies are pinned; no device operations or Integrity requests are made.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MACHINES = {'x86': 3, 'x86_64': 62, 'armeabi-v7a': 40, 'arm64-v8a': 183}


def run(args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', required=True, type=Path)
    parser.add_argument('--android-jar', required=True, type=Path)
    parser.add_argument('--r8', required=True, type=Path)
    parser.add_argument('--hidden-api-jar', type=Path)
    parser.add_argument('--abi', action='append', choices=MACHINES)
    parser.add_argument('--gum-cache', type=Path, help='Version-specific cache with <abi>/ devkits')
    args = parser.parse_args()
    abis = args.abi or ['x86', 'x86_64']
    ndk = args.ndk.resolve()
    output = ROOT/'build/native-maintenance'
    output.mkdir(parents=True, exist_ok=True)
    toolchain = ndk/'build/cmake/android.toolchain.cmake'
    strip = ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip'
    for path in [toolchain, strip, args.android_jar, args.r8]:
        if not path.is_file():
            parser.error('Missing build input: ' + str(path))
    hidden = args.hidden_api_jar
    if hidden is None:
        aar = output/'hiddenapibypass-6.2.aar'
        digest = 'dcdbd6fc8f70d722b06700cbe30888ded5d329bc3088d3ad92deddbdc8895db3'
        if not aar.exists():
            url = 'https://repo.maven.apache.org/maven2/org/lsposed/hiddenapibypass/hiddenapibypass/6.2/hiddenapibypass-6.2.aar'
            with urllib.request.urlopen(url, timeout=60) as response:
                blob = response.read(8 * 1024 * 1024)
            if hashlib.sha256(blob).hexdigest() != digest:
                raise RuntimeError('HiddenApiBypass checksum mismatch')
            aar.write_bytes(blob)
        if hashlib.sha256(aar.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Cached HiddenApiBypass checksum mismatch')
        hidden = output/'hiddenapibypass-6.2.jar'
        with zipfile.ZipFile(aar) as archive:
            hidden.write_bytes(archive.read('classes.jar'))
    classes = output/'classes'
    # Each invocation compiles into a fresh directory to exclude stale classes.
    with tempfile.TemporaryDirectory(dir=output, prefix='javac-') as temporary:
        sources = sorted((ROOT/'app/src/main/java').rglob('*.java'))
        run(['javac', '--release', '8', '-Xlint:-options', '-cp',
             str(args.android_jar.resolve()) + os.pathsep + str(hidden.resolve()),
             '-d', temporary, *sources])
        jar = output/'source-classes.jar'
        with zipfile.ZipFile(jar, 'w') as archive:
            for source in sorted(Path(temporary).rglob('*.class')):
                archive.write(source, source.relative_to(temporary))
    dex = output/'dex'
    dex.mkdir(exist_ok=True)
    run(['java', '-cp', args.r8.resolve(), 'com.android.tools.r8.D8', '--release',
         '--min-api', '26', '--lib', args.android_jar.resolve(), '--output', dex,
         jar, hidden.resolve()])
    manifest = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'worktree_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
                'ndk': (ndk/'source.properties').read_text(), 'abis': {},
                'build_inputs_sha256': {str(p.name): hashlib.sha256(p.read_bytes()).hexdigest()
                                        for p in [args.r8, args.android_jar, hidden]}}
    # Staging is temporary so removed/unsupported ABIs cannot leak into a ZIP.
    with tempfile.TemporaryDirectory(dir=output, prefix='module-') as temporary:
        stage = Path(temporary)
        shutil.copytree(ROOT/'module', stage, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('zygisk', 'classes.dex', 'custom.pif.*', 'pif.json'))
        shutil.copy2(dex/'classes.dex', stage/'classes.dex')
        (stage/'zygisk').mkdir()
        for abi in abis:
            build = output/abi
            configure = ['cmake', '-S', ROOT/'app/src/main/cpp', '-B', build, '-G', 'Ninja',
                         '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain), '-DANDROID_ABI='+abi,
                         '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_static',
                         '-DPIF_USE_PREFAB_CXX=OFF', '-DPIF_BUILD_NATIVE_TESTS=ON',
                         '-DCMAKE_BUILD_TYPE=Release']
            if args.gum_cache and abi in ['x86', 'x86_64']:
                configure.append('-DPIF_GUM_ROOT='+str((args.gum_cache/abi).resolve()))
            run(configure)
            run(['cmake', '--build', build, '-j', '6'])
            source = build/'libplayintegrityfix.so'
            header = source.read_bytes()[:20]
            if header[:4] != b'\x7fELF' or struct.unpack_from('<H', header, 18)[0] != MACHINES[abi]:
                raise RuntimeError('Incorrect ELF architecture for ' + abi)
            library = stage/'zygisk'/(abi+'.so')
            shutil.copy2(source, library)
            run([strip, '--strip-unneeded', library])
            manifest['abis'][abi] = {'elf_machine': MACHINES[abi],
                                     'sha256': hashlib.sha256(library.read_bytes()).hexdigest()}
        (stage/'build-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        shutil.copy2(ROOT/'LICENSE', stage/'LICENSE')
        if (ROOT/'THIRD-PARTY-NOTICES.md').exists():
            shutil.copy2(ROOT/'THIRD-PARTY-NOTICES.md', stage/'THIRD-PARTY-NOTICES.md')
        if (ROOT/'licenses').exists():
            shutil.copytree(ROOT/'licenses', stage/'licenses')
        destination = ROOT/'out'
        destination.mkdir(exist_ok=True)
        archive_path = destination/'PlayIntegrityFork-x86-maintenance.zip'
        with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for file in sorted(stage.rglob('*')):
                if file.is_file():
                    archive.write(file, file.relative_to(stage))
    (output/'build-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('MODULE_ZIP', archive_path)


if __name__ == '__main__':
    main()
