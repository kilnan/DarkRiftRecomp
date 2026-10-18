#!/bin/zsh
# Builds the macOS release dependencies from source with an old deployment target, so a release
# bundle runs on more than the newest macOS (Homebrew's builds target the host OS). SDL2 is
# provided by sdl2-compat on top of SDL3, the same combination Homebrew ships and the one the
# port is developed and tested with (with the classic SDL2 2.30 the game hangs on the title).
#
# Usage: scripts/build_macos_deps.sh [prefix]   (default: deps-macos)
# Then configure with -DCMAKE_PREFIX_PATH=<prefix>.
set -euo pipefail

SDL3_VERSION=3.4.18
SDL2_COMPAT_VERSION=2.32.74
FREETYPE_TAG=VER-2-13-2
DEPLOYMENT_TARGET=11.0

ROOT=${0:A:h:h}
PREFIX=${1:-$ROOT/deps-macos}
PREFIX=${PREFIX:A}
SRC=$PREFIX/src
mkdir -p "$SRC"

if [[ ! -d "$SRC/SDL3" ]]; then
    git clone --depth 1 --branch "release-$SDL3_VERSION" https://github.com/libsdl-org/SDL.git "$SRC/SDL3"
fi
cmake -S "$SRC/SDL3" -B "$SRC/SDL3/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF
cmake --build "$SRC/SDL3/build"
cmake --install "$SRC/SDL3/build"

if [[ ! -d "$SRC/sdl2-compat" ]]; then
    git clone --depth 1 --branch "release-$SDL2_COMPAT_VERSION" https://github.com/libsdl-org/sdl2-compat.git "$SRC/sdl2-compat"
fi
cmake -S "$SRC/sdl2-compat" -B "$SRC/sdl2-compat/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DSDL2COMPAT_TESTS=OFF
cmake --build "$SRC/sdl2-compat/build"
cmake --install "$SRC/sdl2-compat/build"

if [[ ! -d "$SRC/freetype" ]]; then
    git clone --depth 1 --branch "$FREETYPE_TAG" https://gitlab.freedesktop.org/freetype/freetype.git "$SRC/freetype"
fi
cmake -S "$SRC/freetype" -B "$SRC/freetype/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_ZLIB=ON
cmake --build "$SRC/freetype/build"
cmake --install "$SRC/freetype/build"

echo "Dependencies installed to $PREFIX"
