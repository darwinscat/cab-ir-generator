// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

// A CABINET'S MICS WHERE THE PANEL'S LINE STAYS GREEN — for whoever spreads them: tools/respread, the factory presets;
// tools/cabpack, the packs' typical cabinets.
//
// Judged as the panel draws a cabinet: the pit, how far the heard response (a sixth of an octave, as the band draws it)
// falls under its own third of an octave either side, 40 Hz to 8 kHz — a dip a player hears, which no comb shows, 3 dB at
// most, where the line starts to redden (the owner, 05.11) — and with two mics where they meet (synth::combOf, 30 Hz to
// 12 kHz), its worst point green above -3 dB.
// A single mic with a pit moves as FIND moves one (synth::MicSearch): about where it stands, the nearest place without
// one. A pair, in two steps (the owner, 05.11): first A alone, the same way, if A by itself has a pit — B rarely fills
// one — then B: its mics, A's place, the blend and the clock kept, B tried across the speaker at least half a radius from
// A — a coarse round at its own distance and a quarter either side, each place at its best TIME (-0.5 to +0.5 ms), then
// finer round the six best — the place whose worse margin is the widest kept, ties to the smaller shift, then the nearer
// place; where that fails, wider (spreadB).

#include "CabSynth.h"
#include "MicSearch.h"
#include "Response.h"
#include "State.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace cabir::synth::green
{

constexpr double apartRadii = 0.5;   // B at least this far across from A
constexpr double greenDb = -3.0;     // a worst point above this: the band green
constexpr double pitDb = 3.0;        // the response at most this far under its neighbours: where the panel's line reddens
                                     // (the bass's; a table's own, synth::pitRedOf — the guitar's 5 — where it says one)

/** How far the response falls under its own third of an octave either side, 40 Hz to 8 kHz, at its worst. */
inline double pitOf (const std::vector<double>& db)
{
    const auto pits = synth::pitsOf (db);
    return pits.empty() ? 0.0 : *std::max_element (pits.begin(), pits.end());
}

/** Both, from a pair's two spectra with B turned by `tau` s: the worst comb point and the pit. */
inline std::pair<double, double> judge (const std::vector<cd>& a, const std::vector<cd>& b, std::vector<cd>& sum, double tau)
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
inline double margin (double worst, double pit, double pitLimit = pitDb) { return std::min (worst - greenDb, pitLimit - pit); }

inline bool passes (double worst, double pit, double pitLimit = pitDb) { return worst >= greenDb && pit <= pitLimit; }

/** The pair as the panel draws it, on the app's own path: the worst comb point and the pit of what is heard. */
inline std::pair<double, double> judgeOf (const synth::Table& table, const State& s)
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
inline Found spread (const synth::Table& table, const State& s, bool wide)
{
    synth::PairRenderer renderer;   // A is rendered once: only B moves
    std::vector<cd> sum, bb;
    std::vector<double> blends { s.blend };
    if (wide)   // either way: where A is weak, less of B can meet A the more evenly and cancel it the deeper
        for (const double by : { -0.05, 0.05, -0.10, 0.10, -0.15, 0.15 })
            if (s.blend + by >= 0.15 && s.blend + by <= 0.85)
                blends.push_back (s.blend + by);
    // A margin past a dB is margin enough: then the least change of blend, the smaller shift, the nearer place.
    const double red = synth::pitRedOf (table, s.box);
    const auto better = [&s, red] (const Found& c, const Found& best)
    {
        const double mc = std::min (1.0, margin (c.worst, c.pit, red)), mb = std::min (1.0, margin (best.worst, best.pit, red));
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

/** `s` with B where `f` found it: its place, its TIME, the blend. */
inline State withB (const State& s, const Found& f)
{
    auto out = s;
    out.b.x = f.x;
    out.b.y = f.y;
    out.b.distance = f.distance;
    out.shiftMs = f.shiftMs;
    out.blend = f.blend;
    return out;
}

/** B spread (spread), and where that fails, wider — the better of the two kept: where it stands, and how it is judged
    there on the real path, B moved in time as the app moves it. */
struct SpreadB { Found found; std::pair<double, double> after; };

inline SpreadB spreadB (const synth::Table& table, const State& s)
{
    const double red = synth::pitRedOf (table, s.box);
    SpreadB out { spread (table, s, false), {} };
    out.after = judgeOf (table, withB (s, out.found));
    if (! passes (out.after.first, out.after.second, red))
    {
        // Wider: B's distance further either way, B's share of the blend a little less.
        const auto wider = spread (table, s, true);
        const auto there = judgeOf (table, withB (s, wider));
        if (margin (there.first, there.second, red) > margin (out.after.first, out.after.second, red))
            out = { wider, there };
    }
    return out;
}

/** One mic heard (A, else B) moved where it has no pit, about where it stands (synth::MicSearch, as FIND moves it). */
inline State smoothed (const synth::Table& table, const State& s)
{
    const auto place = synth::smoothPlaceForMic (table, s);
    auto out = s;
    auto& m = s.a.on ? out.a : out.b;
    m.x = place.x;
    m.y = place.y;
    m.distance = place.distance;
    return out;
}

} // namespace cabir::synth::green
