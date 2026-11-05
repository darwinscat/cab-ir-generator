// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "MainComponent.h"

#include <BinaryData.h>
#include <felitronics/appkit/AudioSettingsPanel.h>
#include <felitronics/appkit/BrandAssets.h>
#include <felitronics/toml/Toml.h>
#include <panel/Response.h>
#include <synth/CabSynth.h>

namespace cabir::app
{

namespace
{
    std::unique_ptr<juce::Drawable> markOf() { return juce::Drawable::createFromImageData (BinaryData::mark_svg, (size_t) BinaryData::mark_svgSize); }

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
      badge (checker, about(), "Standalone"),
      history (felitronics::appkit::CompareHistory::Mode::PerRegister,
               [this]
               {
                   juce::ValueTree t ("Cabinet");
                   t.setProperty ("state", juce::String (toToml (state)), nullptr);
                   return t;
               },
               [this] (const juce::ValueTree& t)
               {
                   if (const auto s = fromToml (t.getProperty ("state").toString().toStdString()))
                       show (*s);
               },
               felitronics::appkit::CompareHistory::Config {})
{
    properties.setStorageParameters (storage());
    mark = markOf();

    // The panel in the family's look: its palette, Michroma, and the tracking the panel lays its words out by.
    look().font = brandFont;

    picker.onSetup    = [this] (int cab, CabPicker::Setup setup) { keep (CabPicker::stateOf (cab, setup)); };
    picker.onMove     = [this] (int cab, CabPicker::Setup setup) { render (CabPicker::stateOf (cab, setup), true); };
    picker.onDownload = [this] { download(); };
    addAndMakeVisible (picker);

    badge.setBrandTypeface (felitronics::appkit::brand::wordmarkTypeface());
    addChildComponent (badge);   // never shown: the strip draws the version, the badge holds the window it opens

    // The workspace the last run left — A/B/C/D and the one that plays — else a 4x10 with its own mic at its cap.
    history.onHistoryChanged = [this] { repaint (header()); };
    const auto workspace = juce::parseXML (properties.getUserSettings()->getValue ("workspace"));
    if (workspace == nullptr || ! history.fromTree (juce::ValueTree::fromXml (*workspace)))
    {
        State first;
        first.box = "SVT-410HE";
        first.back = 1;   // ported, as eight boxes of the ten are (the owner, 05.11)
        show (first);
        history.reset();
    }

    // The output the last run used, else the system's.
    const auto audio = juce::parseXML (properties.getUserSettings()->getValue ("audio"));
    engine.devices.initialise (0, 2, audio.get(), true);
    engine.devices.addAudioCallback (&engine);

    if (const auto last = juce::File (properties.getUserSettings()->getValue ("sample")); last.existsAsFile())
        loadSample (last);

    setSize (CabPicker::panelW, headerH + CabPicker::panelH + stripH);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    alive.reset();
    worker.removeAllJobs (true, 2000);
    if (const auto xml = engine.devices.createStateXml())
        properties.getUserSettings()->setValue ("audio", xml->toString());
    properties.getUserSettings()->setValue ("workspace", history.toTree().toXmlString());
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
                keptWav = std::move (bytes);
                keptCurve = now;
            }
            picker.showResponse (std::move (now), std::move (before), std::move (comb));
        });
    });
}

void MainComponent::keep (const State& s)
{
    state = s;
    render (s, false);
    repaint (header());
}

void MainComponent::show (const State& s)
{
    state = s;
    const auto [cab, setup] = CabPicker::setupOf (s);
    picker.show (cab, setup);
    render (s, false);
    repaint (header());
}

// ---------------------------------------------------------------- presets
juce::File MainComponent::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Darwin's Cat").getChildFile ("Cab IR Generator").getChildFile ("Presets");
}

namespace
{
    /** A preset as a file holds it: its name and its style over the state's own TOML. */
    struct Preset { juce::String style, name, toml; };

    Preset presetOf (const juce::String& text, const juce::String& fallback)
    {
        namespace t = felitronics::toml;
        Preset p { {}, fallback, text };
        auto parsed = t::parse (text.toStdString());
        if (const auto* root = std::get_if<t::Table> (&parsed))
        {
            if (const auto* v = root->find ("name"); v != nullptr)
                if (const auto* n = std::get_if<std::string> (&v->data))
                    p.name = juce::String (*n);
            if (const auto* v = root->find ("style"); v != nullptr)
                if (const auto* n = std::get_if<std::string> (&v->data))
                    p.style = juce::String (*n);
        }
        return p;
    }

