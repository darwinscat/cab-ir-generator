// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PLAYER'S INPUT, without a device of its own (app/Engine.h): the input chosen is what plays, IN 4 of four as of two
// open; a change is crossfaded over 20 ms, no step in the sound; a change made while nothing plays is through by the
// time it plays again;
// every open input is measured for its meter, a closed one reads nought.

#include "Engine.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double rate = 48000.0;
constexpr int block = 256;

/** A device that is never opened: its rate, its block and the inputs it hands over. */
class FakeDevice final : public juce::AudioIODevice
{
public:
    explicit FakeDevice (juce::BigInteger inputsOpen) : juce::AudioIODevice ("fake", "fake"), ins (std::move (inputsOpen)) {}

    juce::StringArray getOutputChannelNames() override { return { "1", "2" }; }
    juce::StringArray getInputChannelNames() override { return { "1", "2", "3", "4" }; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { block }; }
    int getDefaultBufferSize() override { return block; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return ins; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    juce::BigInteger ins;
};

/** Blocks through the engine, each input a level of its own (input k at `levelOf[k]`), the first output kept. */
struct Rig
{
    cabir::app::Engine engine;
    std::vector<float> levelOf;          // by the inputs the device hands over, in order
    std::vector<float> heard;            // the first output, every sample run

    Rig (juce::BigInteger open, std::vector<float> levels) : levelOf (std::move (levels))
    {
        FakeDevice device (std::move (open));
        engine.audioDeviceAboutToStart (&device);
        engine.inputOnly();              // the input the source, played
        engine.setBypass (true);         // the cabinet out: what is heard is the input itself, once BYPASS has faded in
    }

    void run (double ms)
    {
        std::vector<std::vector<float>> in (levelOf.size(), std::vector<float> (block));
        std::vector<const float*> inputs;
        for (size_t k = 0; k < levelOf.size(); ++k)
        {
            std::fill (in[k].begin(), in[k].end(), levelOf[k]);
            inputs.push_back (in[k].data());
        }
        std::vector<float> left (block), right (block);
        float* outputs[] { left.data(), right.data() };
        for (int n = 0; n < (int) std::ceil (ms * rate / 1000.0 / block); ++n)
        {
            engine.audioDeviceIOCallbackWithContext (inputs.data(), (int) inputs.size(), outputs, 2, block, {});
            heard.insert (heard.end(), left.begin(), left.end());
        }
    }

    float now() const { return heard.empty() ? -1.0f : heard.back(); }

    float biggestStep (size_t from) const
    {
        float most = 0.0f;
        for (size_t i = from + 1; i < heard.size(); ++i)
            most = std::max (most, std::abs (heard[i] - heard[i - 1]));
        return most;
    }
};

bool near (float a, float b) { return std::abs (a - b) < 1.0e-4f; }
}

int main()
{
    // Four inputs open, IN 1 heard first.
    {
        juce::BigInteger four;
        four.setRange (0, 4, true);
        Rig rig (four, { 0.1f, 0.2f, 0.3f, 0.4f });
        rig.run (60.0);
        check (near (rig.now(), 0.1f), "IN 1 is what plays at first");

        // IN 4 chosen: there within 20 ms, no step bigger than the crossfade's.
        rig.engine.chooseInput (3);
        const auto from = rig.heard.size();
        rig.run (40.0);
        check (near (rig.now(), 0.4f), "IN 4 plays once chosen");
        check (rig.biggestStep (from - 1) <= 0.3f / (float) (0.02 * rate) * 1.01f, "the change from IN 1 to IN 4 has no step");
        int fading = 0;
        for (size_t i = from; i < rig.heard.size(); ++i)
            fading += rig.heard[i] > 0.1001f && rig.heard[i] < 0.3999f;
        check (std::abs (fading - (int) (0.02 * rate)) <= 2, "the crossfade takes 20 ms");

        // Each open input measured for its meter, read once; a closed one reads nought.
        check (near (rig.engine.takeLevel (2), 0.3f), "IN 3's meter reads its level");
        check (near (rig.engine.takeLevel (2), 0.0f), "a meter read is cleared until the next block");
        check (near (rig.engine.takeLevel (9), 0.0f), "an input the device has not reads nought");

        // Stopped, IN 2 chosen, played again past the crossfade's 20 ms: IN 2 at once, nothing of IN 4.
        rig.engine.play (false);
        rig.run (10.0);
        check (near (rig.now(), 0.0f), "nothing plays while stopped");
        rig.engine.chooseInput (1);
        rig.run (30.0);
        check (near (rig.now(), 0.0f), "...nor while the change crossfades unheard");
        rig.engine.play (true);
        const auto again = rig.heard.size();
        rig.run (10.0);
        check (near (rig.heard[again], 0.2f), "a change made while stopped is through by the time it plays again");
    }

    // Two of four open, IN 2 and IN 4: the device hands over those alone, in order.
    {
        juce::BigInteger two;
        two.setBit (1);
        two.setBit (3);
        Rig rig (two, { 0.2f, 0.4f });
        rig.engine.chooseInput (3);
        rig.run (60.0);
        check (near (rig.now(), 0.4f), "IN 4 plays when it arrives second");
        check (near (rig.engine.takeLevel (1), 0.2f) && near (rig.engine.takeLevel (3), 0.4f), "the open inputs measured by their own number");
        rig.engine.chooseInput (0);
        rig.run (40.0);
        check (near (rig.now(), 0.0f), "an input not open plays silence");
    }

    if (failures == 0)
        std::printf ("engine: all passed\n");
    return failures == 0 ? 0 : 1;
}
