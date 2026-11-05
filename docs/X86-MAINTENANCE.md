# x86 maintenance fork

This fork tracks `osm0sis/PlayIntegrityFork`. Its initial maintenance branch is
`maintenance/x86`, based on upstream commit
`2dfc57ce38e1a2ecd3daf3d893e9bf3e87b33acb`.

`main` remains an upstream synchronization branch. Work on the native x86 port
belongs on the maintenance branch. Preserve upstream attribution and the existing
GPL-3.0 license. This branch implements an embedded Gum backend for x86/x86_64.
Both native architectures have been built and tested on Waydroid; the x86_64
module has also been installed and its hook verified in the actual GMS process.
This does not mean the device passes Device or Strong integrity.

## Current validation (2026-11-05)

- x86 and x86_64 native tests: 24,003 callbacks each, zero failures, including
  null/reused cookies, nested reads and eight concurrent threads.
- GMS logs confirm `Property hook active: backend=gum`; selected Java Build
  fields agree with the callback-based native property reads.
- ARM64 ShadowHook backend cross-compiles; ARM runtime behavior has not been
  tested on this x86 device.
- One rate-limited service test after installation still returned Basic only.
  Device/Strong remain unpassed and the exact server rejection reason is unknown.
- A subsequent local probe found that the legacy `__system_property_get` API
  still exposes different values from the intercepted callback API. Coverage of
  additional read paths is under investigation; do not mistake selected-field
  alignment for complete property API consistency.

The native backend uses Gum's fast replacement API because the default
replacement recursion guard skipped nested reads in the initial test. Each read
uses a stack callback context instead of the upstream global callback registry.
The backend and original-function trampoline remain loaded for process lifetime.

## Why a port is needed

Upstream v18 replaced Dobby with ShadowHook to address newer GMS detection of
Dobby affecting <A13 integrity checks, Wallet and RCS. The release notes explicitly
state that this removed x86 and x86_64 support. The dependency pinned by the
current CMake build, ShadowHook v2.0.1, implements ARM and ARM64 backends only.
Adding ABI filters cannot supply its missing x86 instruction relocation backend.

An Android ARM Native Bridge does not make an ARM Zygisk module suitable for an
x86_64 Zygote or turn ARM inline hooks into hooks of x86_64 libc instructions.

## Candidate backend

Frida Gum is a candidate inline hook backend. The official 17.25.2 release
provides Android x86 and x86_64 devkits containing a header, static library and
standalone example. Using embedded Gum does not require frida-server.

The Android x86_64 devkit has been downloaded separately and checked against its
official release digest:

```text
asset: frida-gum-devkit-17.25.2-android-x86_64.tar.xz
sha256: 8ae298fbecab410b38d615d14187689546017d26c0e5304a484aa71162d4bb98
```

This verifies the artifact, not its compatibility with this module or GMS.
Neither Gum nor any other hook backend is assumed to avoid detection or produce
a passing integrity verdict. Before adopting Gum, review its license, static
linking obligations, Android API requirements and module unload lifecycle.

ByteHook also publishes x86 and x86_64 libraries, but implements PLT hooking.
It is not an equivalent replacement for intercepting a function entry reached
through arbitrary function pointers. Dobby supports these architectures but
reintroducing it alone does not resolve the compatibility issue under review.

## Implementation sequence

1. Build an isolated Android x86_64 executable that intercepts
   `__system_property_read_callback` and safely calls the original function.
   Verify ordinary and null callback cookies, repeated calls and concurrency.
2. Separate the module's hook backend from its property mapping code. Retain
   upstream ShadowHook behavior for ARM while adding the validated x86 backend.
3. Handle hook failure explicitly and validate initialization, original-function
   trampoline lifetime, module unload behavior and callback concurrency.
4. Add x86_64, then x86 ABI builds and check the ELF machine type of every
   packaged `zygisk/<abi>.so`. Packaging must never relabel an ARM binary as x86.
5. Confirm Java and native values in the actual target process and inspect
   runtime logs before conducting any server-side integrity test.

Compilation, correct hooking, stable GMS operation and a server-accepted Device
verdict are separate milestones. No server check belongs in CI or an automatic
retry loop.

## Build baseline

Initialize the pinned submodules:

```sh
git submodule update --init --recursive
```

The inherited build currently specifies JDK 21, Android API/build tools 37,
NDK `30.0.16248370`, CMake `3.22.1`, and the Gradle wrapper in this repository:

```sh
./gradlew --warning-mode all assembleRelease
```

These are the inherited Gradle build requirements. The complete Gradle pipeline
has not been validated locally. A separate, validated native/dex build avoids
requiring AGP or an entire Android SDK installation:

```sh
python tools/build-native.py \
  --ndk /path/to/android-ndk-r30 \
  --android-jar /path/to/android-33/android.jar \
  --r8 /path/to/r8.jar
python tools/test-native.py --serial DEVICE_SERIAL
```

The local validation used NDK 30.0.16248370, Android API 33 stubs, D8 8.6.27,
and HiddenApiBypass 6.2. Gum devkits and the default HiddenApiBypass AAR are
checksum-pinned. The native test runner never requests an integrity token.
The build defaults to x86 and x86_64; use repeated `--abi` options to select
other architectures. Its ZIP is written to `out/PlayIntegrityFork-x86-maintenance.zip`
with a build manifest and verified ELF architecture for every included library.

Before distributing a maintenance build, give it clear fork provenance and
review `module/module.prop` and update metadata: their inherited upstream update
URL must not silently replace a maintained x86 build with an ARM-only release.
Do not advertise or publish an x86 ZIP before validating its actual contents.

## Repository workflow

`origin` is this fork; `upstream` is `osm0sis/PlayIntegrityFork`.

```sh
git fetch upstream --tags
git switch main
git merge --ff-only upstream/main
git push origin main
git switch maintenance/x86
git merge main
```

Keep upstream changes and port changes reviewable. Resolve divergence before
updating `main`; do not force-push it to discard work.

Keep credentials, keyboxes, private certificates, device identifiers, account
data and raw device logs outside this public repository. Commit source,
synthetic test inputs and redacted findings only. Retain rollback copies outside
the repository before replacing a module on a device.

## Primary sources

- [PIF v18 release](https://github.com/osm0sis/PlayIntegrityFork/releases/tag/v18)
- [ShadowHook v2.0.1](https://github.com/bytedance/android-inline-hook/tree/v2.0.1)
- [Frida 17.25.2 release](https://github.com/frida/frida/releases/tag/17.25.2)
- [Frida Gum](https://github.com/frida/frida-gum)
- [ByteHook](https://github.com/bytedance/bhook)
