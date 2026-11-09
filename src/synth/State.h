// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <array>
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

/** THE PLAYER'S EQ over the cabinet (the owner, 09.11): a high pass and a low pass, 12 dB an octave, and three bells —
    rendered into the IR, so what is heard is what DOWNLOAD IR writes and what a preset keeps. Off, the IR as the cabinet
    makes it, its settings kept, so it comes back as it was left. A pass at 0 Hz is out. */
struct Eq
{
    struct Bell
    {
        double hz = 1000, db = 0, q = 1;
        bool operator== (const Bell&) const = default;
    };
    bool on = false;
    double highPassHz = 0, lowPassHz = 0;
    std::array<Bell, 3> bells { { { 250, 0, 1 }, { 800, 0, 1 }, { 3500, 0, 1 } } };   // mud, honk, bite

    /** Whether it changes anything: on, with a pass in or a bell off nought. */
    bool heard() const
    {
        if (! on)
            return false;
        bool any = highPassHz > 0 || lowPassHz > 0;
        for (const auto& b : bells)
            any = any || b.db != 0;
        return any;
    }

    bool operator== (const Eq&) const = default;
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
    std::string room;    // the room round the box by the table's name ("room-<name>", the guitar's), empty for none
    bool horn = true;
    double hornDb = 0;
    Mic a { true, "", -0.3, 0, 0.05, 0 }, b { false, "u87", 0.4, 0, 0.25, 0 };   // A a little left of the cap, B right of it
    double blend = 0.5;
    bool align = true, invert = false;
    double shiftMs = 0;   // B moved in time against A, ms: later positive — where ALIGN (or the one clock) leaves it, 0
    Eq eq;                // the player's, over all of it

    bool operator== (const State&) const = default;
};

/** The words a back is written by, -1 first: "own", "closed", "ported", "open" — and the guitar's "half". */
inline constexpr const char* backNames[] { "own", "closed", "ported", "open", "half" };

/** THE STATE AS TOML — canonical: the same state, the same bytes, on every platform. Its first key, `cabir`, is the
    format's mark and version; a mic's model is left out where it is the box's own, and so is the speaker, and the wall
    where there is none, and the EQ while it stands as it came. */
std::string toToml (const State&);

/** ...and read back: nullopt for text that is not one — no `cabir = 1`, no box, a value out of its range. A key it
    does not know is passed over, so a state a later version wrote still reads; a key it lacks keeps its default. */
std::optional<State> fromToml (std::string_view text);

} // namespace cabir
