// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

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
    auto* read = formats.createReaderFor (file);
    if (read == nullptr)
        return false;
    const double rate = read->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (read, true);
    source->setLooping (true);
    transport.setSource (source.get(), 0, nullptr, rate);
    reader = std::move (source);
    name = file.getFileNameWithoutExtension();
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
        float* io[] { mono };
        if (! convolver.process (io, 1, numSamples))
            juce::FloatVectorOperations::clear (mono, numSamples);
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputs[ch], mono, numSamples);
}

} // namespace cabir::app
