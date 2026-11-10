// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PRESETS' MICS WHERE THE PANEL'S LINE STAYS GREEN — a single mic off its pit, a pair's B off A's spot.
//
//   respread <cabinets.txt> <preset.toml>... [--write]
//   respread <cabinets.txt> <preset.toml>... --tint [--write]
//
// Judged as the panel draws a cabinet: the pit, how far the heard response (a sixth of an octave, as the band draws it)
// falls under its own third of an octave either side, 40 Hz to 8 kHz — a dip a player hears, which no comb shows, 3 dB at
// most, where the line starts to redden (the owner, 05.11; a table's own where it says one, the guitar's 7, synth::pitRedOf) — and with two mics where they meet (synth::combOf, 30 Hz to
// 12 kHz), its worst point green above -3 dB.
// A single mic with a pit moves as FIND moves one (synth::MicSearch): about where it stands, the nearest place without
// one. A pair, in two steps (the owner, 05.11): first A alone, the same way, if A by itself has a pit — B rarely fills
// one — then B. A pair whose B stands on A's spot (under half a radius across), or that fails either, has B moved: its
// mics, A's place, the blend and the clock kept, B tried across the speaker at least half a radius from A — a coarse
// round at its own distance and a quarter either side, each place at its best TIME (-0.5 to +0.5 ms), then finer round
// the six best — the place whose worse margin is the widest kept, ties to the smaller shift, then the nearer place. Up to
// two pairs a style that stand on one axis and pass already stay as they are: a close mic and a far one on one axis is a
// way too.
//
// --tint, on a table whose comb may cancel deeper from 2 kHz up (synth::combTopRedOf, the guitar's 6): half of each
// style's pairs given a comb a player hears up there — the second mic's phase colour (the owner, 07.11) — B moved as
// little as it takes: about where it stands (a seventh of a radius either way, 0.8 to 1.25 of its distance), its TIME
// within 0.3 ms of its own, its mic, A and the blend kept; the place kept where the pair cancels 3.5 to 6 dB from 2 kHz up,
// no more than 3 under it, without a pit — the nearest such, judged again on the app's own path at the preset's
// precision. The pairs it finds that for with the least move, half a style's at most; the rest stay.

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

    // --TINT ---------------------------------------------------------------------------------------------------------
    constexpr double tintLightest = -3.5, tintDeepest = -6.0, lowGreen = -3.0;

    /** A pair's comb under 2 kHz and from 2 kHz up, apart, and its pit — from its two spectra, B turned by `tau` s. */
    struct Bands { double low, top, pit; };

    Bands bandsOf (const std::vector<synth::cd>& a, const std::vector<synth::cd>& b, std::vector<synth::cd>& sum, double tau)
    {
        constexpr double rate = 48000.0;
        const size_t n = a.size();
        for (size_t k = 0; k <= n / 2; ++k)
            sum[k] = a[k] + b[k] * std::polar (1.0, -2 * synth::pi * rate * (double) k / (double) n * tau);
        const double narrow = std::pow (2.0, 1.0 / 48.0), sixth = std::pow (2.0, 1.0 / 12.0);
        Bands out { 1e9, 1e9, 0 };
        std::vector<double> heard ((size_t) synth::responsePoints);
        for (int i = 0; i < synth::responsePoints; ++i)
        {
            const double hz = synth::responseHz (i);
            const double c = 10.0 * std::log10 (synth::detail::powerAt (sum, rate, hz, narrow)
                                                / (synth::detail::powerAt (a, rate, hz, narrow) + synth::detail::powerAt (b, rate, hz, narrow)));
            (hz >= synth::combTopHz ? out.top : out.low) = std::min (hz >= synth::combTopHz ? out.top : out.low, c);
            heard[(size_t) i] = 10.0 * std::log10 (synth::detail::powerAt (sum, rate, hz, sixth));
        }
        out.pit = pitOf (heard);
        return out;
    }

    /** ...the same on the app's own path, as the panel draws it. */
    Bands bandsOf (const synth::Table& table, const State& s)
    {
        const auto parts = synth::PairRenderer {}.render (table, s.box, synth::pairOf (s));
        const auto comb = synth::combOf (parts.a, parts.b);
        const auto heard = synth::responseOf (synth::as24Bit (parts.mix), 48000.0);
        Bands out { 1e9, 1e9, pitOf (std::vector<double> (heard.begin(), heard.end())) };
        for (size_t i = 0; i < comb.size(); ++i)
        {
            auto& band = synth::responseHz ((int) i) >= synth::combTopHz ? out.top : out.low;
            band = std::min (band, (double) comb[i]);
        }
        return out;
    }

    bool tinted (const Bands& b, double pitLimit)
    {
        return b.low >= lowGreen && b.pit <= pitLimit && b.top <= tintLightest && b.top >= tintDeepest;
    }

    double rounded (double v) { return std::round (v * 1000.0) / 1000.0; }   // the preset's precision

    /** B where the pair has its tint, the least move from where it stands — or `found` false. */
    struct Tint { bool found = false; State state; double cost = 1e9; Bands bands {}; };

    Tint tintOf (const synth::Table& table, const State& s, double pitLimit)
    {
        synth::PairRenderer renderer;   // A is rendered once: only B moves
        std::vector<synth::cd> sum;
        const double nearA = std::min (apartRadii, apart (s));   // B no nearer A than it stands, nor than half a radius
        std::vector<Tint> candidates;
        for (const double dx : { 0.0, -0.075, 0.075, -0.15, 0.15 })
            for (const double dy : { 0.0, -0.075, 0.075, -0.15, 0.15 })
                for (const double k : { 1.0, 0.9, 1.1, 0.8, 1.25 })
                {
                    auto here = s;
                    here.b.x = rounded (s.b.x + dx);
                    here.b.y = rounded (s.b.y + dy);
                    here.b.distance = rounded (std::clamp (s.b.distance * k, 0.02, 0.30));
                    if (std::hypot (here.b.x, here.b.y) > 1.05 || apart (here) < nearA - 1e-9)
                        continue;
                    here.shiftMs = 0;
                    const auto parts = renderer.render (table, s.box, synth::pairOf (here));
                    const int length = (int) std::min (parts.a.size(), parts.b.size());
                    const auto a = synth::detail::spectrumOf (parts.a.data(), length), b = synth::detail::spectrumOf (parts.b.data(), length);
                    sum.assign (a.size(), 0.0);
                    for (int step = -12; step <= 12; ++step)
                    {
                        const double ms = rounded (std::clamp (s.shiftMs + step * 0.025, -0.5, 0.5));
                        const auto bands = bandsOf (a, b, sum, ms * 1e-3);
                        if (! tinted (bands, pitLimit))
                            continue;
                        auto c = here;
                        c.shiftMs = ms;
                        const double cost = std::hypot (dx, dy) / 0.15 + std::abs (std::log (k)) / 0.2 + std::abs (ms - s.shiftMs) / 0.3;
                        candidates.push_back ({ true, c, cost, bands });
                    }
                }
        std::stable_sort (candidates.begin(), candidates.end(), [] (const Tint& x, const Tint& y) { return x.cost < y.cost; });
        for (const auto& c : candidates)   // the nearest that holds on the app's own path
            if (const auto real = bandsOf (table, c.state); tinted (real, pitLimit))
                return { true, c.state, c.cost, real };
        return {};
    }
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "respread <cabinets.txt> <preset.toml>... [--write]\n"); return 2; }
    const auto table = synth::readTable (argv[1]);
    bool write = false, tint = false;
    std::vector<Preset> all;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--write") == 0) { write = true; continue; }
        if (std::strcmp (argv[i], "--tint") == 0) { tint = true; continue; }
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

    if (tint)
    {
        // Each pair's nearest tint, two at a time — a render spreads over the machine's threads itself; then half a style's
        // at most, the least moved first.
        std::vector<Tint> tints (all.size());
        std::vector<Bands> before (all.size());
        {
            std::vector<std::future<void>> work;
            constexpr size_t lanes = 2;
            for (size_t lane = 0; lane < lanes; ++lane)
                work.push_back (std::async (std::launch::async, [&, lane]
                {
                    for (size_t i = lane; i < all.size(); i += lanes)
                        if (all[i].pair)
                        {
                            before[i] = bandsOf (table, all[i].state);
                            tints[i] = tintOf (table, all[i].state, synth::pitRedOf (table, all[i].state.box));
                        }
                }));
            for (auto& w : work) w.get();
        }
        std::map<std::string, std::vector<size_t>> byStyle;
        std::map<std::string, int> pairsOf;
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].pair)
            {
                ++pairsOf[all[i].style];
                if (tints[i].found)
                    byStyle[all[i].style].push_back (i);
            }
        std::vector<bool> chosen (all.size(), false);
        for (auto& [style, list] : byStyle)
        {
            std::stable_sort (list.begin(), list.end(), [&tints] (size_t a, size_t b) { return tints[a].cost < tints[b].cost; });
            const size_t half = (size_t) (pairsOf[style] + 1) / 2;
            for (size_t k = 0; k < list.size() && k < half; ++k)
                chosen[list[k]] = true;
        }
        const auto save = [] (const Preset& p, const State& s)
        {
            t::Table head;
            (void) head.insert ("name", t::Value (p.name));
            (void) head.insert ("style", t::Value (p.style));
            std::ofstream (p.path) << t::write (head) + toToml (s);
        };
        int count = 0;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& p = all[i];
            if (! p.pair)
                continue;
            const auto& b = before[i];
            if (! chosen[i])
            {
                std::printf ("KEPT  %-30s low %5.1f top %5.1f pit %4.1f%s\n", p.name.c_str(), b.low, b.top, b.pit,
                             tints[i].found ? "  (a tint found, not needed)" : "");
                continue;
            }
            ++count;
            const auto& n = tints[i].state;
            const auto& a = tints[i].bands;
            std::printf ("TINT  %-30s low %5.1f top %5.1f pit %4.1f -> low %5.1f top %5.1f pit %4.1f   B %+.3f %+.3f %4.1f cm %+.3f ms -> %+.3f %+.3f %4.1f cm %+.3f ms\n",
                         p.name.c_str(), b.low, b.top, b.pit, a.low, a.top, a.pit, p.state.b.x, p.state.b.y, p.state.b.distance * 100,
                         p.state.shiftMs, n.b.x, n.b.y, n.b.distance * 100, n.shiftMs);
            if (write)
                save (p, n);
        }
        std::printf ("%d pairs tinted%s\n", count, write ? "; written" : "");
        return 0;
    }

    // A single mic, and a pair's A heard alone: off its pit first, each search on all the machine's threads.
    // Where the line reddens for a preset's box: its table's (synth::pitRedOf — the guitar's 7 dB, the bass's 3).
    const auto limit = [&table] (const State& st) { return synth::pitRedOf (table, st.box); };
    int singles = 0, singlesMoved = 0, singlesClear = 0;
    for (auto& p : all)
    {
        const double line = limit (p.state);
        auto alone = p.state;
        if (p.pair)
            alone.b.on = false;
        const double before = judgeOf (table, alone).second;
        double now = before;
        if (before > line)
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
        singlesClear += now <= line;
        const auto& m = p.state.a.on ? p.state.a : p.state.b;
        const auto& w = p.was.a.on ? p.was.a : p.was.b;
        if (before > line)
            std::printf ("ONE   %-34s pit %5.1f -> %5.1f   %+.2f %+.2f %4.1f cm -> %+.2f %+.2f %4.1f cm%s\n", p.name.c_str(), before, now,
                         w.x, w.y, w.distance * 100, m.x, m.y, m.distance * 100, now <= line ? "" : "  STILL RED");
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
        std::stable_sort (list.begin(), list.end(), [&all, &limit] (size_t a, size_t b)
                          { return margin (all[a].worst, all[a].pit, limit (all[a].state)) > margin (all[b].worst, all[b].pit, limit (all[b].state)); });
        for (size_t k = 0, kept = 0; k < list.size() && kept < 2; ++k)
            if (passes (all[list[k]].worst, all[list[k]].pit, limit (all[list[k]].state)))
            {
                keep[list[k]] = true;
                ++kept;
            }
    }

    std::vector<size_t> moving;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].pair && ! keep[i] && (all[i].apart < apartRadii || ! passes (all[i].worst, all[i].pit, limit (all[i].state))))
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
        const bool ok = passes (after[i].first, after[i].second, limit (p.state));
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
    std::printf ("%d single mics: %d moved off a pit, %d clear of the line now\n", singles, singlesMoved, singlesClear);
    std::printf ("%d pairs: %d with A moved first; %d kept, %d moved — %d pass, %d still fail%s\n", kept + moved, aMoved, kept, moved, green,
                 red, write ? "; written" : "");
    return 0;
}
