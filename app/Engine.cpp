// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "Engine.h"

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

bool Engine::loadSample (const juce::File& file)
{
    return take (formats.createReaderFor (file), file.getFileNameWithoutExtension());
}

bool Engine::loadSample (const void* data, size_t size, const juce::String& title)
{
    return take (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, size, false)), title);
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
    return true;
}

void Engine::play (bool on)
{
    if (reader == nullptr)
        return;
    if (on)
        transport.start();
    else
        transport.stop();
}

void Engine::setIr (std::vector<float> ir, double rate)
{
    irNow = std::move (ir);
    irRate = rate;
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
    transport.prepareToPlay (block, rate);
    scratch.setSize (2, block);
    prepared = convolver.prepare (rate, block, 1, 1.0, true);   // reference-unity: the same loudness as a plugin's
    loadIrIntoConvolver();
}

void Engine::audioDeviceStopped()
{
    prepared = false;
    transport.releaseResources();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    if (numSamples > scratch.getNumSamples() || numOutputs == 0)
        return;

    // The sample, folded to mono, through the cabinet, to every output.
    juce::AudioSourceChannelInfo info (&scratch, 0, numSamples);
    scratch.clear (0, numSamples);
    transport.getNextAudioBlock (info);
    float* mono = scratch.getWritePointer (0);
    if (scratch.getNumChannels() > 1)
    {
        juce::FloatVectorOperations::add (mono, scratch.getReadPointer (1), numSamples);
        juce::FloatVectorOperations::multiply (mono, 0.5f, numSamples);
    }
    if (prepared.load())
    {
        // The sample as it came kept beside it, for BYPASS: the two crossfaded, the cabinet's share moving 20 ms end to end.
        float* dry = scratch.getWritePointer (1);
        juce::FloatVectorOperations::copy (dry, mono, numSamples);
        float* io[] { mono };
        if (! convolver.process (io, 1, numSamples))
            juce::FloatVectorOperations::clear (mono, numSamples);
        const float target = bypass.load() ? 0.0f : 1.0f, step = (float) (1.0 / (0.02 * rateNow));
        for (int i = 0; i < numSamples; ++i)
        {
            wet = target > wet ? std::min (target, wet + step) : std::max (target, wet - step);
            mono[i] = wet * mono[i] + (1.0f - wet) * dry[i];
        }
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputs[ch], mono, numSamples);
}

} // namespace cabir::app
