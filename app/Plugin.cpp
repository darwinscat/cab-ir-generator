// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "Plugin.h"
#include "Flavour.h"
#include "MainComponent.h"

#include <BinaryData.h>

#if CABIR_GUITAR
 #include <panel/GuitarCatalog.h>
#endif

namespace cabir::app
{

namespace
{
    /** THE WINDOW IN A HOST'S: the generator laid out at its one size and drawn as large as the host's window lets it,
        its shape kept as a corner is dragged, as the app's window does (Main.cpp). */
    class Editor final : public juce::AudioProcessorEditor
    {
    public:
        explicit Editor (Plugin& p) : juce::AudioProcessorEditor (p), plugin (p), main (&p)
        {
            addAndMakeVisible (main);
            const int w = main.getWidth(), h = main.getHeight();
            setResizable (true, true);
            setResizeLimits (w * 6 / 10, h * 6 / 10, w * 3, h * 3);
            getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
            const int kept = p.editorWidth;
            if (kept >= w * 6 / 10 && kept <= w * 3)
                setSize (kept, juce::roundToInt ((double) kept * h / w));
            else
                setSize (w, h);
        }

        void paint (juce::Graphics& g) override { g.fillAll (look().panel); }

        void resized() override
        {
            const float s = juce::jmin ((float) getWidth() / (float) main.getWidth(), (float) getHeight() / (float) main.getHeight());
            const float x = 0.5f * ((float) getWidth() - (float) main.getWidth() * s), y = 0.5f * ((float) getHeight() - (float) main.getHeight() * s);
            main.setTransform (juce::AffineTransform::scale (s).translated (x, y));
            plugin.editorWidth = getWidth();
        }

    private:
        Plugin& plugin;
        MainComponent main;
    };

    const synth::Table& table()
    {
        static const auto t = synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
        return t;
    }
}

Plugin::Plugin()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
#if CABIR_GUITAR
    Panel::useCatalog (guitarCatalog());   // before the first window: the guitar's boxes and mics
#endif
    // A fresh instance plays the instrument's default until a project or the window says otherwise.
    playing = toToml (flavour::defaultState());
    playState (playing);
    startTimerHz (10);
}

Plugin::~Plugin()
{
    stopTimer();
    alive.reset();
    worker.removeAllJobs (true, 4000);
}

const juce::String Plugin::getName() const
{
    return flavour::product;
}

juce::String Plugin::format() const
{
    // The CLAP wrapper is not one JUCE names; it leaves the type undefined.
    return wrapperType == wrapperType_Undefined ? juce::String ("CLAP") : juce::String (getWrapperTypeDescription (wrapperType));
}

bool Plugin::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo out, its input the same or mono: a mono amp into a stereo track is the cabinet on both sides.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out || in == juce::AudioChannelSet::mono();
}

void Plugin::prepareToPlay (double rate, int maxBlock)
{
    std::lock_guard<std::mutex> lock (loading);
    prepared = false;
    rateNow = rate;
    width = juce::jlimit (1, 2, getMainBusNumInputChannels());
    dry.setSize (width, juce::jmax (1, maxBlock));
    wet = bypass.load() ? 0.0f : 1.0f;
    const bool ok = convolver.prepare (rate, maxBlock, width, 1.0, true);   // reference-unity: the app's loudness
    if (ok && ! irNow.empty())
    {
        const float* planes[] { irNow.data() };
        convolver.loadIR (planes, 1, (int) irNow.size(), irRate);
    }
    prepared = ok;
}

void Plugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ins = getTotalNumInputChannels(), outs = buffer.getNumChannels();
    for (int ch = ins; ch < outs; ++ch)
        buffer.clear (ch, 0, n);
    if (! prepared.load() || ins < width || n > dry.getNumSamples())
        return;   // not ready for this block: the signal passes as it came

    float* io[2] { buffer.getWritePointer (0), width > 1 ? buffer.getWritePointer (1) : nullptr };
    for (int ch = 0; ch < width; ++ch)
        dry.copyFrom (ch, 0, io[ch], n);
    if (! convolver.process (io, width, n))
        for (int ch = 0; ch < width; ++ch)
            juce::FloatVectorOperations::clear (io[ch], n);

    // BYPASS: the signal as it came and the cabinet crossfaded, the cabinet's share moving 20 ms end to end.
    const float target = bypass.load() ? 0.0f : 1.0f, step = (float) (1.0 / (0.02 * rateNow));
    for (int i = 0; i < n; ++i)
    {
        wet = target > wet ? std::min (target, wet + step) : std::max (target, wet - step);
        for (int ch = 0; ch < width; ++ch)
            io[ch][i] = wet * io[ch][i] + (1.0f - wet) * dry.getSample (ch, i);
    }

    // A mono cabinet on a stereo track: the same on both sides.
    for (int ch = width; ch < outs; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
}

void Plugin::setIr (std::vector<float> ir, double rate)
{
    ++ticket;   // the window's: a render of the session's own asked before it is not played
    load (std::move (ir), rate);
}

void Plugin::load (std::vector<float> ir, double rate)
{
    std::lock_guard<std::mutex> lock (loading);
    irNow = std::move (ir);
    irRate = rate;
    if (prepared.load() && ! irNow.empty())
    {
        const float* planes[] { irNow.data() };
        convolver.loadIR (planes, 1, (int) irNow.size(), irRate);
    }
}

void Plugin::flush()
{
    std::lock_guard<std::mutex> lock (loading);
    if (prepared.load() && convolver.hasPending())
        convolver.flushPending();
}

void Plugin::playState (const std::string& toml)
{
    const int mine = ++ticket;
    std::weak_ptr<int> stillHere = alive;
    worker.addJob ([this, stillHere, mine, toml]
    {
        if (mine != ticket.load())
            return;   // superseded before it began
        const auto s = knownState (toml);
        if (! s)
            return;
        auto ir = renderer.render (table(), s->box, synth::pairOf (*s), (size_t) 1 << 14);
        juce::MessageManager::callAsync ([this, stillHere, mine, mix = std::move (ir.mix)]() mutable
        {
            if (stillHere.lock() != nullptr && mine == ticket.load())
                load (std::move (mix), 48000.0);
        });
    });
}

juce::String Plugin::workspace() const
{
    std::lock_guard<std::mutex> lock (keeping);
    return kept;
}

void Plugin::keepWorkspace (const juce::String& xml, const std::string& state)
{
    std::lock_guard<std::mutex> lock (keeping);
    kept = xml;
    playing = state;
}

void Plugin::getStateInformation (juce::MemoryBlock& out)
{
    juce::ValueTree t ("OrbitCabBuilder");
    {
        std::lock_guard<std::mutex> lock (keeping);
        t.setProperty ("workspace", kept, nullptr);
        t.setProperty ("state", juce::String (playing), nullptr);
    }
    t.setProperty ("width", editorWidth, nullptr);
    if (const auto xml = t.createXml())
        copyXmlToBinary (*xml, out);
}

void Plugin::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    const auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("OrbitCabBuilder"))
        return;
    const auto state = t.getProperty ("state").toString().toStdString();
    {
        std::lock_guard<std::mutex> lock (keeping);
        kept = t.getProperty ("workspace").toString();
        playing = state;
    }
    editorWidth = (int) t.getProperty ("width", 0);
    playState (state);
    sendChangeMessage();   // a window open shows the project's workspace
}

juce::AudioProcessorEditor* Plugin::createEditor()
{
    return new Editor (*this);
}

} // namespace cabir::app

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cabir::app::Plugin();
}
