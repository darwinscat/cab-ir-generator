// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE STATE'S TEXT: what is written reads back the same, a later version's key is passed over, a broken one refused,
// and pairOf renders the mics that are on.

#include "synth/CabSynth.h"
#include "synth/State.h"

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
    plain.box = "SVT-410HE";
    const auto plainBack = cabir::fromToml (cabir::toToml (plain));
    check (plainBack.has_value() && *plainBack == plain, "a default state reads back the same");

    State full;
    full.box = "VB-212";
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
    const auto later = cabir::fromToml ("cabir = 1\nbox = \"SVT-810\"\nfuture = 3\n[mic.a]\nmodel = \"sm57\"\nwidth = 2\n");
    check (later.has_value() && later->box == "SVT-810" && later->a.model == "sm57" && later->back == -1 && ! later->b.on,
           "a later version's keys passed over, the rest defaulted");

    // Refused: not the format, no box, out of range.
    check (! cabir::fromToml ("box = \"SVT-810\"\n").has_value(), "no cabir mark: refused");
    check (! cabir::fromToml ("cabir = 1\n").has_value(), "no box: refused");
    check (! cabir::fromToml ("cabir = 1\nbox = \"SVT-810\"\nblend = 1.5\n").has_value(), "blend out of range: refused");
    check (! cabir::fromToml ("synth:SVT-810|0|0|0.05|1|md421").has_value(), "the old line: refused");

    // pairOf: B alone is rendered as one mic, at B's spot; A alone with the blend kept but not heard.
    State onlyB = full;
    onlyB.a.on = false;
    const auto pairB = cabir::synth::pairOf (onlyB);
    check (pairB.blend <= 0 && pairB.a.mic == "u87" && pairB.a.x < -0.5, "B alone is rendered at B's spot");
    State onlyA = full;
    onlyA.b.on = false;
    const auto pairA = cabir::synth::pairOf (onlyA);
    check (pairA.blend <= 0 && pairA.a.mic == "re20" && pairA.a.back == 2 && pairA.a.hornDb < -5, "A alone, its horn and back");

    std::printf ("%s\n%s", failures == 0 ? "state: all passed" : "state: FAILED", text.c_str());
    return failures == 0 ? 0 : 1;
}
