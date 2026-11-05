// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

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
class Engine final : public juce::AudioIODeviceCallback
{
public:
    Engine();
    ~Engine() override;

    juce::AudioDeviceManager devices;

    /** A sample from a file: false for one that does not decode. Played from its start once play() is asked. */
    bool loadSample (const juce::File&);
    juce::String sampleName() const { return name; }
    bool hasSample() const { return reader != nullptr; }

    void play (bool on);
    bool isPlaying() const { return transport.isPlaying(); }

    /** The cabinet: its IR, mono, at `rate`. Message thread. */
    void setIr (std::vector<float> ir, double rate);

    /** A load the convolver turned away mid-fade, offered again. Message thread, often. */
    void flush();

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
    std::vector<float> irNow;
    double irRate = 48000.0;
    juce::AudioBuffer<float> scratch;

    void loadIrIntoConvolver();
};

} // namespace cabir::app
