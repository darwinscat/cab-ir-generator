// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "Sound.h"

#include <felitronics/convolution/CabConvolver.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <synth/CabSynth.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace cabir::app
{

/** THE PLUGIN: the track through the cabinet the window sets — the family's convolver, as the app plays its sample, mono
    or stereo, no latency. The window hands it each IR it renders; with no window open it renders the state its session
    keeps on a worker of its own (a project loaded, a fresh instance), so it always plays a cabinet. The project keeps the
    workspace, the state that plays and the window's width. */
class Plugin final : public juce::AudioProcessor,
                     public Session,
                     private juce::Timer
{
public:
    Plugin();
    ~Plugin() override;

    void prepareToPlay (double rate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.35; }   // the IR's 2^14 samples at 48 kHz

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    // The window's (Session).
    void setIr (std::vector<float> ir, double rate) override;
    void flush() override;
    void setBypass (bool on) override { bypass = on; }
    juce::String workspace() const override;
    void keepWorkspace (const juce::String& xml, const std::string& playing) override;
    juce::String format() const override;

    /** The window's width as it was last left, kept with the project; 0 for its own size. Message thread. */
    int editorWidth = 0;

private:
    felitronics::convolution::CabConvolver convolver;
    std::mutex loading;                 // prepare and load, both off the audio thread, never at once
    std::atomic<bool> prepared { false };
    std::atomic<bool> bypass { false };
    float wet = 1.0f;                   // the cabinet's share, audio thread: toward nought while bypassed
    double rateNow = 48000.0;
    int width = 1;                      // the channels convolved: the input's, one or two
    std::vector<float> irNow;           // the IR that plays, kept for a prepare to come (under `loading`)
    double irRate = 48000.0;
    juce::AudioBuffer<float> dry;       // the signal as it came, for BYPASS

    mutable std::mutex keeping;         // the session's text: a host may ask for it from any thread
    juce::String kept;                  // the workspace, XML
    std::string playing;                // the state that plays, TOML

    /** THE SESSION'S OWN RENDER: the state it keeps, rendered whole and played — unless an IR came from the window, or
        another render was asked, after it was. Each ask and each IR from the window takes the next ticket. */
    juce::ThreadPool worker { 1 };
    synth::PairRenderer renderer;
    std::atomic<int> ticket { 0 };
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    void playState (const std::string& toml);

    void load (std::vector<float> ir, double rate);   // the IR in, played once the convolver is prepared
    void timerCallback() override { flush(); }
};

} // namespace cabir::app
