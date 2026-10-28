#!/usr/bin/env bash
# Builds the Xbox account helper for Linux (helpers/xbox, Rust, on Xodus) and prints its path, size
# and SHA-256. Launchme does not download it on Linux: it runs the launchme-xbox next to its own
# executable (src/store/xbox_account.cpp), so a package (the AppImage) ships this file there.
#
#   tools/build-xbox-helper.sh [--out DIR] [--xgameruntime]
#
# --out DIR       also installs it as DIR/launchme-xbox (e.g. next to build/linux/launchme).
# --xgameruntime  also builds the open xgameruntime.dll of the Xodus project (pinned commit) into
#                 DIR/xgameruntime/ (or build/xgameruntime/), where the helper's `run` finds it.
#                 Needs the mingw-w64 cross compiler, its widl, CMake and Ninja.
#
# Needs rustup's stable toolchain (Rust 1.98+), protoc (on PATH or $PROTOC: Xodus compiles .proto
# files), pkg-config and the development files of gtk4, webkitgtk-6.0, libsoup-3.0, dbus-1 and openssl
# (Arch: gtk4 webkitgtk-6.0 dbus openssl protobuf; Debian/Ubuntu: libgtk-4-dev libwebkitgtk-6.0-dev
# libsoup-3.0-dev libdbus-1-dev libssl-dev protobuf-compiler).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
crate="$root/helpers/xbox"
out=""
runtime=0
while [ $# -gt 0 ]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --xgameruntime) runtime=1; shift ;;
        *) echo "usage: $0 [--out DIR] [--xgameruntime]" >&2; exit 2 ;;
    esac
done

for tool in cargo pkg-config; do
    command -v "$tool" >/dev/null || { echo "$tool not found on PATH" >&2; exit 1; }
done
if [ -z "${PROTOC:-}" ] && ! command -v protoc >/dev/null; then
    echo "protoc not found (PATH or \$PROTOC)" >&2; exit 1
fi
for module in gtk4 webkitgtk-6.0 libsoup-3.0 dbus-1 openssl; do
    pkg-config --exists "$module" || { echo "missing development files: $module" >&2; exit 1; }
done

version="$(sed -n 's/^version = "\(.*\)"/\1/p' "$crate/Cargo.toml" | head -1)"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$root/build/temp/xbox-helper-linux}"
cargo build --release --locked --manifest-path "$crate/Cargo.toml"
cargo test --release --locked --manifest-path "$crate/Cargo.toml"

mkdir -p "$root/build"
published="$root/build/launchme-xbox-$version-linux-$(uname -m)"
install -m 0755 "$CARGO_TARGET_DIR/release/launchme-xbox" "$published"
echo "Helper:  $published"
echo "Version: $version"
echo "Bytes:   $(stat -c %s "$published")"
echo "SHA-256: $(sha256sum "$published" | cut -d' ' -f1)"
if [ -n "$out" ]; then
    mkdir -p "$out"
    install -m 0755 "$published" "$out/launchme-xbox"
    echo "Installed: $out/launchme-xbox"
fi

# xgameruntime (https://github.com/xodus-gaming/xgameruntime, no releases yet): a mingw build of
# xgameruntime.dll for Wine. It implements only part of the GDK runtime so far (XThreading, task
# queues); `launchme-xbox run` places it next to the game's executable with xgameruntime=n,b.
if [ "$runtime" = 1 ]; then
    rev="f03484d64867c1e65d421c1aba2f290c20bd7b4d"
    for tool in x86_64-w64-mingw32-g++ cmake ninja git; do
        command -v "$tool" >/dev/null || { echo "$tool not found on PATH (xgameruntime)" >&2; exit 1; }
    done
    command -v widl >/dev/null || command -v x86_64-w64-mingw32-widl >/dev/null || { echo "widl not found (mingw-w64-tools)" >&2; exit 1; }
    source_dir="$root/build/temp/xgameruntime"
    if [ ! -d "$source_dir/.git" ]; then
        git init -q "$source_dir"
        git -C "$source_dir" remote add origin https://github.com/xodus-gaming/xgameruntime.git
    fi
    git -C "$source_dir" fetch -q --depth 1 origin "$rev"
    git -C "$source_dir" checkout -q --force FETCH_HEAD
    cmake -S "$source_dir" -B "$source_dir/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$source_dir/win64-target.cmake"
    cmake --build "$source_dir/build" --target xgameruntime
    destination="${out:-$root/build}/xgameruntime"
    mkdir -p "$destination"
    install -m 0644 "$source_dir/build/bin/xgameruntime.dll" "$destination/xgameruntime.dll"
    echo "xgameruntime: $destination/xgameruntime.dll ($(stat -c %s "$destination/xgameruntime.dll") bytes, $rev)"
fi
