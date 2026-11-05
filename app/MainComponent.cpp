// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "MainComponent.h"

#include <BinaryData.h>
#include <felitronics/appkit/AudioSettingsPanel.h>
#include <felitronics/appkit/BrandAssets.h>
#include <panel/Response.h>
#include <synth/CabSynth.h>

namespace cabir::app
{

namespace
{
    juce::PropertiesFile::Options storage()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Cab IR Generator";
        o.filenameSuffix      = ".settings";
        o.folderName          = "Darwin's Cat";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::Font brandFont (float height)
    {
        return felitronics::appkit::brand::wordmarkFont (felitronics::appkit::brand::wordmarkTypeface(), height);
    }

    /** What the version window says that the badge cannot know: who we are, what this build is and was built against. */
    felitronics::appkit::VersionBadge::Config about()
    {
        felitronics::appkit::VersionBadge::Config c;
        c.productName = "Cab IR Generator";
        c.productUrl  = "https://github.com/darwinscat/cab-ir-generator";
        c.gitHash     = CABIR_GIT_HASH;
        c.os          = juce::SystemStats::getOperatingSystemName();
        c.licence     = "AGPL-3.0+";
        const auto juceVersion = juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf (" ", false, false);
        c.dependencies = { { .label = "felitronics-core",   .version = CABIR_CORE_VERSION,   .ownerRepo = "darwinscat/felitronics-core" },
                           { .label = "felitronics-toml",   .version = CABIR_TOML_VERSION,   .ownerRepo = "darwinscat/felitronics-toml" },
                           { .label = "felitronics-appkit", .version = CABIR_APPKIT_VERSION, .ownerRepo = "darwinscat/felitronics-appkit" },
                           { .label = "JUCE",               .version = juceVersion,          .ownerRepo = "juce-framework/JUCE" } };
        // The product's mark, drawn from the logo's own SVG.
        std::shared_ptr<juce::Drawable> mark { juce::Drawable::createFromImageData (BinaryData::mark_svg, (size_t) BinaryData::mark_svgSize).release() };
        c.drawMark = [mark] (juce::Graphics& g, float cx, float cy, float d)
        {
            if (mark != nullptr)
                mark->drawWithin (g, juce::Rectangle<float> (d, d).withCentre ({ cx, cy }), juce::RectanglePlacement::centred, 1.0f);
        };
        return c;
    }
}

MainComponent::MainComponent()
    : checker ({ .ownerRepo = "darwinscat/cab-ir-generator", .productName = "Cab IR Generator", .currentVersion = CABIR_VERSION,
                 .settings = [this] { return properties.getUserSettings(); } }),
      badge (checker, about(), "Standalone")
{
    properties.setStorageParameters (storage());

    // The panel in the family's look: its palette, Michroma, and the tracking the panel lays its words out by.
    look().font = brandFont;

    picker.onSetup    = [this] (int cab, CabPicker::Setup setup) { render (CabPicker::stateOf (cab, setup), false); };
    picker.onMove     = [this] (int cab, CabPicker::Setup setup) { render (CabPicker::stateOf (cab, setup), true); };
    picker.onDownload = [this] { download(); };
    addAndMakeVisible (picker);

    badge.setBrandTypeface (felitronics::appkit::brand::wordmarkTypeface());
    addChildComponent (badge);   // never shown: the strip draws the version, the badge holds the window it opens

    // The state the last run kept, else a 4x10 with its own mic at its cap.
    state.box = "SVT-410HE";
    if (const auto kept = fromToml (properties.getUserSettings()->getValue ("state").toStdString()))
        state = *kept;
    const auto [cab, setup] = CabPicker::setupOf (state);
    picker.show (cab, setup);
    render (state, false);

    // The output the last run used, else the system's.
    const auto audio = juce::parseXML (properties.getUserSettings()->getValue ("audio"));
    engine.devices.initialise (0, 2, audio.get(), true);
    engine.devices.addAudioCallback (&engine);

    if (const auto last = juce::File (properties.getUserSettings()->getValue ("sample")); last.existsAsFile())
        loadSample (last);

    setSize (CabPicker::panelW, CabPicker::panelH + stripH);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    alive.reset();
    worker.removeAllJobs (true, 2000);
    if (const auto xml = engine.devices.createStateXml())
        properties.getUserSettings()->setValue ("audio", xml->toString());
    properties.saveIfNeeded();
}

void MainComponent::render (const State& s, bool live)
{
    const int mine = ++latest;
    std::weak_ptr<int> stillHere = alive;
    worker.addJob ([this, stillHere, mine, s, live]
    {
        if (mine != latest.load())
            return;   // superseded before it began
        static const auto table = synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
        auto ir = synth::renderPairParts (table, s.box, synth::pairOf (s));
        auto wav = synth::wav24 (ir.mix, 48000);

        juce::MessageManager::callAsync ([this, stillHere, mine, s, live, ir, wav]() mutable
        {
            if (stillHere.lock() == nullptr)
                return;
            // A moving hand: whatever finished is heard unless a later one was; a kept one only when it is the last asked.
            if (live ? mine <= heard : mine != latest.load())
                return;
            heard = mine;

            juce::MemoryBlock bytes (wav.data(), wav.size());
            auto now = responseOf (bytes);
            auto comb = ir.a.empty() ? std::vector<float>() : combOf (ir.a, ir.b);
            auto before = keptCurve;
            engine.setIr (std::move (ir.mix), 48000.0);
            if (! live)
            {
                state = s;
                keptWav = std::move (bytes);
                keptCurve = now;
                properties.getUserSettings()->setValue ("state", juce::String (toToml (s)));
            }
            picker.showResponse (std::move (now), std::move (before), std::move (comb));
        });
    });
}

void MainComponent::loadSample (const juce::File& file)
{
    const bool wasPlaying = engine.isPlaying();
    if (! engine.loadSample (file))
        return;
    properties.getUserSettings()->setValue ("sample", file.getFullPathName());
    engine.play (wasPlaying);
    repaint (strip());
}

void MainComponent::openSample()
{
    chooser = std::make_unique<juce::FileChooser> ("A bass sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) loadSample (c.getResult()); });
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadSample (juce::File (files[0]));
}

void MainComponent::showAudio()
{
    felitronics::appkit::AudioSettingsPanel::Options o;
    o.minInputs = 0;
    o.maxInputs = 0;
    auto panel = std::make_unique<felitronics::appkit::AudioSettingsPanel> (engine.devices, o);
    panel->setSize (460, 260);
    juce::CallOutBox::launchAsynchronously (std::move (panel), audioArea().translated (0, 0), this);
}

void MainComponent::download()
{
    if (keptWav.isEmpty())
        return;
    const auto name = juce::File::createLegalFileName (CabPicker::labelOf (state).replace (juce::String::fromUTF8 (" \xc2\xb7 "), " "));
    auto folder = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Download IR", folder.getChildFile (name + ".wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, bytes = keptWav] (const juce::FileChooser& c)
                          {
                              if (const auto file = c.getResult(); file != juce::File())
                                  file.withFileExtension ("wav").replaceWithData (bytes.getData(), bytes.getSize());
                          });
}

