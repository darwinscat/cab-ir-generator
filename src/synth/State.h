// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cabir
{

/** A MIC ON THE SPEAKER: on or off; its model by the table's name (data/cabinets.txt, "mic-<name>"), or empty for the
    box's own; across the first speaker in radii from the cap, x right and y up; in front of the cap's plane, m; and
    how far it leans back toward the cap, degrees. */
struct Mic
{
    bool on = true;
    std::string model;
    double x = 0, y = 0, distance = 0.05, angle = 0;

    bool operator== (const Mic&) const = default;
};

/** WHAT THE GENERATOR MAKES, all of it: the box by the table's name; its back — -1 the box's own, else synth::Back (3,
    half closed, the guitar's);
    its speaker by the table's name ("speaker-<name>"), empty for the box's own — the guitar's, as the wall behind it is;
    its horn and the horn's step (0, -6, -12 while on); the two mics; and how B joins A — mixed in by `blend` on one
    clock unless `align`, turned over by `invert`, moved in time by `shiftMs`. Both on, the pair; one on, that one alone, the blend kept and not
    heard. A plugin keeps it, the app's history and A/B/C/D hold copies of it, the page links it — written as TOML
    (toToml) and read back (fromToml). */
struct State
{
    std::string box;
    int back = -1;
    std::string speaker;
    bool wall = false;   // the wall behind the box, the guitar's: heard where the back lets sound out
    std::string cone;    // the speaker's cone as a variant of it has it (the guitar's "paper"), empty as it is
    bool horn = true;
    double hornDb = 0;
    Mic a { true, "", -0.3, 0, 0.05, 0 }, b { false, "u87", 0.4, 0, 0.25, 0 };   // A a little left of the cap, B right of it
    double blend = 0.5;
    bool align = true, invert = false;
    double shiftMs = 0;   // B moved in time against A, ms: later positive — where ALIGN (or the one clock) leaves it, 0

    bool operator== (const State&) const = default;
};

/** The words a back is written by, -1 first: "own", "closed", "ported", "open" — and the guitar's "half". */
inline constexpr const char* backNames[] { "own", "closed", "ported", "open", "half" };

/** THE STATE AS TOML — canonical: the same state, the same bytes, on every platform. Its first key, `cabir`, is the
    format's mark and version; a mic's model is left out where it is the box's own, and so is the speaker, and the wall
    where there is none. */
std::string toToml (const State&);

/** ...and read back: nullopt for text that is not one — no `cabir = 1`, no box, a value out of its range. A key it
    does not know is passed over, so a state a later version wrote still reads; a key it lacks keeps its default. */
std::optional<State> fromToml (std::string_view text);

} // namespace cabir