    /** The factory's, built in (every .toml of data/presets), by style then name. */
    std::vector<Preset> factoryPresets()
    {
        std::vector<Preset> out;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::originalFilenames[i]).endsWithIgnoreCase (".toml"))
            {
                int size = 0;
                const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                out.push_back (presetOf (juce::String::fromUTF8 (data, size), BinaryData::originalFilenames[i]));
            }
        std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b)
                   { return a.style != b.style ? a.style < b.style : a.name < b.name; });
        return out;
    }
}

std::vector<std::pair<juce::String, juce::String>> MainComponent::allPresets()
{
    std::vector<std::pair<juce::String, juce::String>> out;   // toml, name: the factory's by style, then the player's
    for (const auto& p : factoryPresets())
        out.push_back ({ p.toml, p.name });
    for (const auto& f : presetFolder().findChildFiles (juce::File::findFiles, false, "*.toml"))
    {
        const auto p = presetOf (f.loadFileAsString(), f.getFileNameWithoutExtension());
        out.push_back ({ p.toml, p.name });
    }
    return out;
}

void MainComponent::stepPreset (int by)
{
    const auto all = allPresets();
    if (all.empty())
        return;
    int at = -1;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].second == presetName)
            at = (int) i;
    const int n = (int) all.size();
    const int next = at < 0 ? (by > 0 ? 0 : n - 1) : ((at + by) % n + n) % n;
    loadPreset (all[(size_t) next].first, all[(size_t) next].second);
}

void MainComponent::loadPreset (const juce::String& toml, const juce::String& name)
{
    const auto s = fromToml (toml.toStdString());
    if (! s)
        return;
    show (*s);
    presetName = name;
    presetText = toToml (*s);
    repaint (header());
}

void MainComponent::showPresets()
{
    juce::PopupMenu menu;
    const auto factory = factoryPresets();
    juce::String style;
    juce::PopupMenu group;
    std::vector<std::pair<juce::String, juce::String>> picks;   // toml, name — by item id - 1
    const auto flush = [&]
    {
        if (group.getNumItems() > 0)
            menu.addSubMenu (style.toUpperCase(), group);
        group = {};
    };
    for (const auto& p : factory)
    {
        if (p.style != style)
        {
            flush();
            style = p.style;
        }
        picks.push_back ({ p.toml, p.name });
        group.addItem ((int) picks.size(), p.name);
    }
    flush();

    const auto mine = presetFolder().findChildFiles (juce::File::findFiles, false, "*.toml");
    if (! mine.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("MY PRESETS");
        for (const auto& f : mine)
        {
            const auto p = presetOf (f.loadFileAsString(), f.getFileNameWithoutExtension());
            picks.push_back ({ p.toml, p.name });
            menu.addItem ((int) picks.size(), p.name);
        }
    }
    menu.addSeparator();
    constexpr int saveId = 100000, folderId = 100001;
    menu.addItem (saveId, "SAVE AS...");
    menu.addItem (folderId, "SHOW MY PRESETS");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (presetArea())),
                        [this, picks] (int id)
                        {
                            if (id == saveId)
                                savePreset();
                            else if (id == folderId)
                            {
                                presetFolder().createDirectory();
                                presetFolder().revealToUser();
                            }
                            else if (id > 0 && id <= (int) picks.size())
                                loadPreset (picks[(size_t) id - 1].first, picks[(size_t) id - 1].second);
                        });
}

void MainComponent::savePreset()
{
    auto* window = new juce::AlertWindow ("Save preset", "A name for it", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", presetName.isNotEmpty() ? presetName : CabPicker::labelOf (state), {});
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;
        namespace t = felitronics::toml;
        t::Table head;
        (void) head.insert ("name", t::Value (name.toStdString()));
        const auto text = juce::String (t::write (head) + toToml (state));
        presetFolder().createDirectory();
        if (presetFolder().getChildFile (juce::File::createLegalFileName (name) + ".toml").replaceWithText (text))
        {
            presetName = name;
            presetText = toToml (state);
            repaint (header());
        }
    }), true);
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
                          [bytes = keptWav] (const juce::FileChooser& c)
                          {
                              if (const auto file = c.getResult(); file != juce::File())
                                  file.withFileExtension ("wav").replaceWithData (bytes.getData(), bytes.getSize());
                          });
}

void MainComponent::timerCallback()
{
    history.tick();
    engine.flush();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('z', command, 0))
        return history.undo(), true;
    if (key == juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress ('y', command, 0))
        return history.redo(), true;
    return false;
}

