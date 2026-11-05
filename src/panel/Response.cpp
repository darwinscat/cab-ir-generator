// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "Response.h"

#include "CabPicker.h"
#include "../synth/CabSynth.h"

namespace cabir
{

namespace
{
    using synth::cd;

    /** A signal's spectrum, padded far past it, so a narrow band at 30 Hz still spans bins enough. */
    std::vector<cd> spectrumOf (const float* x, int length)
    {
        size_t n = 1 << 16;
        while (n < (size_t) length)
            n <<= 1;
        std::vector<cd> s (n, 0.0);
        for (int i = 0; i < length; ++i)
            s[(size_t) i] = x[i];
        synth::fft (s, false);
        return s;
    }

    /** Its power round `hz`, the bins from hz / ratio to hz * ratio averaged. */
    double powerAt (const std::vector<cd>& s, double rate, double hz, double ratio)
    {
        const double binHz = rate / (double) s.size();
        const double top = (double) (s.size() / 2 - 1);
        const auto lo = (size_t) juce::jlimit (1.0, top, std::floor (hz / ratio / binHz));
        const auto hi = (size_t) juce::jlimit ((double) lo, top, std::ceil (hz * ratio / binHz));
        double power = 0;
        for (size_t k = lo; k <= hi; ++k)
            power += std::norm (s[k]);
        return power / (double) (hi - lo + 1) + 1.0e-20;
    }
}

std::vector<float> responseOf (const juce::MemoryBlock& bytes)
{
    juce::AudioFormatManager formats;   // a few objects, at most ten times a second
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, false)));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
        return {};

    const int length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 1 << 16);
    juce::AudioBuffer<float> ir (1, length);
    reader->read (&ir, 0, length, 0, true, false);
    const auto s = spectrumOf (ir.getReadPointer (0), length);

    std::vector<float> db ((size_t) CabPicker::responsePoints);
    double middle = 0;
    int counted = 0;
    for (int i = 0; i < CabPicker::responsePoints; ++i)
    {
        const double hz = CabPicker::responseHz (i);
        db[(size_t) i] = (float) (10.0 * std::log10 (powerAt (s, reader->sampleRate, hz, std::pow (2.0, 1.0 / 12.0))));
        if (hz >= 100.0 && hz <= 5000.0)
        {
            middle += db[(size_t) i];
            ++counted;
        }
    }
    for (auto& v : db)
        v -= (float) (middle / juce::jmax (1, counted));
    return db;
}

std::vector<float> combOf (const std::vector<float>& partA, const std::vector<float>& partB, double rate)
{
    const int length = (int) juce::jmin (partA.size(), partB.size());
    if (length <= 0)
        return {};
    const auto a = spectrumOf (partA.data(), length), b = spectrumOf (partB.data(), length);
    std::vector<cd> sum (a.size());
    for (size_t k = 0; k < a.size(); ++k)
        sum[k] = a[k] + b[k];

    // Narrow, a 48th of an octave each way: a comb's teeth are finer than the response's sixth.
    const double ratio = std::pow (2.0, 1.0 / 48.0);
    std::vector<float> db ((size_t) CabPicker::responsePoints);
    for (int i = 0; i < CabPicker::responsePoints; ++i)
    {
        const double hz = CabPicker::responseHz (i);
        db[(size_t) i] = (float) (10.0 * std::log10 (powerAt (sum, rate, hz, ratio)
                                                     / (powerAt (a, rate, hz, ratio) + powerAt (b, rate, hz, ratio))));
    }
    return db;
}

} // namespace cabir
