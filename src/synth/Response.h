// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

// WHAT THE PANEL'S RESPONSE BAND DRAWS, computed without JUCE, so the app, a plugin and the page draw it from the same
// numbers: the IR's response, and where a pair's two mics comb, at the same points.

#include "CabSynth.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cabir::synth
{

/** The points the band is drawn at: from 30 Hz to 12 kHz, evenly on a log scale. */
constexpr int responsePoints = 240;
inline float responseHz (int i) { return 30.0f * std::pow (12000.0f / 30.0f, (float) i / (float) (responsePoints - 1)); }

/** An IR as a 24-bit WAV holds it (wav24) — what a player downloads, and what the band is drawn from. */
inline std::vector<float> as24Bit (const std::vector<float>& x)
{
    std::vector<float> q (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        q[i] = (float) std::lround (std::clamp ((double) x[i], -1.0, 1.0 - 1.0 / 8388608.0) * 8388608.0) / 8388608.0f;
    return q;
}

namespace detail
{
    /** A signal's spectrum, padded far past it, so a narrow band at 30 Hz still spans bins enough. */
    inline std::vector<cd> spectrumOf (const float* x, int length)
    {
        size_t n = 1 << 16;
        while (n < (size_t) length)
            n <<= 1;
        std::vector<cd> s (n, 0.0);
        for (int i = 0; i < length; ++i)
            s[(size_t) i] = x[i];
        fft (s, false);
        return s;
    }

    /** Its power round `hz`, the bins from hz / ratio to hz * ratio averaged. */
    inline double powerAt (const std::vector<cd>& s, double rate, double hz, double ratio)
    {
        const double binHz = rate / (double) s.size();
        const double top = (double) (s.size() / 2 - 1);
        const auto lo = (size_t) std::clamp (std::floor (hz / ratio / binHz), 1.0, top);
        const auto hi = (size_t) std::clamp (std::ceil (hz * ratio / binHz), (double) lo, top);
        double power = 0;
        for (size_t k = lo; k <= hi; ++k)
            power += std::norm (s[k]);
        return power / (double) (hi - lo + 1) + 1.0e-20;
    }
}

/** AN IR'S RESPONSE: a sixth of an octave wide each point, in dB, levelled to its own middle (100 Hz .. 5 kHz); its first
    65536 samples at `rate`. Empty for an empty IR. */
inline std::vector<float> responseOf (const std::vector<float>& ir, double rate)
{
    if (ir.empty() || rate <= 0)
        return {};
    const int length = (int) std::min<size_t> (ir.size(), 1 << 16);
    const auto s = detail::spectrumOf (ir.data(), length);

    std::vector<float> db ((size_t) responsePoints);
    double middle = 0;
    int counted = 0;
    for (int i = 0; i < responsePoints; ++i)
    {
        const double hz = responseHz (i);
        db[(size_t) i] = (float) (10.0 * std::log10 (detail::powerAt (s, rate, hz, std::pow (2.0, 1.0 / 12.0))));
        if (hz >= 100.0 && hz <= 5000.0)
        {
            middle += db[(size_t) i];
            ++counted;
        }
    }
    for (auto& v : db)
        v -= (float) (middle / std::max (1, counted));
    return db;
}

/** ...and where a pair's two mics meet, at the same points — each mic's part of the pair at the mix's level, `rate` Hz:
    in dB, the mix against the two powers apart, above nought where they add in phase, below where they cancel. */
inline std::vector<float> combOf (const std::vector<float>& partA, const std::vector<float>& partB, double rate = 48000.0)
{
    const int length = (int) std::min (partA.size(), partB.size());
    if (length <= 0)
        return {};
    const auto a = detail::spectrumOf (partA.data(), length), b = detail::spectrumOf (partB.data(), length);
    std::vector<cd> sum (a.size());
    for (size_t k = 0; k < a.size(); ++k)
        sum[k] = a[k] + b[k];

    // Narrow, a 48th of an octave each way: a comb's teeth are finer than the response's sixth.
    const double ratio = std::pow (2.0, 1.0 / 48.0);
    std::vector<float> db ((size_t) responsePoints);
    for (int i = 0; i < responsePoints; ++i)
    {
        const double hz = responseHz (i);
        db[(size_t) i] = (float) (10.0 * std::log10 (detail::powerAt (sum, rate, hz, ratio)
                                                     / (detail::powerAt (a, rate, hz, ratio) + detail::powerAt (b, rate, hz, ratio))));
    }
    return db;
}

} // namespace cabir::synth
