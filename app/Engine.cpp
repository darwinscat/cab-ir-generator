// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "Engine.h"

#include <felitronics/appkit/InputList.h>   // openInputBits

namespace cabir::app
{

Engine::Engine()
{
    formats.registerBasicFormats();
}

Engine::~Engine()
{
    devices.removeAudioCallback (this);
    transport.setSource (nullptr);
}

bool Engine::loadSample (const juce::File& file, const juce::String& title)
{
    return take (formats.createReaderFor (file), title.isNotEmpty() ? title : file.getFileNameWithoutExtension());
}

bool Engine::take (juce::AudioFormatReader* read, const juce::String& title)
{
    if (read == nullptr)
        return false;
    const double rate = read->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (read, true);
    source->setLooping (true);
    transport.setSource (source.get(), 0, nullptr, rate);
    reader = std::move (source);
    name = title;
    input = false;
    live = false;
    listen (false);
    return true;
}

void Engine::useInput (bool play)
{
    const bool playing = isPlaying();
    transport.stop();
    live = play && playing;
    input = true;
    listen (true);
}

void Engine::inputOnly()
{
    transport.stop();
    input = true;
    live = true;
}

void Engine::listen (bool on)
{
    auto setup = devices.getAudioDeviceSetup();
    const bool open = setup.inputDeviceName.isNotEmpty() && ! setup.inputChannels.isZero();
    if (open == on)
        return;
    if (on)
    {
        // The one open last time, else the system's default input; every input it has (the device keeps those it has).
        setup.inputDeviceName = keptInput;
        if (auto* type = devices.getCurrentDeviceTypeObject(); type != nullptr
            && (setup.inputDeviceName.isEmpty() || ! type->getDeviceNames (true).contains (setup.inputDeviceName)))
        {
            const auto names = type->getDeviceNames (true);
            const int at = type->getDefaultDeviceIndex (true);
            setup.inputDeviceName = at >= 0 && at < names.size() ? names[at] : juce::String();
        }
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, maxInputs, true);
    }
    else
    {
        keptInput = setup.inputDeviceName;
        setup.inputDeviceName = {};
        setup.inputChannels.clear();
    }
    setup.useDefaultInputChannels = false;   // named: JUCE keeps the count asked for through a change of device
    devices.setAudioDeviceSetup (setup, true);
}

juce::String Engine::inputName() const
{
    const auto setup = devices.getAudioDeviceSetup();
    return setup.inputChannels.isZero() ? juce::String() : setup.inputDeviceName;
}

void Engine::openEveryInput()
{
    auto setup = devices.getAudioDeviceSetup();
    auto* device = devices.getCurrentAudioDevice();
    if (device == nullptr || setup.inputDeviceName.isEmpty())
        return;
    const int has = std::min (maxInputs, device->getInputChannelNames().size());
    const auto asked = setup.inputDeviceName + " " + juce::String (has);
    if (setup.inputChannels.countNumberOfSetBits() >= has || asked == askedEvery)
        return;
    askedEvery = asked;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, maxInputs, true);   // the device keeps those it has
    setup.useDefaultInputChannels = false;
    devices.setAudioDeviceSetup (setup, true);
}

std::uint64_t Engine::openInputs() const
{
    auto* device = devices.getCurrentAudioDevice();
    return device != nullptr && inputName().isNotEmpty() ? felitronics::appkit::openInputBits (device->getActiveInputChannels()) : 0;
}

int Engine::inputCount() const
{
    auto* device = devices.getCurrentAudioDevice();
    return device != nullptr && inputName().isNotEmpty() ? std::min (maxInputs, device->getInputChannelNames().size()) : 0;
}

void Engine::play (bool on)
{
    if (input.load())
    {
        live = on;
        return;
    }
    if (reader == nullptr)
        return;
    if (on)
        transport.start();
    else
        transport.stop();
}

void Engine::setIr (std::vector<float> ir, double rate, float levelOf)
{
    irNow = std::move (ir);
    irRate = rate;
    levelWanted = levelOf;
    loadIrIntoConvolver();
}

void Engine::loadIrIntoConvolver()
{
    if (! prepared.load() || irNow.empty())
        return;
    const float* planes[] { irNow.data() };
    convolver.loadIR (planes, 1, (int) irNow.size(), irRate);
}

void Engine::flush()
{
    if (prepared.load() && convolver.hasPending())
        convolver.flushPending();
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();
    rateNow = rate;
    // Which inputs the device opened: it hands over those alone, in order.
    pick.prepare (rate, felitronics::appkit::openInputBits (device->getActiveInputChannels()));
    transport.prepareToPlay (block, rate);
    scratch.setSize (2, block);
    prepared = convolver.prepare (rate, block, 1, 1.0, true);   // reference-unity: the same loudness as a plugin's
    spectrumFeed.prepare (rate);
    spectrumPreFeed.prepare (rate);
    loadIrIntoConvolver();
}

void Engine::audioDeviceStopped()
{
    prepared = false;
    transport.releaseResources();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // The input chosen while it is played, a change crossfaded over 20 ms, and every input measured for its meter, played
    // or not (InputPick, into the scratch: read before any output is written, the two may share memory) — or the sample,
    // folded to mono; through the cabinet, to every output.
    const bool fits = numSamples <= scratch.getNumSamples() && numOutputs > 0;
    float* mono = fits ? scratch.getWritePointer (0) : nullptr;
    if (fits)
    {
        scratch.clear (0, numSamples);
        if (input.load())
        {
            float* picked[] { mono };
            pick.process (inputs, numInputs, picked, 1, numSamples);
            if (! live.load())
                juce::FloatVectorOperations::clear (mono, numSamples);   // measured, not heard
        }
        else
        {
            juce::AudioSourceChannelInfo info (&scratch, 0, numSamples);
            transport.getNextAudioBlock (info);
            if (scratch.getNumChannels() > 1)
            {
                juce::FloatVectorOperations::add (mono, scratch.getReadPointer (1), numSamples);
                juce::FloatVectorOperations::multiply (mono, 0.5f, numSamples);
            }
        }
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    if (! fits)
        return;

    if (prepared.load())
    {
        // The sample as it came kept beside it, for BYPASS: the two crossfaded, the cabinet's share moving 20 ms end to end.
        float* dry = scratch.getWritePointer (1);
        juce::FloatVectorOperations::copy (dry, mono, numSamples);
        spectrumPreFeed.feed (dry, nullptr, numSamples);   // what goes into the cabinet, for the spectrum's pre
        float* io[] { mono };
        if (! convolver.process (io, 1, numSamples))
            juce::FloatVectorOperations::clear (mono, numSamples);
        const float target = bypass.load() ? 0.0f : 1.0f, step = (float) (1.0 / (0.02 * rateNow)), wanted = levelWanted.load();
        for (int i = 0; i < numSamples; ++i)
        {
            wet = target > wet ? std::min (target, wet + step) : std::max (target, wet - step);
            level = wanted > level ? std::min (wanted, level + step) : std::max (wanted, level - step);
            mono[i] = wet * level * mono[i] + (1.0f - wet) * dry[i];
        }
        spectrumFeed.feed (mono, nullptr, numSamples);   // what is heard, for the EQ's spectrum
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputs[ch], mono, numSamples);
}

} // namespace cabir::app
