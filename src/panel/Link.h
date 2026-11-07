// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

// A CABINET AS A LINK (the owner, 06.11): the page's address, the cabinet after its `#` — one page for a search engine,
// and the server never sees which cabinet (the owner, 05.11) — opening the live panel on exactly that cabinet. The same
// link is in every WAV the generator writes (synth::WavMeta), beside its preset's TOML.
//
//   https://darwinscat.com/felitronics/cab-ir-generator#cab=1&box=8x10&back=closed&horn=-6
//       &a=md421,-0.3,0,5,0&b=u47,-0.65,0,12,0&blend=0.35&align=1&invert=0&shift=0
//
// cab — the link's own version, 1, first, so an older link still reads when the cabinet grows (an unknown key is passed
// over); box — by the panel's size word, no maker's name (the owner, 05.11), a table key only for a box the panel's pile
// has none of the size of (one is read either way); back own|closed|ported|open; horn off or its step in dB; a, b —
// model (own: the box's own), across and up in radii, distance in cm, angle in degrees, a mic not heard left out; blend,
// align, invert, shift (ms). Every number at the precision the preset's TOML keeps, so the two say the same cabinet.

#include "Panel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace cabir
{

inline constexpr const char* pageAddress = "https://darwinscat.com/felitronics/cab-ir-generator";

namespace link
{
    /** A number at `decimals` at most, its trailing noughts gone: "-0.3", "5", "12.5". */
    inline std::string number (double v, int decimals)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.*f", decimals, v);
        std::string s (b);
        if (s.find ('.') != std::string::npos)
        {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        return s == "-0" ? "0" : s;
    }

    /** The pile's box of a size word ("8x10"), or empty. */
    inline std::string pileBoxOf (const std::string& word)
    {
        for (const auto& column : Panel::pile())
            for (const auto& name : column)
                if (const int i = Panel::indexOf (name); i >= 0 && Panel::wordOf (i).toStdString() == word)
                    return name.toStdString();
        return {};
    }

    inline std::string decoded (std::string_view s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i] == '%' && i + 2 < s.size())
            {
                out.push_back ((char) std::strtol (std::string (s.substr (i + 1, 2)).c_str(), nullptr, 16));
                i += 2;
            }
            else
                out.push_back (s[i] == '+' ? ' ' : s[i]);
        return out;
    }

    /** A number, finite — "nan", "inf" and the like are none (strtod reads them, and a clamp lets NaN through). */
    inline bool numberOf (const std::string& s, double& v)
    {
        if (s.empty())
            return false;
        char* end = nullptr;
        v = std::strtod (s.c_str(), &end);
        return end != nullptr && *end == 0 && std::isfinite (v);
    }
}

/** The part after `#` that carries `s`. */
inline std::string linkOf (const State& s)
{
    using link::number;
    const int at = Panel::indexOf (ui::String (s.box));
    const auto word = at >= 0 ? Panel::wordOf (at).toStdString() : std::string();
    const auto box = ! word.empty() && link::pileBoxOf (word) == s.box ? word : s.box;
    const auto mic = [] (const Mic& m)
    {
        return (m.model.empty() ? std::string ("own") : m.model) + "," + number (m.x, 3) + "," + number (m.y, 3) + ","
             + number (m.distance * 100.0, 1) + "," + number (m.angle, 1);
    };
    std::string out = "cab=1&box=" + box + "&back=" + backNames[std::clamp (s.back, -1, 3) + 1]
                    + (s.speaker.empty() ? std::string() : "&speaker=" + s.speaker)   // the guitar's, where turned
                    + (s.wall ? "&wall=1" : "")                                        // ...and its wall, where on
                    + "&horn=" + (s.horn ? number (std::round (s.hornDb), 0) : std::string ("off"));
    if (s.a.on) out += "&a=" + mic (s.a);
    if (s.b.on) out += "&b=" + mic (s.b);
    out += "&blend=" + number (s.blend, 3) + "&align=" + (s.align ? "1" : "0") + "&invert=" + (s.invert ? "1" : "0")
         + "&shift=" + number (s.shiftMs, 3);
    return out;
}

