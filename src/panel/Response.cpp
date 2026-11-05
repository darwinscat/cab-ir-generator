// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "Response.h"

#include "../synth/Response.h"

namespace cabir
{

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
    return synth::responseOf (std::vector<float> (ir.getReadPointer (0), ir.getReadPointer (0) + length), reader->sampleRate);
}

std::vector<float> combOf (const std::vector<float>& partA, const std::vector<float>& partB, double rate)
{
    return synth::combOf (partA, partB, rate);
}

} // namespace cabir
