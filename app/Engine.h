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
    by the convolver; one that lands mid-fade waits for the next tick (flush).
    ...OR LIVE (useInput, the owner, 07.11): the audio interface's first open input through the cabinet, as an amp's
    speaker — heard only while played (PLAY), so a laptop's own mic beside its speakers never howls the moment it is
    picked. The input device is open only while it is the source (the system's mic light off otherwise); the one the
    AUDIO settings chose is kept for the next time. */
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
    bool hasSample() const { return input.load() || reader != nullptr; }

    /** The input instead of a sample, silent until played — or, `play`, heard at once if the sample was playing. */
    void useInput (bool play = false);
    /** THE INPUT ALONE, as a release plays it (no player): whatever input AUDIO has open, heard at once — none opened
        here, so a first run is silent until one is picked there. */
    void inputOnly();
    bool usingInput() const { return input.load(); }
    juce::String inputName() const;   // the input device open, empty for none

    void play (bool on);
    bool isPlaying() const { return input.load() ? live.load() : transport.isPlaying(); }

    void setIr (std::vector<float> ir, double rate, float level) override;
    void flush() override;

    /** BYPASS: the sample heard as it is, the cabinet out — faded either way over 20 ms, the convolver running on under
        it so the cabinet comes back without a start. */
    void setBypass (bool on) override { bypass = on; }
    SpectrumFeed& spectrum() override { return spectrumFeed; }
    SpectrumFeed& spectrumPre() override { return spectrumPreFeed; }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource transport;
    juce::String name;
    std::atomic<bool> input { false }, live { false };   // the input the source; ...heard
    juce::String keptInput;                               // the input device and channels last open, for the next time
    juce::BigInteger keptChannels;
    void listen (bool on);                                // the input device opened, or closed

    felitronics::convolution::CabConvolver convolver;
    std::atomic<bool> prepared { false };
    std::atomic<bool> bypass { false };
    float wet = 1.0f;          // the cabinet's share, audio thread: toward nought while bypassed
    std::atomic<float> levelWanted { 1.0f };   // the cabinet's level (setIr's)
    float level = 1.0f;        // ...as it plays, audio thread: moving there as BYPASS does
    double rateNow = 48000.0;  // the device's
    std::vector<float> irNow;
    double irRate = 48000.0;
    juce::AudioBuffer<float> scratch;
    SpectrumFeed spectrumFeed;   // what plays, out of the cabinet, for the EQ's spectrum
    SpectrumFeed spectrumPreFeed;   // ...and what goes in

    bool take (juce::AudioFormatReader*, const juce::String& title);   // the reader looped into the transport, named
    void loadIrIntoConvolver();
};

} // namespace cabir::app
