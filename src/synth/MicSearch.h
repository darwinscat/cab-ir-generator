// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

// FIND WITH ONE MIC HEARD (the owner, 05.11): where that mic stands without a pit — about where the player put it, so
// its sound stays its own — judged by what the panel's line reddens by (pitsOf).

#include "Response.h"

namespace cabir::synth
{

/** THE ONE MIC'S PLACE WITHOUT A PIT, in steps as BSearch's are, for whoever spreads them: about where the heard mic
    stands, a third of a radius every way across in steps of a sixth, its distance from 0.6 to 1.6 of its own — its
    model, its angle, the box's horn and back kept — each place rendered as the panel keeps it, and judged by its deepest
    pit (pitsOf, 40 Hz to 8 kHz). Of the places with none the line reddens at — under 3 dB — the nearest is kept, so the
    mic moves no further than it must; with none of them, the shallowest. One round: fine() has none. */
class MicSearch
{
public:
    using Judged = BSearch::Judged;   // score: the deepest pit times -10, dB — the search map's scale, full red at 6 dB

    MicSearch (const Table& t, const State& s, size_t size = 1 << 14)
        : table (t), box (s.box), n (size), heard (pairOf (s).a) {}

    std::vector<Place> coarse() const
    {
        std::vector<Place> out;
        for (const double dx : { -0.3, -0.15, 0.0, 0.15, 0.3 })
            for (const double dy : { -0.3, -0.15, 0.0, 0.15, 0.3 })
                for (const double k : { 0.6, 0.8, 1.0, 1.25, 1.6 })
                {
                    const Place p { heard.x + dx, heard.y + dy, std::clamp (heard.distance * k, 0.01, 0.30) };
                    const bool again = std::any_of (out.begin(), out.end(), [&p] (const Place& q)   // a distance clamped twice
                    { return std::abs (q.x - p.x) < 1e-9 && std::abs (q.y - p.y) < 1e-9 && std::abs (q.distance - p.distance) < 1e-9; });
                    if (std::hypot (p.x, p.y) <= 1.1 && ! again)   // as far as the panel lets a hand drag it
                        out.push_back (p);
                }
        return out;
    }

    std::vector<Place> fine (const std::vector<Place>&, const std::vector<Judged>&) const { return {}; }

    Judged score (const Place& place) const
    {
        auto spot = heard;
        spot.x = place.x;
        spot.y = place.y;
        spot.distance = place.distance;
        const auto ir = render (cabAt (table, box, spot), n, 1);   // one thread a render: the places are spread over them
        const auto pits = pitsOf (responseOf (as24Bit (std::vector<float> (ir.begin(), ir.end())), fs));
        return { -10.0 * (pits.empty() ? 0.0 : (double) *std::max_element (pits.begin(), pits.end())), 0.0 };
    }

    Place best (const std::vector<Place>& first, const std::vector<Judged>& judged,
                const std::vector<Place>& = {}, const std::vector<Judged>& = {}) const
    {
        const auto away = [this] (const Place& p)   // a sixth of a radius across about as far as a quarter nearer or farther
        { return std::hypot (std::hypot (p.x - heard.x, p.y - heard.y), 0.5 * std::abs (std::log2 (p.distance / heard.distance))); };
        constexpr double red = -30.0;   // 3 dB
        long kept = -1;
        for (size_t i = 0; i < first.size() && i < judged.size(); ++i)
        {
            if (judged[i].score <= -1e29)
                continue;   // not judged
            if (kept < 0)
            {
                kept = (long) i;
                continue;
            }
            const auto& k = judged[(size_t) kept];
            const bool clear = judged[i].score > red, keptClear = k.score > red;
            if (clear != keptClear ? clear
                : clear ? away (first[i]) < away (first[(size_t) kept])
                        : judged[i].score > k.score)
                kept = (long) i;
        }
        if (kept < 0)
            return { heard.x, heard.y, heard.distance };
        return first[(size_t) kept];
    }

private:
    const Table& table;
    std::string box;
    size_t n;
    Spot heard;   // the one mic heard, as the synthesis renders it
};

/** ...run on this machine's threads: the place, `tried` hearing of each as it is judged (from whichever thread). */
inline Place smoothPlaceForMic (const Table& table, const State& s, size_t n = 1 << 14,
                                const std::function<void (const Place&, double score)>& tried = {})
{
    const MicSearch search (table, s, n);
    const auto all = search.coarse();
    std::vector<MicSearch::Judged> judged (all.size());
    const auto judge = [&] (size_t i)
    {
        judged[i] = search.score (all[i]);
        if (tried)
            tried (all[i], judged[i].score);
    };
#if defined(__EMSCRIPTEN__) && ! defined(__EMSCRIPTEN_PTHREADS__)
    for (size_t i = 0; i < all.size(); ++i)
        judge (i);
#else
    const size_t lanes = std::clamp<size_t> (std::thread::hardware_concurrency(), 1, 32);
    std::vector<std::future<void>> others;
    for (size_t lane = 1; lane < lanes; ++lane)
        others.push_back (std::async (std::launch::async, [&all, &judge, lane, lanes] { for (size_t i = lane; i < all.size(); i += lanes) judge (i); }));
    for (size_t i = 0; i < all.size(); i += lanes)
        judge (i);
    for (auto& f : others)
        f.get();
#endif
    return search.best (all, judged);
}

} // namespace cabir::synth
