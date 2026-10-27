#!/usr/bin/env bash
# Builds what macOS and Linux players download, the counterpart of
# build_release.ps1 (which builds Windows' and the server package):
#   macOS: build/dist/golfpp-<version>-macos.dmg, golf++.app (Apple Silicon
#          and Intel, macOS 11 or newer) to drag into Applications
#   Linux: build/dist/golfpp-<version>-linux-x86_64.tar.gz, a folder with
#          golf++ and assets/ to unpack anywhere and run
#
#   tooling/release/build_release.sh <deps>
#
# <deps> is the prefix build_deps.sh built SDL2, SDL2_mixer and GLM into
# (on macOS built for arm64 and x86_64). Runs the tests, builds the release
# in build/installer, stages it with `cmake --install` and writes
# THIRD_PARTY.txt (third_party.py). The macOS app is signed ad hoc, not by a
# developer: macOS asks before its first launch. The version is
# project(VERSION) in CMakeLists.txt.
#
# Needs CMake, Ninja, a C++17 compiler, Rust (on macOS with the
# aarch64-apple-darwin and x86_64-apple-darwin targets), Python 3 and, on
# Linux, pkg-config with OpenSSL's and D-Bus's development files.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <deps>" >&2
    exit 2
fi
deps=$(cd "$1" && pwd)
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir="$repo_root/build/installer"
dist_dir="$repo_root/build/dist"

say() {
    echo "[release] $*"
}

version=$(sed -n 's/^project(golfpp VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' "$repo_root/CMakeLists.txt")
if [ -z "$version" ]; then
    echo "No project(golfpp VERSION x.y.z) in CMakeLists.txt" >&2
    exit 1
fi

case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform=linux ;;
    *) echo "build_release.sh is for macOS and Linux; Windows uses build_release.ps1" >&2; exit 1 ;;
esac

cd "$repo_root"
commit=$(git rev-parse --short HEAD)
if [ -n "$(git status --porcelain)" ]; then
    say "warning: the working tree has changes: this build is not exactly commit $commit."
fi
say "golf++ $version for $platform from commit $commit"

say "building and running the tests"
cmake --preset test -DCMAKE_PREFIX_PATH="$deps"
cmake --build build/test
./build/test/golf++-tests

release_args=(-DCMAKE_PREFIX_PATH="$deps")
if [ "$platform" = macos ]; then
    release_args+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
fi
say "building the release"
cmake --preset release -B "$build_dir" "${release_args[@]}"
cmake --build "$build_dir"

stage_dir="$dist_dir/golfpp-$version"
rm -rf "$stage_dir"
say "staging the install"
cmake --install "$build_dir" --prefix "$stage_dir"
python3 "$repo_root/tooling/release/third_party.py" --platform "$platform" \
    --licenses "$deps/share/licenses" --out "$stage_dir/THIRD_PARTY.txt"

if [ "$platform" = macos ]; then
    app="$stage_dir/golf++.app"
    cp "$stage_dir/THIRD_PARTY.txt" "$app/Contents/Resources/"
    # Signed once everything is in the bundle: Apple Silicon runs nothing
    # unsigned, and a change after signing reads as a damaged app.
    codesign --force --deep --sign - "$app"
    codesign --verify --deep --strict "$app"
    ln -s /Applications "$stage_dir/Applications"
    package="$dist_dir/golfpp-$version-macos.dmg"
    rm -f "$package"
    say "making $package"
    hdiutil create -volname "golf++ $version" -srcfolder "$stage_dir" -fs HFS+ -format UDZO "$package"
else
    package="$dist_dir/golfpp-$version-linux-x86_64.tar.gz"
    rm -f "$package"
    say "making $package"
    tar -czf "$package" -C "$dist_dir" "golfpp-$version"
fi
say "done: $package"