/** ...and the cabinet a link carries: the part after `#` (or a query, or a whole address), or none where it names no
    box the panel knows. */
inline std::optional<State> fromLink (std::string_view text)
{
    if (const auto hash = text.find ('#'); hash != std::string_view::npos)
        text = text.substr (hash + 1);
    else if (const auto query = text.find ('?'); query != std::string_view::npos)
        text = text.substr (query + 1);

    State s;
    s.a.on = false;
    s.b.on = false;
    bool boxed = false, broken = false;   // broken: a value that is no number where one is due — then no cabinet at all
    const auto mic = [] (const std::string& v, Mic& m)
    {
        std::vector<std::string> f;
        for (size_t from = 0;;)
        {
            const auto comma = v.find (',', from);
            f.push_back (v.substr (from, comma == std::string::npos ? std::string::npos : comma - from));
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        double x = 0, y = 0, cm = 5, angle = 0;
        if (f.size() < 5 || ! link::numberOf (f[1], x) || ! link::numberOf (f[2], y) || ! link::numberOf (f[3], cm)
            || ! link::numberOf (f[4], angle))
            return false;
        m.on = true;
        m.model = f[0] == "own" ? std::string() : f[0];
        m.x = std::clamp (x, -1.2, 1.2);
        m.y = std::clamp (y, -1.2, 1.2);
        m.distance = std::round (std::clamp (cm, 1.0, 30.0) * 10.0) / 1000.0;   // tenths of a cm: the TOML's own metres
        m.angle = std::clamp (angle, 0.0, 90.0);
        return true;
    };
    while (! text.empty())
    {
        const auto amp = text.find ('&');
        const auto pair = text.substr (0, amp);
        text = amp == std::string_view::npos ? std::string_view() : text.substr (amp + 1);
        const auto eq = pair.find ('=');
        if (eq == std::string_view::npos)
            continue;
        const auto key = link::decoded (pair.substr (0, eq)), value = link::decoded (pair.substr (eq + 1));
        double v = 0;
        if (key == "box")
        {
            if (const auto pile = link::pileBoxOf (value); ! pile.empty())
                s.box = pile;
            else if (Panel::indexOf (ui::String (value)) >= 0)
                s.box = value;
            boxed = ! s.box.empty();
        }
        else if (key == "back")
        {
            for (int i = 0; i < 5; ++i)
                if (value == backNames[i])
                    s.back = i - 1;
        }
        else if (key == "wall") s.wall = value == "1";
        else if (key == "speaker")
        {
            if (Panel::catalog().speakers.contains (ui::String (value)))
                s.speaker = value;
        }
        else if (key == "horn")
        {
            s.horn = value != "off";
            if (s.horn && link::numberOf (value, v))
                s.hornDb = std::clamp (v, -12.0, 0.0);
            else if (s.horn)
                broken = true;
        }
        else if (key == "a") broken |= ! mic (value, s.a);
        else if (key == "b") broken |= ! mic (value, s.b);
        else if (key == "blend") { if (link::numberOf (value, v)) s.blend = std::clamp (v, 0.0, 1.0); else broken = true; }
        else if (key == "align") s.align = value == "1";
        else if (key == "invert") s.invert = value == "1";
        else if (key == "shift") { if (link::numberOf (value, v)) s.shiftMs = std::clamp (v, -1.0, 1.0); else broken = true; }
        // cab, and every key it does not know, passed over
    }
    if (! boxed || broken)
        return std::nullopt;
    if (! s.a.on && ! s.b.on)
        s.a.on = true;     // no mic said: the box's own at its cap
    if (! s.b.on)          // a mic not heard waits where a fresh state keeps it
        s.b = { false, "u87", 0.4, 0, 0.25, 0 };
    if (! s.a.on)
        s.a = { false, "", -0.3, 0, 0.05, 0 };
    return s;
}

} // namespace cabir
