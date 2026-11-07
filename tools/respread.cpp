// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PRESETS' MICS WHERE THE PANEL'S LINE STAYS GREEN — a single mic off its pit, a pair's B off A's spot.
//
//   respread <cabinets.txt> <preset.toml>... [--write]
//
// Judged as the panel draws a cabinet: the pit, how far the heard response (a sixth of an octave, as the band draws it)
// falls under its own third of an octave either side, 40 Hz to 8 kHz — a dip a player hears, which no comb shows, 3 dB at
// most, where the line starts to redden (the owner, 05.11) — and with two mics where they meet (synth::combOf, 30 Hz to
// 12 kHz), its worst point green above -3 dB.
// A single mic with a pit moves as FIND moves one (synth::MicSearch): about where it stands, the nearest place without
// one. A pair, in two steps (the owner, 05.11): first A alone, the same way, if A by itself has a pit — B rarely fills
// one — then B. A pair whose B stands on A's spot (under half a radius across), or that fails either, has B moved: its
// mics, A's place, the blend and the clock kept, B tried across the speaker at least half a radius from A — a coarse
// round at its own distance and a quarter either side, each place at its best TIME (-0.5 to +0.5 ms), then finer round
// the six best — the place whose worse margin is the widest kept, ties to the smaller shift, then the nearer place. Up to
// two pairs a style that stand on one axis and pass already stay as they are: a close mic and a far one on one axis is a
// way too.

#include <synth/PairSpread.h>
#include <synth/State.h>

#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <sstream>
#include <thread>
#include <tuple>

namespace
{
    using namespace cabir;
    using namespace cabir::synth::green;   // the search and its judging (synth/PairSpread.h)
    namespace t = felitronics::toml;

    std::string field (const std::string& text, const char* key)
    {
        auto parsed = t::parse (text);
        if (const auto* root = std::get_if<t::Table> (&parsed))
            if (const auto* v = root->find (key))
                if (const auto* s = std::get_if<std::string> (&v->data))
                    return *s;
        return {};
    }

    double apart (const State& s) { return std::hypot (s.b.x - s.a.x, s.b.y - s.a.y); }

