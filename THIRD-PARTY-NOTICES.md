# Native backend dependencies

The module remains licensed under the repository's GPL-3.0 license. Upstream
copyright notices in its sources and submodules are retained.

The x86 and x86_64 backend links the official Frida Gum 17.25.2 Android devkit.
Gum uses the wxWindows Library Licence 3.1 (GNU Library GPL with an exception).
The upstream license text is in `licenses/frida-gum-COPYING`; its referenced GNU
Library GPL 2.0 text is in `licenses/GNU-Library-GPL-2.0.txt`.

Source and build instructions are available in the corresponding upstream tag:

- https://github.com/frida/frida-gum/tree/17.25.2
- https://github.com/frida/frida/tree/17.25.2

The official static devkit includes transitive dependencies with their own
licenses, including GLib and Capstone. Preserve the corresponding source and
dependency license notices when redistributing binary builds. Local build
artifacts in `out/` are not published automatically by this fork.

ARM builds retain ShadowHook v2.0.1 and its original license notices. The JSON
and local C++ atexit implementation remain pinned submodules with their original
licenses. The dex build includes upstream HiddenApiBypass 6.2; its source and
license are available at https://github.com/LSPosed/AndroidHiddenApiBypass.
