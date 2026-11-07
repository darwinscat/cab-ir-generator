#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.
#
# Makes the IR packs a page offers (tools/cabpack.cpp), the bass's into one folder and the guitar's into another, each
# with what made it (BUILD-INFO) — run again whenever the presets or the typical lists change; the same inputs on the same
# machine give the same bytes. The typical cabinets are presets in data/typical/ and data/guitar/typical/: after a change
# to a list or the synthesis, `cabpack --typical` makes them again.
#
#   tools/cabpack.sh [out-dir] [guitar-out-dir]          (default build-pack/packs, build-pack/packs-guitar)
#
# The pins are the ones CMakeLists.txt names (felitronics-toml), fetched; CABIR_TOML_DIR names a checkout instead.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-pack"
OUT="${1:-$BUILD/packs}"
GUITAR_OUT="${2:-$BUILD/packs-guitar}"

mkdir -p "$BUILD"
GEN=()
command -v ninja > /dev/null && GEN=(-G Ninja)
cmake -S "$ROOT" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -UCABIR_TOML_TAG -DCABIR_TOML_DIR="${CABIR_TOML_DIR:-}" \
    -DCABIR_APP=OFF -DCABIR_WEB=OFF -DCABIR_TESTS=OFF -DCABIR_TOOLS=ON > "$BUILD/configure.log" || { cat "$BUILD/configure.log"; exit 1; }
cmake --build "$BUILD" --target cabpack

rm -rf "$OUT" "$GUITAR_OUT"
mkdir -p "$OUT" "$GUITAR_OUT"
"$BUILD/cabpack" "$ROOT/data/cabinets.txt" "$ROOT/data/presets" "$ROOT/data/typical.toml" "$OUT"
"$BUILD/cabpack" "$ROOT/data/guitar/cabinets.txt" "$ROOT/data/guitar/presets" "$ROOT/data/guitar/typical.toml" "$GUITAR_OUT"

describe() { git -C "$1" describe --tags --match 'v[0-9]*' --always --dirty 2>/dev/null || echo "unknown"; }
commit() { git -C "$1" rev-parse HEAD 2>/dev/null || echo "unknown"; }
TOML_SRC="$(sed -n 's/^felitronics_toml_SOURCE_DIR:STATIC=//p' "$BUILD/CMakeCache.txt")"
[ -n "$TOML_SRC" ] || TOML_SRC="$BUILD/_deps/felitronics_toml-src"
info() {
    echo "cab-ir-generator   $(describe "$ROOT")   $(commit "$ROOT")"
    echo "felitronics-toml   $(describe "$TOML_SRC")   $(commit "$TOML_SRC")"
    echo "felitronics-core   not used: the packs are the synthesis alone"
    echo "rates              44100 48000 96000, each synthesised at its own rate"
    echo "format             24-bit mono WAV, 100 ms, 15 ms raised-cosine tail, peak -6 dBFS (the app's DOWNLOAD IR)"
    echo "machine            $(uname -sm)"
}
info > "$OUT/BUILD-INFO"
info > "$GUITAR_OUT/BUILD-INFO"
cat "$OUT/BUILD-INFO"
