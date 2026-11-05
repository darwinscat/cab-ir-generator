// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "CabSynth.h"

#include <felitronics/toml/Schema.h>
#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cmath>
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

/** WHAT THE GENERATOR MAKES, all of it: the box by the table's name; its back — -1 the box's own, else synth::Back;
    its horn and the horn's step (0, -6, -12 while on); the two mics; and how B joins A — mixed in by `blend` on one
    clock unless `align`, turned over by `invert`. Both on, the pair; one on, that one alone, the blend kept and not
    heard. A plugin keeps it, the app's history and A/B/C/D hold copies of it, the page links it — written as TOML
    (toToml) and read back (fromToml). */
struct State
{
    std::string box;
    int back = -1;
    bool horn = true;
    double hornDb = 0;
    Mic a, b { false, "u87", 0, 0, 0.25, 0 };
    double blend = 0.5;
    bool align = true, invert = false;

    bool operator== (const State&) const = default;
};

/** The words a back is written by, -1 first: "own", "closed", "ported", "open". */
inline constexpr const char* backNames[] { "own", "closed", "ported", "open" };

/** THE STATE AS TOML — canonical: the same state, the same bytes, on every platform. Its first key, `cabir`, is the
    format's mark and version; a mic's model is left out where it is the box's own. */
inline std::string toToml (const State& s)
{
    namespace t = felitronics::toml;
    const auto decimal = [] (double x, std::uint8_t scale) { return t::Value (t::Decimal::fromDouble (x, scale)); };
    t::Table root;
    (void) root.insert ("cabir", t::Value (std::int64_t { 1 }));
    (void) root.insert ("box", t::Value (s.box));
    (void) root.insert ("back", t::Value (backNames[std::clamp (s.back, -1, 2) + 1]));
    (void) root.insert ("horn", t::Value (s.horn));
    (void) root.insert ("horn_db", t::Value ((std::int64_t) std::lround (s.hornDb)));
    (void) root.insert ("blend", decimal (s.blend, 3));
    (void) root.insert ("align", t::Value (s.align));
    (void) root.insert ("invert", t::Value (s.invert));
    const auto mic = [&decimal] (const Mic& m)
    {
        t::Table out;
        (void) out.insert ("on", t::Value (m.on));
        if (! m.model.empty())
            (void) out.insert ("model", t::Value (m.model));
        (void) out.insert ("x", decimal (m.x, 3));
        (void) out.insert ("y", decimal (m.y, 3));
        (void) out.insert ("distance", decimal (m.distance, 3));
        (void) out.insert ("angle", decimal (m.angle, 1));
        return out;
    };
    t::Table mics;
    (void) mics.insert ("a", t::Value (mic (s.a)));
    (void) mics.insert ("b", t::Value (mic (s.b)));
    (void) root.insert ("mic", t::Value (std::move (mics)));
    return t::write (root);
}

/** ...and read back: nullopt for text that is not one — no `cabir = 1`, no box, a value out of its range. A key it
    does not know is passed over, so a state a later version wrote still reads; a key it lacks keeps its default. */
inline std::optional<State> fromToml (std::string_view text)
{
    namespace t = felitronics::toml;
    auto parsed = t::parse (text);
    const auto* root = std::get_if<t::Table> (&parsed);
    if (root == nullptr)
        return std::nullopt;

    State s;
    std::int64_t version = 0, hornDb = 0;
    std::string back = backNames[0];
    const auto report = t::read (*root, [&] (t::Reader& in)
    {
        in.required ("cabir", version, { 1, 1 });
        in.required ("box", s.box);
        in.optional ("back", back);
        in.optional ("horn", s.horn);
        in.optional ("horn_db", hornDb, { -12, 0 });
        in.optional ("blend", s.blend, { 0.0, 1.0 });
        in.optional ("align", s.align);
        in.optional ("invert", s.invert);
        const auto mic = [] (t::Reader& m, Mic& out)
        {
            m.optional ("on", out.on);
            m.optional ("model", out.model);
            m.optional ("x", out.x, { -1.5, 1.5 });
            m.optional ("y", out.y, { -1.5, 1.5 });
            m.optional ("distance", out.distance, { 0.001, 1.0 });
            m.optional ("angle", out.angle, { 0.0, 90.0 });
        };
        in.table ("mic", t::Need::Optional, [&] (t::Reader& mics)
        {
            mics.table ("a", t::Need::Optional, [&] (t::Reader& m) { mic (m, s.a); });
            mics.table ("b", t::Need::Optional, [&] (t::Reader& m) { mic (m, s.b); });
        });
    }, t::ReadOptions { t::Severity::Warning });
    if (! report.ok() || s.box.empty())
        return std::nullopt;

    s.back = -1;
    for (int i = 0; i < 4; ++i)
        if (back == backNames[i])
            s.back = i - 1;
    s.hornDb = (double) hornDb;
    return s;
}

/** WHAT THE SYNTHESIS RENDERS of a state: its two mics on one clock, the horn and the back A's — or, with one mic on,
    that one alone. */
inline synth::Pair pairOf (const State& s)
{
    const auto spot = [&s] (const Mic& m)
    {
        synth::Spot p;
        p.x = m.x;
        p.y = m.y;
        p.distance = m.distance;
        p.horn = s.horn;
        p.mic = m.model;
        p.angle = m.angle;
        p.hornDb = s.hornDb;
        p.back = s.back;
        return p;
    };
    synth::Pair pair { spot (s.a), spot (s.b), s.blend, s.align, s.invert };
    if (! s.b.on)
        pair.blend = 0;            // A alone: the blend not heard
    else if (! s.a.on)
    {
        pair.a = spot (s.b);       // B alone, as one mic is rendered
        pair.blend = 0;
    }
    return pair;
}

} // namespace cabir
