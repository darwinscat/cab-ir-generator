// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "State.h"

#include <felitronics/toml/Schema.h>
#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cmath>

namespace cabir
{

std::string toToml (const State& s)
{
    namespace t = felitronics::toml;
    const auto decimal = [] (double x, std::uint8_t scale) { return t::Value (t::Decimal::fromDouble (x, scale)); };
    t::Table root;
    (void) root.insert ("cabir", t::Value (std::int64_t { 1 }));
    (void) root.insert ("box", t::Value (s.box));
    (void) root.insert ("back", t::Value (backNames[std::clamp (s.back, -1, 3) + 1]));
    if (! s.speaker.empty())
        (void) root.insert ("speaker", t::Value (s.speaker));
    if (s.wall)
        (void) root.insert ("wall", t::Value (true));
    if (! s.cone.empty())
        (void) root.insert ("cone", t::Value (s.cone));
    if (! s.room.empty())
        (void) root.insert ("room", t::Value (s.room));
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
    auto b = mic (s.b);
    (void) b.insert ("shift", decimal (s.shiftMs, 3));   // ms against A
    (void) mics.insert ("b", t::Value (std::move (b)));
    (void) root.insert ("mic", t::Value (std::move (mics)));
    // The writer heads the mics' parent table too; with nothing of its own it says nothing, and the file reads the
    // same without it — [mic.a] and [mic.b] imply it.
    auto text = t::write (root);
    if (const auto at = text.find ("[mic]\n\n[mic.a]"); at != std::string::npos)
        text.erase (at, std::string ("[mic]\n\n").size());
    return text;
}

std::optional<State> fromToml (std::string_view text)
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
        in.optional ("speaker", s.speaker);
        in.optional ("wall", s.wall);
        in.optional ("cone", s.cone);
        in.optional ("room", s.room);
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
            m.optional ("distance", out.distance, { 0.001, 2.0 });   // the guitar's room mic goes to 1.5 m
            m.optional ("angle", out.angle, { 0.0, 90.0 });
        };
        in.table ("mic", t::Need::Optional, [&] (t::Reader& mics)
        {
            mics.table ("a", t::Need::Optional, [&] (t::Reader& m) { mic (m, s.a); });
            mics.table ("b", t::Need::Optional, [&] (t::Reader& m)
            {
                mic (m, s.b);
                m.optional ("shift", s.shiftMs, { -2.0, 2.0 });
            });
        });
    }, t::ReadOptions { t::Severity::Warning });
    if (! report.ok() || s.box.empty())
        return std::nullopt;

    s.back = -1;
    for (int i = 0; i < 5; ++i)
        if (back == backNames[i])
            s.back = i - 1;
    s.hornDb = (double) hornDb;
    return s;
}

} // namespace cabir
