#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of Cab IR Generator — see LICENSE.
#
# Builds the page's module — the synthesis behind the C ABI of src/web/CabirWeb.cpp — for a browser worker:
#
#   cabir.mjs     the emscripten glue, an ES module whose default export is the factory (createCabir)
#   cabir.wasm    the module
#   BUILD-INFO    what it was built from: this repository, felitronics-core, felitronics-toml, the toolchain, the flags
#
#   tools/wasm/build.sh [out-dir]          (default tools/wasm/build)
#
# The pins are the ones CMakeLists.txt names, fetched; CABIR_CORE_DIR / CABIR_TOML_DIR name checkouts to build against
# instead, and BUILD-INFO says so. Single-threaded, as the family's wasm tier is by contract: proven from the artifact
# by felitronics-core's tools/wasm/check-no-threads.mjs.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
if ! command -v em++ > /dev/null; then
    [ -f "$HOME/emsdk/emsdk_env.sh" ] || { echo "em++ not on PATH and no ~/emsdk — source \$EMSDK/emsdk_env.sh first"; exit 1; }
    # shellcheck disable=SC1091
    source "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
fi
EMVER="$(emcc --version | head -1)"
echo "emcc: $EMVER"

GEN=()
command -v ninja > /dev/null && GEN=(-G Ninja)
FLAGS="-fno-exceptions -fno-rtti"
BUILD="$OUT/cmake"
# The pins' cache entries dropped and the checkouts stated on every run, so a build directory configured once with
# other sources cannot keep them.
emcmake cmake -S "$ROOT" -B "$BUILD" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -UCABIR_CORE_TAG -UCABIR_TOML_TAG \
    -DCABIR_CORE_DIR="${CABIR_CORE_DIR:-}" -DCABIR_TOML_DIR="${CABIR_TOML_DIR:-}" \
    "-DCMAKE_CXX_FLAGS=$FLAGS" > "$OUT/configure.log" || { cat "$OUT/configure.log"; exit 1; }
cmake --build "$BUILD" --target cabir_web
cp "$BUILD/cabir.mjs" "$BUILD/cabir.wasm" "$OUT/"

CORE_SRC="$(sed -n 's/^felitronics_core_SOURCE_DIR:STATIC=//p' "$BUILD/CMakeCache.txt")"
[ -n "$CORE_SRC" ] || CORE_SRC="$BUILD/_deps/felitronics_core-src"
echo
echo "=== no threads, proven from the artifact"
node "$CORE_SRC/tools/wasm/check-no-threads.mjs" "$OUT/cabir.wasm" "$OUT/cabir.mjs"

describe() { git -C "$1" describe --tags --always --dirty 2>/dev/null || echo "unknown"; }
commit() { git -C "$1" rev-parse HEAD 2>/dev/null || echo "unknown"; }
TOML_SRC="$(sed -n 's/^felitronics_toml_SOURCE_DIR:STATIC=//p' "$BUILD/CMakeCache.txt")"
[ -n "$TOML_SRC" ] || TOML_SRC="$BUILD/_deps/felitronics_toml-src"
{
    echo "cab-ir-generator   $(describe "$ROOT")   $(commit "$ROOT")"
    echo "felitronics-core   $(describe "$CORE_SRC")   $(commit "$CORE_SRC")"
    echo "felitronics-toml   $(describe "$TOML_SRC")   $(commit "$TOML_SRC")"
    echo "emcc               $EMVER"
    echo "flags              $FLAGS"
} > "$OUT/BUILD-INFO"
echo
cat "$OUT/BUILD-INFO"
ls -la "$OUT/cabir.mjs" "$OUT/cabir.wasm"