// ---------------------------------------------------------------- the header
juce::Rectangle<int> MainComponent::undoArea() const   { return header().reduced (8, 6).withX (220).withWidth (64); }
juce::Rectangle<int> MainComponent::redoArea() const   { return undoArea().translated (70, 0); }
juce::Rectangle<int> MainComponent::prevArea() const   { return { redoArea().getRight() + 18, undoArea().getY(), 28, undoArea().getHeight() }; }
juce::Rectangle<int> MainComponent::presetArea() const { return { prevArea().getRight() + 4, undoArea().getY(), 300, undoArea().getHeight() }; }
juce::Rectangle<int> MainComponent::nextArea() const   { return { presetArea().getRight() + 4, undoArea().getY(), 28, undoArea().getHeight() }; }
juce::Rectangle<int> MainComponent::registerArea (int i) const
{
    const auto h = header().reduced (8, 6);
    return { h.getRight() - (4 - i) * 34 + 4, h.getY(), 30, h.getHeight() };
}

// ---------------------------------------------------------------- the strip
juce::Rectangle<int> MainComponent::openArea() const    { return strip().reduced (8, 5).withWidth (70); }
juce::Rectangle<int> MainComponent::playArea() const    { return openArea().translated (78, 0).withWidth (70); }
juce::Rectangle<int> MainComponent::audioArea() const   { return versionArea().translated (-82, 0).withWidth (74); }
juce::Rectangle<int> MainComponent::versionArea() const { return strip().reduced (8, 5).removeFromRight (70); }

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (look().panel);
    const auto font = look().font (13.0f);
    const auto button = [&] (const juce::String& word, juce::Rectangle<int> r, bool lit, bool enabled = true)
    {
        const auto b = r.toFloat().reduced (0.5f);
        g.setColour (look().panel2);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour ((lit ? look().orange : look().txFaint).withMultipliedAlpha (enabled ? 1.0f : 0.4f));
        g.drawRoundedRectangle (b, 4.0f, lit ? 1.2f : 1.0f);
        g.setColour ((lit ? look().orange : look().txDim).withMultipliedAlpha (enabled ? 1.0f : 0.4f));
        look().drawTracked (g, word, b, font, 0.06f, juce::Justification::centred);
    };

    // The header: the mark and the name; UNDO, REDO; the preset; A/B/C/D.
    {
        const auto h = header();
        g.setColour (look().hair);
        g.fillRect (h.withTop (h.getBottom() - 1));
        if (mark != nullptr)
            mark->drawWithin (g, juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ 22.0f, (float) h.getCentreY() }), juce::RectanglePlacement::centred, 1.0f);
        g.setColour (look().tx);
        look().drawTracked (g, "CAB IR GENERATOR", { 42.0f, (float) h.getY(), 170.0f, (float) h.getHeight() }, font, 0.08f, juce::Justification::centredLeft);
        button ("UNDO", undoArea(), false, history.canUndo());
        button ("REDO", redoArea(), false, history.canRedo());
        const bool edited = presetName.isEmpty() || toToml (state) != presetText;
        const auto pr = presetArea().toFloat().reduced (0.5f);
        g.setColour (look().panel2);
        g.fillRoundedRectangle (pr, 4.0f);
        g.setColour (look().txFaint);
        g.drawRoundedRectangle (pr, 4.0f, 1.0f);
        g.setColour (presetName.isEmpty() ? look().txFaint : edited ? look().txDim : look().tx);
        look().drawTracked (g, (presetName.isEmpty() ? juce::String ("PRESETS") : presetName.toUpperCase()) + (edited && presetName.isNotEmpty() ? " *" : ""),
                            pr.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
        look().drawTracked (g, juce::String::fromUTF8 ("\xe2\x96\xbe"), pr.reduced (10.0f, 0.0f), font, 0.0f, juce::Justification::centredRight);
        button (juce::String::fromUTF8 ("\xe2\x97\x80"), prevArea(), false);
        button (juce::String::fromUTF8 ("\xe2\x96\xb6"), nextArea(), false);
        static const char* const letters[] { "A", "B", "C", "D" };
        for (int i = 0; i < 4; ++i)
            button (letters[i], registerArea (i), i == history.active(), true);
    }

    const auto s = strip();
    g.setColour (look().hair);
    g.fillRect (s.withHeight (1));

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
    picker.setBounds (0, headerH, CabPicker::panelW, CabPicker::panelH);
    badge.setBounds (versionArea());
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (undoArea().contains (p))
        history.undo();
    else if (redoArea().contains (p))
        history.redo();
    else if (presetArea().contains (p))
        showPresets();
    else if (prevArea().contains (p))
        stepPreset (-1);
    else if (nextArea().contains (p))
        stepPreset (1);
    else if (const int reg = [&] { for (int i = 0; i < 4; ++i) if (registerArea (i).contains (p)) return i; return -1; }(); reg >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "COPY THE ONE PLAYING HERE", reg != history.active());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (registerArea (reg))),
                                [this, reg] (int id) { if (id == 1) history.copyRegister (history.active(), reg); });
        }
        else
            history.switchTo (reg);
        repaint (header());
    }
    else if (openArea().contains (p))
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
