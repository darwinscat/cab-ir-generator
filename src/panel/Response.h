// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace cabir
{

/** AN IR'S RESPONSE as the panel draws it (CabPicker::responseHz): a sixth of an octave wide each, in dB, levelled to its
    own middle (100 Hz .. 5 kHz). Empty for bytes that do not decode. */
std::vector<float> responseOf (const juce::MemoryBlock& bytes);

/** ...and where a pair's two mics meet, at the same points — each mic's part of the pair at the mix's level, `rate` Hz:
    in dB, the mix against the two powers apart, above nought where they add in phase, below where they cancel. */
std::vector<float> combOf (const std::vector<float>& a, const std::vector<float>& b, double rate = 48000.0);

} // namespace cabir
