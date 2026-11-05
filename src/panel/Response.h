// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace cabir
{

/** AN IR'S RESPONSE as the panel draws it, from a WAV's bytes — synth::responseOf (synth/Response.h) once they are
    decoded: a sixth of an octave wide each point, in dB, levelled to its own middle. Empty for bytes that do not decode. */
std::vector<float> responseOf (const juce::MemoryBlock& bytes);

/** ...and where a pair's two mics meet, at the same points: synth::combOf. */
std::vector<float> combOf (const std::vector<float>& a, const std::vector<float>& b, double rate = 48000.0);

} // namespace cabir
