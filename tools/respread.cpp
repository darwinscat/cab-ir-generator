// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE PRESETS' PAIRS SPREAD APART — B off A's spot, where the two comb least as the panel draws it.
//
//   respread <cabinets.txt> <preset.toml>... [--write]
//
// Judged twice, as the panel draws a pair: where the two mics meet (synth::combOf, 30 Hz to 12 kHz) — its worst point,
// green above -3 dB — and the pit, how far the heard response (a sixth of an octave, as the band draws it) falls under
// its own third of an octave either side, 40 Hz to 8 kHz — a dip a player hears, which no comb shows; 6 dB at most.
// A pair whose B stands on A's spot (under half a radius across), or that fails either, is moved: its mics, A's place,
// the blend and the clock kept, B tried across the speaker at least half a radius from A — a coarse round at its own
// distance and a quarter either side, each place at its best TIME (-0.5 to +0.5 ms), then finer round the six best —
// the place whose worse margin is the widest kept, ties to the smaller shift, then the nearer place. Up to two pairs a
// style that stand on one axis and pass already stay as they are: a close mic and a far one on one axis is a way too.

#include <synth/CabSynth.h>
#include <synth/Response.h>
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
    namespace t = felitronics::toml;
    using synth::cd;

    constexpr double apartRadii = 0.5;   // B at least this far across from A
    constexpr double greenDb = -3.0;     // a worst point above this: the band green
    constexpr double pitDb = 6.0;        // the response at most this far under its neighbours

    /** How far the response falls under its own third of an octave either side, 40 Hz to 8 kHz, at its worst. */
    double pitOf (const std::vector<double>& db)
    {
        const int n = (int) db.size(), reach = (int) std::lround ((double) (n - 1) / std::log2 (12000.0 / 30.0) / 3.0);
        double worst = 0;
        for (int i = 0; i < n; ++i)
        {
            const double hz = synth::responseHz (i);
            if (hz < 40.0 || hz > 8000.0) continue;
            double sum = 0;
            int count = 0;
            for (int k = std::max (0, i - reach); k <= std::min (n - 1, i + reach); ++k) { sum += db[(size_t) k]; ++count; }
            worst = std::max (worst, sum / count - db[(size_t) i]);
        }
        return worst;
    }

    /** Both, from a pair's two spectra with B turned by `tau` s: the worst comb point and the pit. */
    std::pair<double, double> judge (const std::vector<cd>& a, const std::vector<cd>& b, std::vector<cd>& sum, double tau)
    {
        constexpr double rate = 48000.0;
        const size_t n = a.size();
        for (size_t k = 0; k <= n / 2; ++k)
            sum[k] = a[k] + b[k] * std::polar (1.0, -2 * synth::pi * rate * (double) k / (double) n * tau);
        const double narrow = std::pow (2.0, 1.0 / 48.0), sixth = std::pow (2.0, 1.0 / 12.0);
        double worst = 1e9;
        std::vector<double> heard ((size_t) synth::responsePoints);
        for (int i = 0; i < synth::responsePoints; ++i)
        {
            const double hz = synth::responseHz (i);
            worst = std::min (worst, 10.0 * std::log10 (synth::detail::powerAt (sum, rate, hz, narrow)
                                                        / (synth::detail::powerAt (a, rate, hz, narrow) + synth::detail::powerAt (b, rate, hz, narrow))));
            heard[(size_t) i] = 10.0 * std::log10 (synth::detail::powerAt (sum, rate, hz, sixth));
        }
        return { worst, pitOf (heard) };
    }

    /** The narrower of the two margins: how far inside both bounds a pair stands (negative: outside one). */
    double margin (double worst, double pit) { return std::min (worst - greenDb, pitDb - pit); }

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

    /** The pair as the panel draws it, on the app's own path: the worst comb point and the pit of what is heard. */
    std::pair<double, double> judgeOf (const synth::Table& table, const State& s)
    {
        const auto parts = synth::PairRenderer {}.render (table, s.box, synth::pairOf (s));
        const auto comb = synth::combOf (parts.a, parts.b);
        const auto heard = synth::responseOf (synth::as24Bit (parts.mix), 48000.0);
        return { comb.empty() ? 0.0 : (double) *std::min_element (comb.begin(), comb.end()),
                 pitOf (std::vector<double> (heard.begin(), heard.end())) };
    }

    struct Found { double x, y, distance, shiftMs, worst, pit, blend; };

    /** B across the speaker, off A's spot, each place at its best TIME — by turning B's spectrum, as the panel's band
        would read it: a coarse round, then finer round the six best. `wide`, for a pair that fails that: B's distance
        from 0.6 to 1.5 of its own, and the blend moved by as much as 0.15 either way — the least that passes. */
    Found spread (const synth::Table& table, const State& s, bool wide)
    {
        synth::PairRenderer renderer;   // A is rendered once: only B moves
        std::vector<cd> sum, bb;
        std::vector<double> blends { s.blend };
        if (wide)   // either way: where A is weak, less of B can meet A the more evenly and cancel it the deeper
            for (const double by : { -0.05, 0.05, -0.10, 0.10, -0.15, 0.15 })
                if (s.blend + by >= 0.15 && s.blend + by <= 0.85)
                    blends.push_back (s.blend + by);
        // A margin past a dB is margin enough: then the least change of blend, the smaller shift, the nearer place.
        const auto better = [&s] (const Found& c, const Found& best)
        {
            const double mc = std::min (1.0, margin (c.worst, c.pit)), mb = std::min (1.0, margin (best.worst, best.pit));
            if (mc > mb + 0.05) return true;
            if (mc < mb - 0.05) return false;
            const double bc = std::abs (c.blend - s.blend), bb_ = std::abs (best.blend - s.blend);
            if (bc < bb_ - 1e-9) return true;
            if (bc > bb_ + 1e-9) return false;
            if (std::abs (c.shiftMs) < std::abs (best.shiftMs) - 1e-9) return true;
            if (std::abs (c.shiftMs) > std::abs (best.shiftMs) + 1e-9) return false;
            return std::hypot (c.x - s.b.x, c.y - s.b.y) < std::hypot (best.x - s.b.x, best.y - s.b.y) - 1e-9;
        };
        // One place at a set of shifts: its best.
        const auto tryPlace = [&] (const synth::Place& p, const std::vector<double>& shiftsMs)
        {
            auto here = s;
            here.b.x = p.x;
            here.b.y = p.y;
            here.b.distance = p.distance;
            here.shiftMs = 0;
            const auto parts = renderer.render (table, s.box, synth::pairOf (here));
            const int length = (int) std::min (parts.a.size(), parts.b.size());
            const auto a = synth::detail::spectrumOf (parts.a.data(), length), b = synth::detail::spectrumOf (parts.b.data(), length);
            sum.assign (a.size(), 0.0);
            Found best { p.x, p.y, p.distance, 0, -1e9, 1e9, s.blend };
            for (const double blend : blends)
            {
                // B's part at another blend: the mix's two gains are (1 - blend) and blend over each mic's middle.
                const double k = (blend / (1 - blend)) / (s.blend / (1 - s.blend));
                bb.resize (b.size());
                for (size_t i = 0; i < b.size(); ++i) bb[i] = b[i] * k;
                for (const double ms : shiftsMs)
                {
                    const auto [worst, pit] = judge (a, bb, sum, ms * 1e-3);
                    const Found c { p.x, p.y, p.distance, ms, worst, pit, blend };
                    if (better (c, best)) best = c;
                }
            }
            return best;
        };
        const auto onSpeaker = [&s] (const synth::Place& p)
        { return std::hypot (p.x, p.y) <= 1.05 && std::hypot (p.x - s.a.x, p.y - s.a.y) >= apartRadii; };

        std::vector<double> coarseShifts;
        for (int step = -10; step <= 10; ++step) coarseShifts.push_back (step * 0.05);
        std::vector<Found> round;
        const double d0 = s.b.distance;
        const std::vector<double> distances = wide ? std::vector<double> { 0.6, 0.75, 0.9, 1.0, 1.15, 1.3, 1.5 }
                                                   : std::vector<double> { 0.75, 1.0, 1.25 };
        for (const double k : distances)
            for (double x = -0.9; x <= 0.901; x += 0.15)
                for (const double y : { -0.35, 0.0, 0.35 })
                    if (const synth::Place p { x, y, std::clamp (d0 * k, 0.02, 0.30) }; onSpeaker (p))
                        round.push_back (tryPlace (p, coarseShifts));
        std::stable_sort (round.begin(), round.end(), [&better] (const Found& a, const Found& b) { return better (a, b); });

        Found best = round.empty() ? Found { s.b.x, s.b.y, s.b.distance, s.shiftMs, -1e9, 1e9, s.blend } : round.front();
        for (size_t seed = 0; seed < std::min<size_t> (6, round.size()); ++seed)
        {
            const auto at = round[seed];
            std::vector<double> fineShifts;
            for (int step = -4; step <= 4; ++step) fineShifts.push_back (std::clamp (at.shiftMs + step * 0.025, -0.5, 0.5));
            for (const double dx : { -0.075, 0.0, 0.075 })
                for (const double dy : { -0.075, 0.0, 0.075 })
                    for (const double k : { 0.9, 1.0, 1.1 })
                        if (const synth::Place p { at.x + dx, at.y + dy, std::clamp (at.distance * k, 0.02, 0.30) }; onSpeaker (p))
                            if (const auto c = tryPlace (p, fineShifts); better (c, best))
                                best = c;
        }
        return best;
    }

    struct Preset { std::string path, name, style, head; State state; bool pair = false; double worst = 0, pit = 0, apart = 0; };
    bool passes (double worst, double pit) { return worst >= greenDb && pit <= pitDb; }
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
        all.push_back (std::move (p));
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
                    const auto placed = [&] (const Found& f)
                    {
                        auto s = all[i].state;
                        s.b.x = f.x;
                        s.b.y = f.y;
                        s.b.distance = f.distance;
                        s.shiftMs = f.shiftMs;
                        s.blend = f.blend;
                        return s;
                    };
                    found[i] = spread (table, all[i].state, false);
                    after[i] = judgeOf (table, placed (found[i]));   // the real path, B moved in time as the app moves it
                    if (! passes (after[i].first, after[i].second))
                    {
                        // Wider: B's distance further either way, B's share of the blend a little less.
                        const auto wider = spread (table, all[i].state, true);
                        const auto there = judgeOf (table, placed (wider));
                        if (margin (there.first, there.second) > margin (after[i].first, after[i].second))
                        {
                            found[i] = wider;
                            after[i] = there;
                        }
                    }
                }
            }));
        for (auto& w : work) w.get();
    }

    int moved = 0, green = 0, red = 0, kept = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& p = all[i];
        if (! p.pair)
            continue;
        if (std::find (moving.begin(), moving.end(), i) == moving.end())
        {
            ++kept;
            std::printf ("KEPT  %-34s apart %.2f  comb %6.1f pit %5.1f%s\n", p.name.c_str(), p.apart, p.worst, p.pit, keep[i] ? "  (one axis)" : "");
            continue;
        }
        ++moved;
        const bool ok = passes (after[i].first, after[i].second);
        (ok ? green : red)++;
        char was[24] = "";
        if (std::abs (found[i].blend - p.state.blend) > 1e-9)
            std::snprintf (was, sizeof was, " (was %.2f)", p.state.blend);
        std::printf ("MOVED %-34s apart %.2f comb %6.1f pit %5.1f -> B %+.3f %+.3f %4.1f cm %+.3f ms blend %.2f%s, apart %.2f comb %6.1f pit %5.1f%s\n",
                     p.name.c_str(), p.apart, p.worst, p.pit, found[i].x, found[i].y, found[i].distance * 100, found[i].shiftMs,
                     found[i].blend, was,
                     std::hypot (found[i].x - p.state.a.x, found[i].y - p.state.a.y), after[i].first, after[i].second, ok ? "" : "  FAILS");
        if (write)
        {
            auto s = p.state;
            s.b.x = found[i].x;
            s.b.y = found[i].y;
            s.b.distance = found[i].distance;
            s.shiftMs = found[i].shiftMs;
            s.blend = found[i].blend;
            t::Table head;
            (void) head.insert ("name", t::Value (p.name));
            (void) head.insert ("style", t::Value (p.style));
            std::ofstream (p.path) << t::write (head) + toToml (s);
        }
    }
    std::printf ("%d pairs: %d kept, %d moved — %d pass, %d still fail%s\n", kept + moved, kept, moved, green, red, write ? "; written" : "");
    return 0;
}
