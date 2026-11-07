#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.
#
# THE macOS FILES OF A RELEASE — an installer for each instrument, OrbitBassCabBuilder-<ver>-macOS.pkg and
# OrbitGuitarCabBuilder-<ver>-macOS.pkg: its app into /Applications, its plugins (VST3, AU, CLAP) into
# /Library/Audio/Plug-Ins, for Apple silicon and Intel alike.
#
#   installer/macos-release.sh <build-dir> <version> <out-dir>
#
# The eight bundles signed for the hardened runtime where they were built (the apps with the audio-input entitlement:
# INPUT plays an interface; the plugins hear only their host), notarized in one submission and stapled each, so a
# copy taken out of the package carries its ticket; then each package signed, notarized and stapled.
# APP_ID and INST_ID name the Developer ID Application and Installer identities, NOTARY notarytool's keychain profile,
# KEYCHAIN the keychain it lives in (the login one by default). release.yml sets them; by hand, the same.
set -euo pipefail
B="${1:?usage: macos-release.sh <build-dir> <version> <out-dir>}"
VER="${2:?missing version}"
OUT="${3:?missing out dir}"
: "${APP_ID:?set APP_ID}" "${INST_ID:?set INST_ID}" "${NOTARY:?set NOTARY}"
KEYCHAIN="${KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

# A ticket takes a moment to reach the replica stapler asks: notarytool's Accepted is not yet a ticket stapler can
# fetch (Error 65), so it is asked again, not given up on.
staple_retry() {
    local item="$1" i
    for i in 1 2 3 4 5; do
        xcrun stapler staple "$item" && return 0
        echo "staple: no ticket yet for $item (attempt $i of 5)"
        sleep 20
    done
    return 1
}

app()  { echo "$B/Orbit$1CabBuilder_artefacts/Release/Orbit $1 Cab Builder.app"; }
plug() { echo "$B/Orbit$1CabBuilderPlugin_artefacts/Release"; }
bundlesOf() {
    local p; p="$(plug "$1")"
    printf '%s\n' "$(app "$1")" "$p/VST3/Orbit $1 Cab Builder.vst3" "$p/AU/Orbit $1 Cab Builder.component" \
                  "$p/CLAP/Orbit $1 Cab Builder.clap"
}

# Signed where they were built, each for both architectures.
bundles=()
while IFS= read -r b; do bundles+=("$b"); done < <(bundlesOf Bass; bundlesOf Guitar)
for b in "${bundles[@]}"; do
    [ -d "$b" ] || { echo "missing: $b"; exit 1; }
    bin="$(find "$b/Contents/MacOS" -maxdepth 1 -type f | head -1)"
    archs="$(lipo -archs "$bin")"
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || { echo "not universal ($archs): $b"; exit 1; }
    ent=()
    [[ "$b" == *.app ]] && ent=(--entitlements "$ROOT/app/standalone.entitlements")
    codesign --force --options runtime --timestamp ${ent[@]+"${ent[@]}"} --sign "$APP_ID" "$b"
    codesign --verify --strict "$b"
    sig="$(codesign -dvv "$b" 2>&1)"
    [[ "$sig" == *"Authority=Developer ID Application"* && "$sig" == *$'\n'"Timestamp="* ]] || { echo "not signed: $b"; exit 1; }
    echo "signed  $(basename "$b")  ($archs)"
done

# The eight in one submission, a ticket stapled on each.
mkdir -p "$WORK/notarize"
for b in "${bundles[@]}"; do cp -R "$b" "$WORK/notarize/"; done
ditto -c -k "$WORK/notarize" "$WORK/bundles.zip"
xcrun notarytool submit "$WORK/bundles.zip" --keychain-profile "$NOTARY" --keychain "$KEYCHAIN" --wait
for b in "${bundles[@]}"; do
    staple_retry "$b"
    xcrun stapler validate "$b"
done

# A package for each instrument: installed where it is staged, never moved onto an older copy found elsewhere.
for inst in Bass Guitar; do
    stage="$WORK/stage-$inst"
    plugins="$stage/Library/Audio/Plug-Ins"
    mkdir -p "$stage/Applications" "$plugins/VST3" "$plugins/Components" "$plugins/CLAP"
    p="$(plug "$inst")"
    cp -R "$(app "$inst")" "$stage/Applications/"
    cp -R "$p/VST3/Orbit $inst Cab Builder.vst3" "$plugins/VST3/"
    cp -R "$p/AU/Orbit $inst Cab Builder.component" "$plugins/Components/"
    cp -R "$p/CLAP/Orbit $inst Cab Builder.clap" "$plugins/CLAP/"

    plist="$WORK/components-$inst.plist"
    pkgbuild --analyze --root "$stage" "$plist" > /dev/null
    /usr/bin/python3 -I - "$plist" <<'EOF'
import plistlib, sys
p = sys.argv[1]
items = plistlib.load(open(p, 'rb'))
for i in items:
    i['BundleIsRelocatable'] = False
    i['BundleOverwriteAction'] = 'upgrade'
plistlib.dump(items, open(p, 'wb'))
EOF
    lower="$(echo "$inst" | tr '[:upper:]' '[:lower:]')"
    pkg="$OUT/Orbit${inst}CabBuilder-$VER-macOS.pkg"
    pkgbuild --root "$stage" --install-location / --component-plist "$plist" \
        --identifier "com.darwinscat.orbit${lower}cabbuilder.pkg" --version "$VER" \
        --sign "$INST_ID" --timestamp "$pkg"
    xcrun notarytool submit "$pkg" --keychain-profile "$NOTARY" --keychain "$KEYCHAIN" --wait
    staple_retry "$pkg"
    xcrun stapler validate "$pkg"
    spctl -a -vvv -t install "$pkg"
    echo "made    $(basename "$pkg")"
done
