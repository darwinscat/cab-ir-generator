// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE STATE'S TEXT: what is written reads back the same, a later version's key is passed over, a broken one refused,
// and pairOf renders the mics that are on.

#include "panel/Download.h"
#include "panel/Link.h"
#include "synth/CabSynth.h"
#include "synth/State.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}
}

int main()
{
    using cabir::State;

    // A default state, and a full one, written and read back the same.
    State plain;
    plain.box = "4x10-classic";
    const auto plainBack = cabir::fromToml (cabir::toToml (plain));
    check (plainBack.has_value() && *plainBack == plain, "a default state reads back the same");

    State full;
    full.box = "2x12-round";
    full.back = 2;
    full.horn = true;
    full.hornDb = -6;
    full.a = { true, "re20", 0.4, -0.2, 0.1, 15.0 };
    full.b = { true, "u87", -0.6, 0.0, 0.25, 0.0 };
    full.blend = 0.35;
    full.align = false;
    full.invert = true;
    const auto text = cabir::toToml (full);
    const auto fullBack = cabir::fromToml (text);
    check (fullBack.has_value() && *fullBack == full, "a full state reads back the same");
    check (cabir::toToml (*fullBack) == text, "written again, the same bytes");

    // A key a later version adds is passed over; a missing one keeps its default.
    const auto later = cabir::fromToml ("cabir = 1\nbox = \"8x10-classic\"\nfuture = 3\n[mic.a]\nmodel = \"sm57\"\nwidth = 2\n");
    check (later.has_value() && later->box == "8x10-classic" && later->a.model == "sm57" && later->back == -1 && ! later->b.on,
           "a later version's keys passed over, the rest defaulted");

    // Refused: not the format, no box, out of range.
    check (! cabir::fromToml ("box = \"8x10-classic\"\n").has_value(), "no cabir mark: refused");
    check (! cabir::fromToml ("cabir = 1\n").has_value(), "no box: refused");
    check (! cabir::fromToml ("cabir = 1\nbox = \"8x10-classic\"\nblend = 1.5\n").has_value(), "blend out of range: refused");
    check (! cabir::fromToml ("synth:8x10-classic|0|0|0.05|1|md421").has_value(), "the old line: refused");

    // pairOf: B alone is rendered as one mic, at B's spot; A alone with the blend kept but not heard.
    State onlyB = full;
    onlyB.a.on = false;
    const auto pairB = cabir::synth::pairOf (onlyB);
    check (pairB.blend <= 0 && pairB.a.mic == "u87" && pairB.a.x < -0.5, "B alone is rendered at B's spot");
    State onlyA = full;
    onlyA.b.on = false;
    const auto pairA = cabir::synth::pairOf (onlyA);
    check (pairA.blend <= 0 && pairA.a.mic == "re20" && pairA.a.back == 2 && pairA.a.hornDb < -5, "A alone, its horn and back");

    // The quiet place for B: to A's right, within the cone's reach, and no more red with A than where B waited.
    {
        const auto table = cabir::synth::readTable (CABIR_TABLE);
        State pair;
        pair.box = "4x10-classic";
        pair.b.on = true;
        const auto place = cabir::synth::quietPlaceForB (table, pair);
        check (std::hypot (place.x, place.y) <= 1.05 && place.distance > 0.0, "B's quiet place lies on the cone");
        check (std::hypot (place.x - pair.a.x, place.y - pair.a.y) >= 0.1 || std::abs (place.distance - pair.a.distance) >= 0.01,
               "B's quiet place is not on top of A");
    }

    // A pair rendered again with one mic moved, the other's IR kept, is the pair rendered afresh, sample for sample.
    {
        const auto table = cabir::synth::readTable (CABIR_TABLE);
        State pair;
        pair.box = "4x10-classic";
        pair.b.on = true;
        cabir::synth::PairRenderer renderer;
        (void) renderer.render (table, pair.box, cabir::synth::pairOf (pair));
        pair.b.x = 0.8;
        const auto kept = renderer.render (table, pair.box, cabir::synth::pairOf (pair));
        const auto fresh = cabir::synth::renderPairParts (table, pair.box, cabir::synth::pairOf (pair));
        check (kept.mix == fresh.mix && kept.a == fresh.a && kept.b == fresh.b, "a kept mic's IR renders the pair as afresh");
    }

    // A link (panel/Link.h) carries what is heard: read back, the same cabinet — a mic not heard and a horn turned off
    // waiting where a fresh state keeps them; a query, a whole address, a table key and a later key read too.
    {
        State heard = full;
        heard.horn = true;
        heard.hornDb = -12;
        const auto back = cabir::fromLink (cabir::linkOf (heard));
        check (back.has_value() && *back == heard, "a link reads back the same heard cabinet");
        check (cabir::linkOf (heard).find ("2x12-round") == std::string::npos, "a link names the box by its size, no maker's");
        check (cabir::fromLink (std::string (cabir::pageAddress) + "#" + cabir::linkOf (heard)) == back, "a whole address reads");
        check (cabir::fromLink ("?cab=1&box=8x10-classic&a=sm57,0.2,0,3,10&later=7").has_value(), "a query, a table key, a later key read");
        check (! cabir::fromLink ("cab=1&box=9x9").has_value(), "a box the panel has not: none");
        for (const char* bad : { "cab=1&box=8x10&shift=nan", "cab=1&box=8x10&blend=inf", "cab=1&box=8x10&a=md421,nan,0,5,0",
                                 "cab=1&box=8x10&a=md421,0,0,-nan,0", "cab=1&box=8x10&b=u87,0,0,5,NAN(1)", "cab=1&box=8x10&horn=x",
                                 "cab=1&box=8x10&a=md421,0,0" })
            check (! cabir::fromLink (bad).has_value(), "a link with a value that is no number: none");
        State alone = full;
        alone.b.on = false;
        const auto one = cabir::fromLink (cabir::linkOf (alone));
        check (one.has_value() && one->a == alone.a && ! one->b.on && one->box == alone.box, "a mic not heard is left out");
    }

    // A state of a box the table has not — a name an older version kept — is no state, and were it rendered, it would be
    // silence, not a crash (0.1.3 crashed on its start over a 0.1.2 workspace).
    {
        check (! cabir::knownState ("cabir = 1\nbox = \"9x9-gone\"\n").has_value(), "a box the table has not: no state");
        check (cabir::knownState (cabir::toToml (full)).has_value(), "a box it has: the state");
        const auto table = cabir::synth::readTable (CABIR_TABLE);
        State gone = full;
        gone.box = "9x9-gone";
        const auto parts = cabir::synth::renderPairParts (table, gone.box, cabir::synth::pairOf (gone));
        check (std::all_of (parts.mix.begin(), parts.mix.end(), [] (float v) { return std::abs (v) < 1.0e-12f; }),
               "a box the table has not renders silence");
    }

    // DOWNLOAD IR in a format (panel/Download.h): the usual one the very WAV it always was; 44.1 kHz, 16 bits, 1024 samples
    // as a pedal takes it — its header saying so, its length exact, its end faded to nothing.
    {
        const auto table = cabir::synth::readTable (CABIR_TABLE);
        const cabir::synth::WavMeta meta { "test", "Darwin's Cat", "Orbit Bass Cab Builder", "", "" };
        const auto usual = cabir::downloadWav (table, full, {}, meta);
        const auto was = cabir::synth::wav24 (cabir::synth::PairRenderer {}.render (table, full.box, cabir::synth::pairOf (full)).mix, 48000, &meta);
        check (usual == was, "the usual format is the WAV it always was");
        const auto pedal = cabir::downloadWav (table, full, { 44100, 16, 1024 }, meta);
        const auto u16 = [&pedal] (size_t at) { return (int) pedal[at] | ((int) pedal[at + 1] << 8); };
        const auto u32 = [&pedal] (size_t at) { return (unsigned) pedal[at] | ((unsigned) pedal[at + 1] << 8) | ((unsigned) pedal[at + 2] << 16) | ((unsigned) pedal[at + 3] << 24); };
        check (u32 (24) == 44100 && u16 (34) == 16 && u16 (32) == 2, "44.1 kHz, 16 bits in its header");
        check (u32 (40) == 2048, "1024 samples, no more");
        const auto last = (short) u16 (44 + 2 * 1023);
        check (std::abs ((int) last) <= 1, "its end faded to nothing");
        check (cabir::downloadName (full, { 44100, 16, 1024 }).find (" 44k 16bit 1024") != std::string::npos, "the format in its name");
        check (cabir::formatNote ({ 44100, 16, 1024 }) == "44.1 kHz, 16 bit, 1024 samples", "the line under the button");
    }

    // An option lays its keys over its section; switched off, the table is the one without it.
    {
        const std::string plainText = "[a]\nx = 1\ny = 2\n[b]\nz = 3\n";
        const std::string withOption = plainText + "[option loud: a]\nlabel = LOUD\ny = 5\nw = 6\n[option loud: b]\nz = 7\n";
        const auto on = cabir::synth::parseTable (withOption), off = cabir::synth::parseTable (withOption, { "loud" });
        check (off == cabir::synth::parseTable (plainText), "an option switched off leaves the table as it was");
        check (on.size() == 2 && on[0].second.at ("y") == "5" && on[0].second.at ("w") == "6" && on[1].second.at ("z") == "7"
                   && on[0].second.count ("label") == 0,
               "an option on lays its keys over its sections, its label left out");
        const auto names = cabir::synth::optionsOf (withOption);
        check (names.size() == 1 && names[0].first == "loud" && names[0].second == "LOUD", "an option named once, with its label");
    }

    std::printf ("%s\n%s", failures == 0 ? "state: all passed" : "state: FAILED", text.c_str());
    return failures == 0 ? 0 : 1;
}
