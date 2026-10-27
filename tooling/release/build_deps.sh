#!/usr/bin/env bash
# Builds the native libraries a macOS or Linux release links, from pinned
# sources, into <prefix>: SDL2 and SDL2_mixer as static libraries (so players
# need neither installed; SDL still loads X11, Wayland, PulseAudio and the
# like at run time) and the GLM headers. Each library's licence goes in
# <prefix>/share/licenses/<package>, the layout third_party.py reads.
#
#   tooling/release/build_deps.sh <prefix>
#
# SDL2_mixer is built with only what the game plays, through the decoders it
# carries itself: MP3 (minimp3), OGG Vorbis (stb_vorbis) and WAV. On macOS, CMAKE_OSX_ARCHITECTURES and
# CMAKE_OSX_DEPLOYMENT_TARGET in the environment pick what it is built for.
set -euo pipefail

SDL2_VERSION=2.32.10
SDL2_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
SDL2_MIXER_VERSION=2.8.1
SDL2_MIXER_SHA256=cb760211b056bfe44f4a1e180cc7cb201137e4d1572f2002cc1be728efd22660
GLM_VERSION=1.0.1
GLM_SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c

if [ $# -ne 1 ]; then
    echo "usage: $0 <prefix>" >&2
    exit 2
fi
mkdir -p "$1"
prefix=$(cd "$1" && pwd)
work="$prefix/src"
mkdir -p "$work"

# Downloads <url> to <file> in the work folder and checks its SHA-256.
fetch() {
    local url=$1 file=$2 sha=$3
    if [ ! -f "$work/$file" ]; then
        curl -sSfL -o "$work/$file.part" "$url"
        mv "$work/$file.part" "$work/$file"
    fi
    local actual
    if command -v sha256sum >/dev/null; then
        actual=$(sha256sum "$work/$file" | cut -d' ' -f1)
    else
        actual=$(shasum -a 256 "$work/$file" | cut -d' ' -f1)
    fi
    if [ "$actual" != "$sha" ]; then
        echo "$file: SHA-256 $actual, expected $sha" >&2
        exit 1
    fi
    tar -xzf "$work/$file" -C "$work"
}

cmake_args=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix")
if [ -n "${CMAKE_OSX_ARCHITECTURES:-}" ]; then
    cmake_args+=(-DCMAKE_OSX_ARCHITECTURES="$CMAKE_OSX_ARCHITECTURES")
fi
if [ -n "${CMAKE_OSX_DEPLOYMENT_TARGET:-}" ]; then
    cmake_args+=(-DCMAKE_OSX_DEPLOYMENT_TARGET="$CMAKE_OSX_DEPLOYMENT_TARGET")
fi

# Configures, builds and installs the CMake project in <source>.
build() {
    local source=$1
    shift
    cmake -S "$source" -B "$source/build" "${cmake_args[@]}" "$@"
    cmake --build "$source/build"
    cmake --install "$source/build"
}

licence() {
    local package=$1
    shift
    mkdir -p "$prefix/share/licenses/$package"
    cp "$@" "$prefix/share/licenses/$package/"
}

fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" \
    "SDL2-$SDL2_VERSION.tar.gz" "$SDL2_SHA256"
sdl="$work/SDL2-$SDL2_VERSION"
build "$sdl" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
licence SDL2 "$sdl/LICENSE.txt"
cp "$sdl/src/hidapi/LICENSE-bsd.txt" "$prefix/share/licenses/SDL2/hidapi-LICENSE-bsd.txt"

fetch "https://github.com/libsdl-org/SDL_mixer/releases/download/release-$SDL2_MIXER_VERSION/SDL2_mixer-$SDL2_MIXER_VERSION.tar.gz" \
    "SDL2_mixer-$SDL2_MIXER_VERSION.tar.gz" "$SDL2_MIXER_SHA256"
mixer="$work/SDL2_mixer-$SDL2_MIXER_VERSION"
build "$mixer" -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_VENDORED=OFF \
    -DSDL2MIXER_MP3=ON -DSDL2MIXER_MP3_MINIMP3=ON -DSDL2MIXER_MP3_MPG123=OFF \
    -DSDL2MIXER_VORBIS=STB -DSDL2MIXER_WAVE=ON \
    -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_GME=OFF -DSDL2MIXER_MOD=OFF \
    -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_WAVPACK=OFF
licence SDL2_mixer "$mixer/LICENSE.txt"
cp "$mixer/src/codecs/minimp3/LICENSE" "$prefix/share/licenses/SDL2_mixer/minimp3-LICENSE.txt"
# stb_vorbis's licence is the end of its header, from "This software is
# available under 2 licenses".
sed -n '/This software is available under 2 licenses/,$p' "$mixer/src/codecs/stb_vorbis/stb_vorbis.h" \
    > "$prefix/share/licenses/SDL2_mixer/stb_vorbis-LICENSE.txt"

fetch "https://github.com/g-truc/glm/archive/refs/tags/$GLM_VERSION.tar.gz" "glm-$GLM_VERSION.tar.gz" "$GLM_SHA256"
glm="$work/glm-$GLM_VERSION"
build "$glm" -DGLM_BUILD_LIBRARY=OFF -DGLM_BUILD_TESTS=OFF -DGLM_BUILD_INSTALL=ON
licence glm "$glm/copying.txt"
