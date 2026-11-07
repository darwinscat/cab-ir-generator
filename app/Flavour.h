// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <panel/Link.h>
#include <synth/State.h>

#include <iterator>

/** WHICH APP THIS IS: Orbit Bass Cab Builder — or, built with CABIR_GUITAR, Orbit Guitar Cab Builder, the same app on its
    own table, presets and riffs (data/guitar), under its own name, its settings and its presets apart (the names, the
    owner's, 07.11). CMake builds it twice; whatever else the two differ in is the panel's catalog (GuitarCatalog.h),
    handed in before the window opens. `formerly`: the name it had before, whose settings and presets a first run copies. */
namespace cabir::app::flavour
{

/** A riff the app carries: its file among the riffs' binary data, and its title in the menu. */
struct Riff { const char* file; const char* title; };

#if CABIR_GUITAR
inline constexpr bool guitar = true;
inline constexpr const char* product = "Orbit Guitar Cab Builder";
inline constexpr const char* formerly = "Guitar Cab IR Generator";
inline constexpr const char* page = "https://darwinscat.com/felitronics/guitar-cab-ir-builder";   // its own, on the site
inline constexpr const char* utmSource = "guitar-cab-ir-generator";
inline constexpr const char* sampleWord = "GUITAR";
inline constexpr const char* samplePrompt = "A guitar sample";

/** THE RIFFS THE APP CARRIES: Oleh's guitar through its amp and no cabinet, so it goes straight into one (mono, 48 kHz,
    24-bit FLAC), in the menu's order. The first plays until another is picked. */
inline constexpr Riff riffs[] { { "fifth-dimension.flac", "Fifth Dimension" } };

/** A 4x12 as it stands, sealed, an SM57 a little off its cap at 3 cm; B a ribbon a step back, not heard until asked. */
inline State defaultState()
{
    State s;
    s.box = "4x12-closed";
    s.back = 0;
    s.a = { true, "sm57", -0.3, 0.0, 0.03, 0.0 };
    s.b = { false, "ribbon", 0.4, 0.0, 0.10, 0.0 };
    s.blend = 0.35;
    s.align = true;
    return s;
}
#else
inline constexpr bool guitar = false;
inline constexpr const char* product = "Orbit Bass Cab Builder";
inline constexpr const char* formerly = "Cab IR Generator";
inline constexpr const char* page = pageAddress;   // the bass's (Link.h)
inline constexpr const char* utmSource = "cab-ir-generator";
inline constexpr const char* sampleWord = "BASS";
inline constexpr const char* samplePrompt = "A bass sample";

/** THE RIFFS THE APP CARRIES: Oleh's bass, played DI — the preamp already in it, so it goes straight into the cabinet —
    each a seamless loop (mono, 48 kHz, 24-bit FLAC), in the menu's order. The first plays until another is picked. */
inline constexpr Riff riffs[] { { "11yl.flac",              "Eleven Light Years" },
                                { "fifth-dimension-1.flac", "Fifth Dimension 1" },
                                { "fifth-dimension-2.flac", "Fifth Dimension 2" },
                                { "deep-space-1.flac",      "Deep Space is My Home 1" },
                                { "deep-space-2.flac",      "Deep Space is My Home 2" },
                                { "im-projection.flac",     "I am Projection" } };

/** A 4x10, ported as eight boxes of the ten are, an MD421 close and a U47 at 12 cm where the two comb least: green
    from 40 Hz to 12 kHz (synth::quietPlaceForB found the place; the owner, 05.11). */
inline State defaultState()
{
    State s;
    s.box = "4x10-classic";
    s.back = 1;
    s.a = { true, "md421", -0.3, 0.0, 0.05, 0.0 };
    s.b = { true, "u47", -0.65, 0.0, 0.12, 0.0 };
    s.blend = 0.35;
    s.align = true;
    return s;
}
#endif

inline constexpr int numRiffs = (int) std::size (riffs);

/** WHAT IT RUNS AS: the app, or — built with CABIR_PLUGIN — a plugin, the same window in a host's under the same name, the
    track played through it. Its presets are the app's, in the app's folder; its settings its own. */
#if CABIR_PLUGIN
inline constexpr bool plugin = true;
#else
inline constexpr bool plugin = false;
#endif

} // namespace cabir::app::flavour
