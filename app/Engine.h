// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include "Sound.h"

#include <felitronics/convolution/CabConvolver.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <vector>

namespace cabir::app
{

/** THE PLAYER: a sample of the player's own, looped, through the cabinet as it stands — the family's convolver, the one
    the plugins play their cabinets through (felitronics::convolution::CabConvolver), so what is heard here is what a
    plugin plays. A sample in, an IR in, the output device out. The IR is taken on the message thread and crossfaded in
    by the convolver; one that lands mid-fade waits for the next tick (flush). */
class Engine final : public juce::AudioIODeviceCallback,
                     public Sound
{
public:
    Engine();
    ~Engine() override;

    juce::AudioDeviceManager devices;

    /** A sample from a file: false for one that does not decode. Played from its start once play() is asked. */
    bool loadSample (const juce::File&);
    /** ...or from bytes the app carries (a riff of its own), read where they lie: they outlive the player. */
    bool loadSample (const void* data, size_t size, const juce::String& title);
    juce::String sampleName() const { return name; }
    bool hasSample() const { return reader != nullptr; }

    void play (bool on);
    bool isPlaying() const { return transport.isPlaying(); }

    void setIr (std::vector<float> ir, double rate) override;
    void flush() override;

    /** BYPASS: the sample heard as it is, the cabinet out — faded either way over 20 ms, the convolver running on under
        it so the cabinet comes back without a start. */
    void setBypass (bool on) override { bypass = on; }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource transport;
    juce::String name;

    felitronics::convolution::CabConvolver convolver;
    std::atomic<bool> prepared { false };
    std::atomic<bool> bypass { false };
    float wet = 1.0f;          // the cabinet's share, audio thread: toward nought while bypassed
    double rateNow = 48000.0;  // the device's
    std::vector<float> irNow;
    double irRate = 48000.0;
    juce::AudioBuffer<float> scratch;

    bool take (juce::AudioFormatReader*, const juce::String& title);   // the reader looped into the transport, named
    void loadIrIntoConvolver();
};

} // namespace cabir::app