    struct Preset
    {
        std::string path, name, style, head;
        State state, was;
        bool pair = false, aMoved = false;
        double worst = 0, pit = 0, apart = 0, aPit = 0;
    };
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "respread <cabinets.txt> <preset.toml>... [--write]\n"); return 2; }
    const auto table = synth::readTable (argv[1]);
    bool write = false;
    std::vector<Preset> all;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--write") == 0) { write = true; continue; }
        std::ifstream in (argv[i]);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto text = ss.str();
        const auto state = fromToml (text);
        if (! state) { std::printf ("BAD  %s\n", argv[i]); continue; }
        Preset p;
        p.path = argv[i];
        p.name = field (text, "name");
        p.style = field (text, "style");
        p.state = *state;
        p.pair = state->a.on && state->b.on && state->blend > 0;
        p.was = *state;
        all.push_back (std::move (p));
    }

    // A single mic, and a pair's A heard alone: off its pit first, each search on all the machine's threads.
    int singles = 0, singlesMoved = 0, singlesClear = 0;
    for (auto& p : all)
    {
        auto alone = p.state;
        if (p.pair)
            alone.b.on = false;
        const double before = judgeOf (table, alone).second;
        double now = before;
        if (before > pitDb)
        {
            const auto moved = smoothed (table, alone);
            now = judgeOf (table, moved).second;
            if (p.pair)
            {
                p.state.a = moved.a;
                p.aMoved = true;
            }
            else
                p.state = moved;
        }
        if (p.pair)
        {
            p.aPit = now;
            continue;
        }
        ++singles;
        singlesMoved += now < before;
        singlesClear += now <= pitDb;
        const auto& m = p.state.a.on ? p.state.a : p.state.b;
        const auto& w = p.was.a.on ? p.was.a : p.was.b;
        if (before > pitDb)
            std::printf ("ONE   %-34s pit %5.1f -> %5.1f   %+.2f %+.2f %4.1f cm -> %+.2f %+.2f %4.1f cm%s\n", p.name.c_str(), before, now,
                         w.x, w.y, w.distance * 100, m.x, m.y, m.distance * 100, now <= pitDb ? "" : "  STILL RED");
        else
            std::printf ("ONE   %-34s pit %5.1f   kept\n", p.name.c_str(), before);
    }

    // Where each pair stands now, judged as the band draws it.
    {
        std::vector<std::future<void>> work;
        const size_t lanes = std::max<size_t> (1, std::thread::hardware_concurrency() / 4);
        for (size_t lane = 0; lane < lanes; ++lane)
            work.push_back (std::async (std::launch::async, [&, lane]
            {
                for (size_t i = lane; i < all.size(); i += lanes)
                    if (all[i].pair)
                    {
                        std::tie (all[i].worst, all[i].pit) = judgeOf (table, all[i].state);
                        all[i].apart = apart (all[i].state);
                    }
            }));
        for (auto& w : work) w.get();
    }

    // Two green pairs on one axis a style stay; every other pair on A's spot moves.
    std::map<std::string, std::vector<size_t>> stacked;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].pair && all[i].apart < apartRadii)
            stacked[all[i].style].push_back (i);
    std::vector<bool> keep (all.size(), false);
    for (auto& [style, list] : stacked)
    {
        std::stable_sort (list.begin(), list.end(), [&all] (size_t a, size_t b)
                          { return margin (all[a].worst, all[a].pit) > margin (all[b].worst, all[b].pit); });
        for (size_t k = 0, kept = 0; k < list.size() && kept < 2; ++k)
            if (passes (all[list[k]].worst, all[list[k]].pit))
            {
                keep[list[k]] = true;
                ++kept;
            }
    }

    std::vector<size_t> moving;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].pair && ! keep[i] && (all[i].apart < apartRadii || ! passes (all[i].worst, all[i].pit)))
            moving.push_back (i);

    std::vector<Found> found (all.size());
    std::vector<std::pair<double, double>> after (all.size());
    {
        std::vector<std::future<void>> work;
        const size_t lanes = std::max<size_t> (1, std::thread::hardware_concurrency() / 4);
        for (size_t lane = 0; lane < lanes; ++lane)
            work.push_back (std::async (std::launch::async, [&, lane]
            {
                for (size_t k = lane; k < moving.size(); k += lanes)
                {
                    const size_t i = moving[k];
                    const auto b = spreadB (table, all[i].state);   // the real path, B moved in time as the app moves it
                    found[i] = b.found;
                    after[i] = b.after;
                }
            }));
        for (auto& w : work) w.get();
    }

    const auto save = [] (const Preset& p, const State& s)
    {
        t::Table head;
        (void) head.insert ("name", t::Value (p.name));
        (void) head.insert ("style", t::Value (p.style));
        std::ofstream (p.path) << t::write (head) + toToml (s);
    };
    if (write)
        for (const auto& p : all)
            if (! p.pair && ! (p.state.a == p.was.a && p.state.b == p.was.b))
                save (p, p.state);

    int moved = 0, green = 0, red = 0, kept = 0, aMoved = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& p = all[i];
        if (! p.pair)
            continue;
        aMoved += p.aMoved;
        char aSays[96] = "";
        if (p.aMoved)
            std::snprintf (aSays, sizeof aSays, "  A %+.2f %+.2f %4.1f cm -> %+.2f %+.2f %4.1f cm (alone pit %.1f)", p.was.a.x, p.was.a.y,
                           p.was.a.distance * 100, p.state.a.x, p.state.a.y, p.state.a.distance * 100, p.aPit);
        if (std::find (moving.begin(), moving.end(), i) == moving.end())
        {
            ++kept;
            std::printf ("KEPT  %-34s apart %.2f  comb %6.1f pit %5.1f%s%s\n", p.name.c_str(), p.apart, p.worst, p.pit, keep[i] ? "  (one axis)" : "", aSays);
            if (write && p.aMoved)
                save (p, p.state);
            continue;
        }
        ++moved;
        const bool ok = passes (after[i].first, after[i].second);
        (ok ? green : red)++;
        char was[24] = "";
        if (std::abs (found[i].blend - p.state.blend) > 1e-9)
            std::snprintf (was, sizeof was, " (was %.2f)", p.state.blend);
        std::printf ("MOVED %-34s apart %.2f comb %6.1f pit %5.1f -> B %+.3f %+.3f %4.1f cm %+.3f ms blend %.2f%s, apart %.2f comb %6.1f pit %5.1f%s%s\n",
                     p.name.c_str(), p.apart, p.worst, p.pit, found[i].x, found[i].y, found[i].distance * 100, found[i].shiftMs,
                     found[i].blend, was,
                     std::hypot (found[i].x - p.state.a.x, found[i].y - p.state.a.y), after[i].first, after[i].second, ok ? "" : "  FAILS", aSays);
        if (write)
        {
            auto s = p.state;
            s.b.x = found[i].x;
            s.b.y = found[i].y;
            s.b.distance = found[i].distance;
            s.shiftMs = found[i].shiftMs;
            s.blend = found[i].blend;
            save (p, s);
        }
    }
    std::printf ("%d single mics: %d moved off a pit, %d clear of 3 dB now\n", singles, singlesMoved, singlesClear);
    std::printf ("%d pairs: %d with A moved first; %d kept, %d moved — %d pass, %d still fail%s\n", kept + moved, aMoved, kept, moved, green,
                 red, write ? "; written" : "");
    return 0;
}