void MainComponent::timerCallback()
{
    engine.flush();
}

// ---------------------------------------------------------------- the strip
juce::Rectangle<int> MainComponent::openArea() const    { return strip().reduced (8, 5).withWidth (70); }
juce::Rectangle<int> MainComponent::playArea() const    { return openArea().translated (78, 0).withWidth (70); }
juce::Rectangle<int> MainComponent::audioArea() const   { return versionArea().translated (-82, 0).withWidth (74); }
juce::Rectangle<int> MainComponent::versionArea() const { return strip().reduced (8, 5).removeFromRight (70); }

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (look().panel);
    const auto s = strip();
    g.setColour (look().hair);
    g.fillRect (s.withHeight (1));

    const auto font = look().font (13.0f);
    const auto button = [&] (const juce::String& word, juce::Rectangle<int> r, bool lit)
    {
        const auto b = r.toFloat().reduced (0.5f);
        g.setColour (look().panel2);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (lit ? look().orange : look().txFaint);
        g.drawRoundedRectangle (b, 4.0f, lit ? 1.2f : 1.0f);
        g.setColour (lit ? look().orange : look().txDim);
        look().drawTracked (g, word, b, font, 0.06f, juce::Justification::centred);
    };
    button ("OPEN", openArea(), false);
    if (engine.hasSample())
        button (engine.isPlaying() ? "STOP" : "PLAY", playArea(), engine.isPlaying());
    button ("AUDIO", audioArea(), false);

    const auto nameArea = juce::Rectangle<int> (playArea().getRight() + 12, s.getY(), audioArea().getX() - playArea().getRight() - 24, s.getHeight());
    g.setColour (engine.hasSample() ? look().tx : look().txFaint);
    look().drawTracked (g, engine.hasSample() ? engine.sampleName().toUpperCase() : juce::String ("DROP A BASS SAMPLE ON THE WINDOW"),
                        nameArea.toFloat(), font, 0.06f, juce::Justification::centredLeft);

    g.setColour (checker.updateAvailable() ? look().orange : look().txDim);
    look().drawTracked (g, "V" + checker.currentVersion(), versionArea().toFloat(), font, 0.06f, juce::Justification::centredRight);
}

void MainComponent::resized()
{
    picker.setBounds (0, 0, CabPicker::panelW, CabPicker::panelH);
    badge.setBounds (versionArea());
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (openArea().contains (p))
        openSample();
    else if (playArea().contains (p) && engine.hasSample())
    {
        engine.play (! engine.isPlaying());
        repaint (strip());
    }
    else if (audioArea().contains (p))
        showAudio();
    else if (versionArea().contains (p))
        badge.showAbout();
}

} // namespace cabir::app
