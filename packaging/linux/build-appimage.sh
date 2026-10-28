#!/usr/bin/env bash
# Builds dist/launchme-<version>-x86_64.AppImage and dist/launchme-<version>-x86_64.AppImage.sha256
# (docs/linux-port.md, phase 5): a Release build installed into an AppDir (cmake --install, the same
# layout as a package: usr/lib/launchme with usr/bin/launchme linked to it), its libraries deployed by
# linuxdeploy, packed by linuxdeploy-plugin-appimage.
#
#   packaging/linux/build-appimage.sh            # from the repository, on x86-64
#
# Environment: BUILD_DIR (default build/appimage), OUT_DIR (default dist), TOOLS_DIR (where the pinned
# linuxdeploy tools are kept, default $BUILD_DIR/tools), CMAKE_ARGS (more configure options).
#
# Publishing: attach the AppImage to the GitHub release vX.Y.Z under exactly this name. The updater
# (src/platform/updater.cpp) looks for launchme-<tag without v>-x86_64.AppImage and checks its SHA-256
# against the asset's digest, or against a "SHA-256: `<hex>`" line in the release notes (printed below).
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
build=${BUILD_DIR:-$root/build/appimage}
out=${OUT_DIR:-$root/dist}
tools=${TOOLS_DIR:-$build/tools}
app_id=io.github.uxandai.Launchme

die() { echo "build-appimage: $*" >&2; exit 1; }

[ "$(uname -m)" = x86_64 ] || die "x86-64 only (the updater installs launchme-<version>-x86_64.AppImage)"
version=$(sed -n 's/^project(launchme VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
[ -n "$version" ] || die "no project version in CMakeLists.txt"
name="launchme-$version-x86_64.AppImage"

# Pinned tools, checked before they run.
fetch() { # url sha256 file
    local file="$tools/$3"
    if [ -f "$file" ] && echo "$2  $file" | sha256sum -c --quiet - 2>/dev/null; then chmod +x "$file"; return; fi
    mkdir -p "$tools"
    curl -fL --retry 3 -o "$file.part" "$1"
    echo "$2  $file.part" | sha256sum -c --quiet - || die "checksum mismatch for $1"
    mv "$file.part" "$file"
    chmod +x "$file"
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d linuxdeploy-x86_64.AppImage
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-appimage-x86_64.AppImage \
    992d502a248e14ab185448ddf6f6e7d25558cb84d4623c354c3af350c25fccb3 linuxdeploy-plugin-appimage-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1 # The tools run without FUSE (containers, CI).
# A current patchelf for linuxdeploy (PATCHELF) and for the fixes below: the one inside linuxdeploy
# (0.15) breaks libraries linked with packed relative relocations (DT_RELR, default on newer
# distributions), and the AppImage then crashes in the dynamic loader.
fetch https://github.com/NixOS/patchelf/releases/download/0.19.1/patchelf-0.19.1-x86_64.tar.gz \
    a6818fef80128fb354423234ecacdcca3e993913d774e5d8346bc63f70fed4cf patchelf-0.19.1-x86_64.tar.gz
if [ ! -x "$tools/patchelf/bin/patchelf" ] || [ "$tools/patchelf-0.19.1-x86_64.tar.gz" -nt "$tools/patchelf/bin/patchelf" ]; then
    rm -rf "$tools/patchelf"
    mkdir -p "$tools/patchelf"
    tar -xzf "$tools/patchelf-0.19.1-x86_64.tar.gz" -C "$tools/patchelf" ./bin/patchelf
    touch "$tools/patchelf/bin/patchelf"
fi
patchelf=$tools/patchelf/bin/patchelf
export PATCHELF=$patchelf
linuxdeploy=$tools/linuxdeploy-x86_64.AppImage
# Distribution libraries come stripped already, and linuxdeploy's own strip fails on newer ones
# (.relr.dyn sections, e.g. on Arch).
export NO_STRIP=${NO_STRIP:-1}

# shellcheck disable=SC2086 # CMAKE_ARGS is a list of options.
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF ${CMAKE_ARGS:-}
cmake --build "$build"

appdir="$build/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --component Runtime
program="$appdir/usr/lib/launchme"

# The reader window (launchme-web) uses the system's WebKitGTK 6.0 and GTK 4: WebKit starts its own
# helper processes from fixed system paths and cannot be bundled. It stays out of linuxdeploy, and
# GLib (which it shares with the system's WebKit) is not bundled either. Without a usable WebKitGTK
# the launcher opens pages in the default browser.
web_aside=
if [ -f "$program/launchme-web" ]; then
    web_aside="$build/launchme-web.aside"
    mv "$program/launchme-web" "$web_aside"
fi
"$linuxdeploy" --appdir "$appdir" \
    --executable "$program/launchme" \
    --exclude-library 'libglib-2.0.so*' --exclude-library 'libgobject-2.0.so*' \
    --exclude-library 'libgio-2.0.so*' --exclude-library 'libgmodule-2.0.so*'
# linuxdeploy takes usr/lib/launchme for a library folder and gives the program $ORIGIN only: it
# needs usr/lib (the deployed libraries) too, after ONNX Runtime next to it.
# shellcheck disable=SC2016 # $ORIGIN is for the loader.
"$patchelf" --set-rpath '$ORIGIN:$ORIGIN/..' "$program/launchme"
if [ -n "$web_aside" ]; then
    mv "$web_aside" "$program/launchme-web"
    # The window's toolkit from the system; ICU, curl and the like from the AppImage when the system
    # has other versions.
    # shellcheck disable=SC2016
    "$patchelf" --set-rpath '$ORIGIN/..' "$program/launchme-web"
fi
[ -e "$appdir/AppRun" ] || ln -s usr/bin/launchme "$appdir/AppRun"

mkdir -p "$out"
rm -f "$out/$name"
LDAI_OUTPUT="$out/$name" OUTPUT="$out/$name" LINUXDEPLOY_OUTPUT_VERSION="$version" \
    "$tools/linuxdeploy-plugin-appimage-x86_64.AppImage" --appdir "$appdir"
[ -f "$out/$name" ] || die "linuxdeploy-plugin-appimage wrote no $name"
chmod +x "$out/$name"

# What the updater checks before it installs the file (ELF x86-64 with the AppImage type 2 mark).
head -c 11 "$out/$name" | od -An -tx1 | grep -q '7f 45 4c 46 02 01 01 .. 41 49 02' || die "$name is not a type 2 AppImage"
(cd "$out" && sha256sum "$name" > "$name.sha256")
hash=$(cut -d' ' -f1 "$out/$name.sha256")
echo
echo "$out/$name"
echo "For the release notes:"
echo "SHA-256: \`$hash\`"
