# x86 maintenance fork

This fork tracks `osm0sis/PlayIntegrityFork`. Its initial maintenance branch is
`maintenance/x86`, based on upstream commit
`2dfc57ce38e1a2ecd3daf3d893e9bf3e87b33acb`.

`main` remains an upstream synchronization branch. Work on the native x86 port
belongs on the maintenance branch. Preserve upstream attribution and the existing
GPL-3.0 license. This branch currently contains maintenance documentation only;
it does not yet restore x86 support or provide a tested module ZIP.

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

These are source-declared requirements, not a claim that the fork has built
successfully. At initialization no Android NDK was found in the local standard
SDK locations. An SDK/NDK must be provisioned before native build validation.

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
