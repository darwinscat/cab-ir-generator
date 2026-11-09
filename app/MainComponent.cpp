// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "MainComponent.h"
#include "Flavour.h"
#include "Table.h"

#include <BinaryData.h>
#include <felitronics/appkit/AudioSettingsPanel.h>
#include <felitronics/appkit/BrandAssets.h>
#include <felitronics/toml/Toml.h>

#include <cstring>
#include <iterator>
#include <panel/Link.h>
#include <synth/CabSynth.h>
#include <synth/MicSearch.h>
#include <synth/Response.h>

namespace cabir::app
{

namespace
{
    /** The logo's mark for the header: its icon's tile left out (the first rounded rectangle), the lines alone on the
        panel, the way the family's headers draw the orbit. */
    std::unique_ptr<juce::Drawable> markOf()
    {
        auto xml = juce::parseXML (juce::String::fromUTF8 (BinaryData::mark_svg, BinaryData::mark_svgSize));
        if (xml == nullptr)
            return {};
        if (auto* tile = xml->getChildByName ("rect"))
            xml->removeChildElement (tile, true);
        return juce::Drawable::createFromSVG (*xml);
    }

    std::unique_ptr<juce::Drawable> catOf()
    {
        return juce::Drawable::createFromImageData (felitronics::appkit::brand::assets::catSvg, std::strlen (felitronics::appkit::brand::assets::catSvg));
    }

    /** The product's page on the site, where every link the app offers leads (the owner, 05.11: none to GitHub). */
    const juce::String productUrl = juce::String (flavour::page) + "?utm_source=" + flavour::utmSource
                                  + "&utm_medium=" + (flavour::plugin ? "plugin" : "app");

    // The riffs the player plays (Flavour.h), read from the checkout the app was built from (CABIR_RIFFS_DIR) — the owner's
    // machine, nowhere else: where they are not, there is no player (the owner, 09.11). A plugin has none, the track plays.
    using flavour::riffs;
    using flavour::numRiffs;
    const juce::String riffKey = "riff:";   // the setting's prefix for one of these, else a file's path
    const juce::String inputKey = "input:"; // ...or the setting for the live input

    juce::File riffFile (int i)
    {
#ifdef CABIR_RIFFS_DIR
        return juce::File (CABIR_RIFFS_DIR).getChildFile (riffs[i].file);
#else
        juce::ignoreUnused (i);
        return {};
#endif
    }

    /** The first riff on disk, -1 for none: the player shows only with one. */
    int firstRiff()
    {
        for (int i = 0; i < numRiffs; ++i)
            if (riffFile (i).existsAsFile())
                return i;
        return -1;
    }

    /** The cabinet table: what the download's note reads the IR's length from. */
    const synth::Table& cabinetTable() { return playedTable(); }

    juce::PropertiesFile::Options storage()
    {
        // A plugin's settings apart from the app's, so neither writes over what the other kept.
        juce::PropertiesFile::Options o;
        o.applicationName     = flavour::plugin ? juce::String (flavour::product) + " Plugin" : juce::String (flavour::product);
        o.filenameSuffix      = ".settings";
        o.folderName          = "Darwin's Cat";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::Font brandFont (float height)
    {
        return felitronics::appkit::brand::wordmarkFont (felitronics::appkit::brand::wordmarkTypeface(), height);
    }

#if CABIR_LISTEN
    /** THE OWNER'S SETUP: a row for each of the table's options, ticked while heard; a click turns it, and the cabinet is
        rendered again. */
    class SetupView : public juce::Component
    {
    public:
        static constexpr int rowH = 36, top = 44, bottom = 24, width = 460;

        SetupView (std::vector<std::pair<std::string, std::string>> all, std::vector<std::string> off,
                   std::function<void (const std::vector<std::string>&)> chosen)
            : options (std::move (all)), switchedOff (std::move (off)), onChosen (std::move (chosen))
        {
            setSize (width, top + rowH * (int) options.size() + bottom);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (look().panel);
            const auto font = look().font (13.0f), name = look().font (15.0f);
            g.setColour (look().txDim);
            look().drawTracked (g, "TICKED, HEARD. UNTICKED, AS BEFORE (V6).", getLocalBounds().removeFromTop (top).reduced (18, 0).toFloat(),
                                font, 0.06f, juce::Justification::centredLeft);
            for (size_t i = 0; i < options.size(); ++i)
            {
                const auto row = rowArea ((int) i);
                const bool on = isOn (i);
                const auto box = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ (float) row.getX() + 28.0f, (float) row.getCentreY() });
                g.setColour (look().panel2);
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (on ? look().orange : look().txFaint);
                g.drawRoundedRectangle (box, 4.0f, on ? 1.6f : 1.0f);
                if (on)
                {
                    juce::Path tick;
                    tick.startNewSubPath (box.getX() + 5.0f, box.getCentreY());
                    tick.lineTo (box.getX() + 9.0f, box.getBottom() - 5.5f);
                    tick.lineTo (box.getRight() - 4.5f, box.getY() + 5.0f);
                    g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (on ? look().tx : look().txDim);
                look().drawTracked (g, options[i].second, row.withTrimmedLeft (50).toFloat(), name, 0.06f, juce::Justification::centredLeft);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (size_t i = 0; i < options.size(); ++i)
                if (rowArea ((int) i).contains (e.getPosition()))
                {
                    const auto n = options[i].first;
                    if (isOn (i))
                        switchedOff.push_back (n);
                    else
                        switchedOff.erase (std::remove (switchedOff.begin(), switchedOff.end(), n), switchedOff.end());
                    repaint();
                    onChosen (switchedOff);
                }
        }

    private:
        std::vector<std::pair<std::string, std::string>> options;
        std::vector<std::string> switchedOff;
        std::function<void (const std::vector<std::string>&)> onChosen;

        bool isOn (size_t i) const { return std::find (switchedOff.begin(), switchedOff.end(), options[i].first) == switchedOff.end(); }
        juce::Rectangle<int> rowArea (int i) const { return { 0, top + i * rowH, getWidth(), rowH }; }
    };
#endif

    /** What the version window says that the badge cannot know: who we are, what this build is and was built against. */
    felitronics::appkit::VersionBadge::Config about()
    {
        felitronics::appkit::VersionBadge::Config c;
        c.productName = flavour::product;
        c.productUrl  = productUrl;
        c.gitHash     = CABIR_GIT_HASH;
        c.os          = juce::SystemStats::getOperatingSystemName();
        c.licence     = "AGPL-3.0+";
        const auto juceVersion = juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf (" ", false, false);
        // What it was built on, said as plain text: no repository named, no link to one.
        c.dependencies = { { .label = "felitronics-core",   .version = CABIR_CORE_VERSION },
                           { .label = "felitronics-toml",   .version = CABIR_TOML_VERSION },
                           { .label = "felitronics-appkit", .version = CABIR_APPKIT_VERSION },
                           { .label = "JUCE",               .version = juceVersion } };
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

MainComponent::MainComponent (Session* host)
    : session (host),
      checker ({ .ownerRepo = "darwinscat/cab-ir-generator", .productName = flavour::product, .currentVersion = CABIR_VERSION,
                 .settings = [this] { return properties.getUserSettings(); } }),
      badge (checker, about(), host != nullptr ? host->format() : juce::String ("Standalone")),
      history (felitronics::appkit::CompareHistory::Mode::PerRegister,
               [this]
               {
                   juce::ValueTree t ("Cabinet");
                   t.setProperty ("state", juce::String (toToml (state)), nullptr);
                   return t;
               },
               [this] (const juce::ValueTree& t)
               {
                   const auto s = knownState (t.getProperty ("state").toString().toStdString());
                   show (s ? *s : defaultState());   // one an older version kept, of a box since renamed: the default
               },
               felitronics::appkit::CompareHistory::Config {})
{
    properties.setStorageParameters (storage());
    if (session == nullptr)
    {
        carryFormerName();
        engine = std::make_unique<Engine>();
#if CABIR_LISTEN
        // The options the owner's SETUP switched off the last run, before the first cabinet is rendered.
        std::vector<std::string> off;
        for (const auto& n : juce::StringArray::fromTokens (properties.getUserSettings()->getValue ("optionsOff"), " ", ""))
            off.push_back (n.toStdString());
        PlayedTable::get().setOptionsOff (off);
#endif
    }
    mark = markOf();
    cat = catOf();

    // The panel in the family's look: its palette, Michroma, and the tracking the panel lays its words out by.
    look().font = brandFont;

    picker.onSetup    = [this] (int cab, CabPicker::Setup setup) { keep (CabPicker::stateOf (cab, setup)); };
    picker.onMove     = [this] (int cab, CabPicker::Setup setup) { render (CabPicker::stateOf (cab, setup), true); };
    picker.onDownload = [this] { download(); };
    picker.onDownloadFormat = [this] { showFormats(); };
    // SPECTRUM, the player's view: on until switched off in the menu below, kept between runs (the owner, 09.11).
    picker.hasSound = true;
    picker.setSpectrumOn (properties.getUserSettings()->getBoolValue ("spectrum", true));
    // A right click over the response: EQ and SPECTRUM, each ticked while on, and RESET EQ (the owner, 09.11).
    picker.onEqMenu = [this]
    {
        juce::PopupMenu menu;
        const bool eq = picker.eqOn(), spectrum = picker.spectrumOn();
        menu.addItem ("EQ", true, eq, [this, eq] { picker.setEqOn (! eq); });
        menu.addItem ("SPECTRUM", true, spectrum, [this, spectrum]
        {
            picker.setSpectrumOn (! spectrum);
            properties.getUserSettings()->setValue ("spectrum", ! spectrum);
        });
        menu.addSeparator();
        menu.addItem ("RESET EQ", [this] { picker.resetEq(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    };
    picker.onFind     = [this] (int cab, CabPicker::Setup setup) { findPlace (CabPicker::stateOf (cab, setup), true); };   // FIND looks harder
    picker.showDefault = false;   // DEFAULT is the preset menu's first item here
    addAndMakeVisible (picker);

    badge.setBrandTypeface (felitronics::appkit::brand::wordmarkTypeface());
    addChildComponent (badge);   // never shown: the strip draws the version, the badge holds the window it opens

    // The workspace the last run left — A/B/C/D and the one that plays — else the instrument's default; a plugin's from
    // its session, the host's project.
    history.onHistoryChanged = [this] { repaint (header()); keepSession(); };
    loadWorkspace (session != nullptr ? session->workspace() : properties.getUserSettings()->getValue ("workspace"));
    if (session != nullptr)
        session->addChangeListener (this);

    // The output the last run used, else the system's.
    if (engine != nullptr)
    {
        const auto audio = juce::parseXML (properties.getUserSettings()->getValue ("audio"));
        engine->devices.initialise (0, 2, audio.get(), true);
        engine->devices.addAudioCallback (engine.get());
        engine->devices.addChangeListener (this);
    }

    // DOWNLOAD IR's format the last run chose, else 48 kHz, 24 bit, the whole IR.
    {
        const auto words = juce::StringArray::fromTokens (properties.getUserSettings()->getValue ("downloadFormat"), " ", "");
        if (words.size() == 3)
        {
            const int r = words[0].getIntValue(), b = words[1].getIntValue(), n = words[2].getIntValue();
            if (DownloadFormat::offered (r, b, n))
                format = { r, b, n };
        }
        noteDownload();
    }

    // No riffs where the app was built — anyone's machine but the owner's: no player, the input AUDIO opened heard at once;
    // none until one is picked there, so a laptop's own mic does not howl through its speakers on the first run (the device
    // starts with no input and keeps what was picked).
    player = engine != nullptr && firstRiff() >= 0;
    if (engine != nullptr && ! player)
        engine->inputOnly();
    // With them, the sample the last run played — a riff, a file, or the live input (silent until played) — else the first riff.
    if (player)
    {
        const auto last = properties.getUserSettings()->getValue ("sample");
        if (last == inputKey)
            useInput();
        int riff = firstRiff();
        for (int i = 0; i < numRiffs; ++i)
            if (last == riffKey + riffs[i].file && riffFile (i).existsAsFile())
                riff = i;
        if (! last.startsWith (riffKey) && juce::File::isAbsolutePath (last) && juce::File (last).existsAsFile())
            loadSample (juce::File (last));
        if (! engine->hasSample())
            loadRiff (riff);
    }

    setSize (CabPicker::panelW, headerH + CabPicker::panelH - CabPicker::footRowH + stripH);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    alive.reset();
    sound().spectrum().want (false);   // a plugin plays on with its window closed: nothing fed for it
    sound().spectrumPre().want (false);
    worker.removeAllJobs (true, 2000);
    if (session != nullptr)
    {
        // The window closed, the plugin plays on: the pair, whatever BYPASS had taken out, and the workspace as it was left.
        session->removeChangeListener (this);
        offA = offB = false;
        playHeard();
        keepSession();
    }
    else
    {
        engine->devices.removeChangeListener (this);
        if (const auto xml = engine->devices.createStateXml())
            properties.getUserSettings()->setValue ("audio", xml->toString());
        properties.getUserSettings()->setValue ("workspace", history.toTree().toXmlString());
    }
    properties.saveIfNeeded();
}

void MainComponent::carryFormerName()
{
    // THE NAME IT HAD BEFORE (Flavour.h's `formerly`; the owner renamed the apps 07.11): on the first run under the new one
    // its settings — the workspace, the window, the output, the sample — and the player's own presets come along, copied,
    // the old files left where they were.
    auto& now = *properties.getUserSettings();
    auto before = storage();
    before.applicationName = flavour::formerly;
    if (! now.getFile().existsAsFile() && before.getDefaultFile().existsAsFile())
    {
        juce::PropertiesFile old (before);
        now.addAllPropertiesFrom (old);
        now.saveIfNeeded();
    }
    const auto folder = presetFolder();
    const auto former = folder.getParentDirectory().getParentDirectory().getChildFile (flavour::formerly).getChildFile ("Presets");
    if (! folder.exists() && former.isDirectory() && folder.createDirectory())
        for (const auto& f : former.findChildFiles (juce::File::findFiles, false, "*.toml"))
            f.copyFileTo (folder.getChildFile (f.getFileName()));
}

void MainComponent::loadWorkspace (const juce::String& xml)
{
    const auto workspace = juce::parseXML (xml);
    if (workspace == nullptr || ! history.fromTree (juce::ValueTree::fromXml (*workspace)))
    {
        show (defaultState());
        history.reset();
    }
}

void MainComponent::keepSession()
{
    if (session != nullptr)
        session->keepWorkspace (history.toTree().toXmlString(), toToml (state));
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* from)
{
    if (session != nullptr && from == session)
        loadWorkspace (session->workspace());
    else
    {
        // The app's audio device changed: an input opened in AUDIO is what plays now (the owner, 09.11: its meter moved
        // while a riff stood silent) — at once if the riff was playing; and the strip names it.
        if (player && ! engine->usingInput() && engine->inputName().isNotEmpty())
            useInput (true);
        repaint (strip());
    }
}

void MainComponent::render (const State& s, bool live)
{
    const int mine = ++latest;
    std::weak_ptr<int> stillHere = alive;
    const auto meta = metaOf (s);
    // A hand on the EQ alone moves no mic: the mics as last rendered are kept, at full size, so the live one is full too.
    auto cabinet = s;
    cabinet.eq = {};
    const bool small = live && cabinet != lastCabinet;
    lastCabinet = cabinet;
    worker.addJob ([this, stillHere, mine, s, live, small, meta]
    {
        if (mine != latest.load())
            return;   // superseded before it began
        const auto& table = playedTable();
        // The mic that did not move kept, the one that did rendered at half the size while the hand is on it.
        auto ir = renderer.render (table, s.box, synth::pairOf (s), small ? (size_t) 1 << 13 : (size_t) 1 << 14);
        auto wav = synth::wav24 (ir.mix, 48000, &meta);

        juce::MessageManager::callAsync ([this, stillHere, mine, s, live, ir, wav]() mutable
        {
            if (stillHere.lock() == nullptr)
                return;
            // A moving hand: whatever finished is heard unless a later one was; a kept one only when it is the last asked.
            if (live ? mine <= heard : mine != latest.load())
                return;
            heard = mine;

            juce::MemoryBlock bytes (wav.data(), wav.size());
            auto now = synth::responseOf (synth::as24Bit (ir.mix), 48000.0, s.eq);   // the IR as the WAV holds it, as the page draws it
            auto comb = ir.a.empty() ? std::vector<float>() : synth::combOf (ir.a, ir.b);
            auto before = keptCurve;
            irMix = std::move (ir.mix);
            irA = std::move (ir.a);
            irB = std::move (ir.b);
            levelMix = ir.levelMix;
            levelA = ir.levelA;
            levelB = ir.levelB;
            playHeard();
            repaint (strip());   // BYPASS's A and B, live with two mics heard
            if (! live)
            {
                keptWav = std::move (bytes);
                keptCurve = now;
            }
            picker.showResponse (std::move (now), std::move (before), std::move (comb));
        });
    });
}

State MainComponent::defaultState()
{
    return flavour::defaultState();
}

int MainComponent::writeStills (const juce::File& folder, float scale)
{
    if (! folder.createDirectory())
        return 1;
    look().font = brandFont;
    const auto& table = playedTable();

    // The panel as the app has it: no foot, FIND and DOWNLOAD IR offered.
    CabPicker picker;
    picker.showDefault = false;
    picker.onFind = [] (int, CabPicker::Setup) {};
    picker.onDownload = [] {};
    picker.setBounds (0, 0, CabPicker::panelW, CabPicker::panelH - CabPicker::footRowH);

    synth::PairRenderer renderer;
    const auto shown = [&] (const State& s)
    {
        const auto ir = renderer.render (table, s.box, synth::pairOf (s), (size_t) 1 << 14);
        const auto [cab, setup] = CabPicker::setupOf (s);
        picker.show (cab, setup);
        picker.showResponse (synth::responseOf (synth::as24Bit (ir.mix), 48000.0, s.eq), {},
                             ir.a.empty() ? std::vector<float>() : synth::combOf (ir.a, ir.b));
    };
    int failed = 0;
    const auto save = [&] (const juce::File& file, const juce::Image& image)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (image, out))
            ++failed;
    };
    const auto write = [&] (const char* name)
    {
        // On the panel's own colour, as the window under it paints it in the app.
        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) picker.getWidth() * scale),
                           juce::roundToInt ((float) picker.getHeight() * scale), true);
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            g.fillAll (look().panel);
            picker.paintEntireComponent (g, true);
        }
        save (folder.getChildFile (name), image);
    };

    const auto first = defaultState();
    shown (first);
    write ("panel-default@2x.png");

    // A pair that combs: B heard, out in the room, on a clock of its own, the two even — its zebra plain (the owner, 07.11).
    auto combing = first;
    combing.b.on = true;   // the guitar's first has B dark
    combing.b.x = 0.4;
    combing.b.y = 0.0;
    combing.b.distance = 0.30;
    combing.align = false;
    combing.invert = false;
    combing.shiftMs = 0.0;
    combing.blend = 0.5;
    shown (combing);
    write ("panel-comb@2x.png");

    // One mic: A alone.
    auto alone = first;
    alone.b.on = false;
    shown (alone);
    write ("panel-one-mic@2x.png");

    // FIND: every place it judged on the speaker, B where it found the quietest.
    auto found = first;
    const auto place = synth::quietPlaceForB (table, first, (size_t) 1 << 13,
                                              [&picker] (const synth::Place& p, double score) { picker.showSearch ((float) p.x, (float) p.y, (float) score); });
    picker.endSearch();
    found.b.x = place.x;
    found.b.y = place.y;
    found.b.distance = place.distance;
    found.shiftMs = place.shiftMs;
    shown (found);
    write ("panel-find@2x.png");

    // The EQ on (the owner, 09.11): both passes in, two bells off nought, and under them what plays — noise through the
    // cabinet, read as the window reads it.
    {
        auto eqd = first;
        eqd.eq.on = true;
        eqd.eq.highPassHz = 90;
        eqd.eq.highPassSlope = 24;
        eqd.eq.lowPassHz = 7000;
        eqd.eq.bells[0].db = -3.0;
        eqd.eq.bells[2] = { 3500, 2.5, 1.4 };
        shown (eqd);
        const auto ir = renderer.render (table, eqd.box, synth::pairOf (eqd), (size_t) 1 << 14).mix;
        juce::Random random (9);
        std::vector<synth::cd> x ((size_t) 1 << 17, 0.0), h (x.size(), 0.0);   // 64k of noise through the IR, by FFT
        for (size_t i = 0; i < x.size() / 2; ++i)
            x[i] = random.nextFloat() * 2.0f - 1.0f;
        for (size_t i = 0; i < ir.size(); ++i)
            h[i] = ir[i];
        std::vector<float> into (x.size() / 2);   // what goes in, a little under what comes out
        for (size_t i = 0; i < into.size(); ++i)
            into[i] = 0.05f * (float) x[i].real();
        synth::fft (x, false);
        synth::fft (h, false);
        for (size_t k = 0; k < x.size(); ++k)
            x[k] *= h[k];
        synth::fft (x, true);
        std::vector<float> played (x.size() / 2);
        for (size_t i = 0; i < played.size(); ++i)
            played[i] = 0.25f * (float) x[i].real();
        SpectrumFeed feed, feedPre;
        for (auto* f : { &feed, &feedPre })
        {
            f->prepare (48000.0);
            f->want (true);
        }
        std::vector<float> hz;
        for (int i = 0; i < CabPicker::spectrumColumns; ++i)
            hz.push_back (CabPicker::spectrumHz (i));
        SpectrumView view (hz), viewPre (hz);
        std::vector<float> columns, columnsPre;
        for (size_t at = 0; at + 1600 <= played.size(); at += 1600)
        {
            feed.feed (played.data() + at, nullptr, 1600);
            feedPre.feed (into.data() + at, nullptr, 1600);
            columns = view.tick (feed);
            columnsPre = viewPre.tick (feedPre);
        }
        picker.hasSound = true;   // a host with sound, for these two alone: the page has none
        picker.showSpectrum (columns, columnsPre);
        write ("panel-eq@2x.png");
        auto flat = first;   // ...and as it is first switched on: the passes out at their edges, the bells flat
        flat.eq.on = true;
        shown (flat);
        write ("panel-eq-flat@2x.png");
        picker.showSpectrum ({}, {});
        picker.hasSound = false;
    }

    // The whole window as the app opens on its DEFAULT — its header, the panel, its strip, 960 x 540 — twice the stills'
    // scale, 3840 x 2160 at their 2x: a page's 16:9 picture, larger than a screen holds the window (the owner, 07.11);
    // and the same with the pair that combs, its zebra across the response.
    {
        MainComponent main;   // the app's own, on the stills' run's settings: the default, the first riff
        const auto window = [&] (const State& s, const char* name)
        {
            auto ir = renderer.render (table, s.box, synth::pairOf (s), (size_t) 1 << 14);
            const auto [cab, setup] = CabPicker::setupOf (s);
            main.picker.show (cab, setup);
            main.picker.showResponse (synth::responseOf (synth::as24Bit (ir.mix), 48000.0, s.eq), {},
                                      ir.a.empty() ? std::vector<float>() : synth::combOf (ir.a, ir.b));
            main.irMix = std::move (ir.mix);   // BYPASS's A and B live with two mics heard
            main.irA = std::move (ir.a);
            main.irB = std::move (ir.b);
            save (folder.getChildFile (name), main.createComponentSnapshot (main.getLocalBounds(), true, 2.0f * scale));
        };
        window (first, "window-default@4x.png");
        window (combing, "window-comb@4x.png");
    }

    // Each box of the pile alone, as the pile draws it unchosen, for a page's list of sizes (the owner, 05.11): one scale
    // for all, so each stands as big as itself against the others — 420 px a metre, about three and a half times the
    // pile's — on nothing, its tolex opaque; box-<size>.png (at `scale`) and box-<size>.svg in boxes/ — or, where two
    // boxes of the pile share a size (the guitar's two 2x12s), box-<its table name>, so neither writes over the other.
    const auto boxes = folder.getChildFile ("boxes");
    if (! boxes.createDirectory())
        return failed + 1;
    constexpr float perMetre = 420.0f, margin = 4.0f;   // margin: the outline's line kept inside the picture
    for (const auto& column : Panel::pile())
        for (const auto& name : column)
        {
            const int i = Panel::indexOf (name);
            const auto& c = CabPicker::cabs()[(size_t) i];
            const float w = c.w * perMetre, h = c.h * perMetre;
            int sharing = 0;
            for (const auto& col : Panel::pile())
                for (const auto& other : col)
                    sharing += CabPicker::wordOf (Panel::indexOf (other)) == CabPicker::wordOf (i) ? 1 : 0;
            const juce::String stem = "box-" + juce::String (sharing > 1 ? name.toRawUTF8() : CabPicker::wordOf (i).toRawUTF8());
            juce::Image image (juce::Image::ARGB, juce::roundToInt ((w + 2.0f * margin) * scale), juce::roundToInt ((h + 2.0f * margin) * scale), true);
            {
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                CabPicker::paintBox (g, i, { margin, margin, w, h });
            }
            save (boxes.getChildFile (stem + ".png"), image);

            // ...and as SVG beside it, at the same 420 px a metre: crisp at any size, lighter on a page.
            const auto svg = CabPicker::boxSvg (i, w + 2.0f * margin, h + 2.0f * margin, { margin, margin, w, h });
            const auto file = boxes.getChildFile (stem + ".svg");
            file.deleteFile();
            juce::FileOutputStream out (file);
            if (! out.openedOk() || ! out.write (svg.data(), svg.size()))
                ++failed;
        }

    // Each mic's glyph alone as SVG, in the colour the panel draws a mic not chosen (txFaint) — its black turned to it at
    // each part's own opacity, its white highlights kept, as tintGlyph does — 36 x 96 for all; mic-<name>.svg in mics/.
    const auto mics = folder.getChildFile ("mics");
    if (! mics.createDirectory())
        return failed + 1;
    const auto faint = "#" + look().txFaint.toDisplayString (false).toLowerCase();
    for (const auto& name : Panel::mics())
    {
        const auto* svg = micSvg (name.toStdString());
        if (svg == nullptr)
        {
            ++failed;
            continue;
        }
        const auto text = juce::String::fromUTF8 (svg).replace ("#000000", faint)
                              .replace ("viewBox=\"0 0 36 96\"", "width=\"36\" height=\"96\" viewBox=\"0 0 36 96\"") + "\n";
        const auto file = mics.getChildFile ("mic-" + juce::String::fromUTF8 (name.toRawUTF8()) + ".svg");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! out.write (text.toRawUTF8(), text.getNumBytesAsUTF8()))
            ++failed;
    }
    return failed;
}

void MainComponent::keep (const State& s)
{
    const bool bTakenUp = ! state.b.on && s.b.on;
    state = s;
    noteDownload();
    render (s, false);
    repaint (header());
    if (bTakenUp)
        findPlace (s);
}

void MainComponent::findPlace (const State& s, bool thorough)
{
    // B just taken up, or FIND: with both mics heard where B combs least with A, with one where that one stands without
    // a pit, about where it is — found on the worker, each place it judges drawn on the speaker as it goes; the mic
    // moves there unless the hand moved it first.
    const bool pair = s.a.on && s.b.on, alone = s.a.on;   // alone: which mic, with one heard — A, else B
    std::weak_ptr<int> stillHere = alive;
    worker.addJob ([this, stillHere, s, thorough, pair, alone]
    {
        const auto& table = playedTable();
        const auto tried = [this, stillHere] (const synth::Place& p, double score)
        {
            juce::MessageManager::callAsync ([this, stillHere, p, score]
            {
                if (stillHere.lock() != nullptr)
                    picker.showSearch ((float) p.x, (float) p.y, (float) score);
            });
        };
        const auto place = pair ? synth::quietPlaceForB (table, s, (size_t) 1 << 13, tried, thorough)
                                : synth::smoothPlaceForMic (table, s, (size_t) 1 << 14, tried);
        juce::MessageManager::callAsync ([this, stillHere, s, place, pair, alone]
        {
            if (stillHere.lock() == nullptr)
                return;
            picker.endSearch();
            const auto moved = [&] (const State& now) { return pair ? &now.b : alone ? &now.a : &now.b; };
            if (! (*moved (state) == *moved (s)) || state.box != s.box || (state.a.on && state.b.on) != pair)
                return;
            auto next = state;
            auto& m = pair ? next.b : alone ? next.a : next.b;
            m.x = place.x;
            m.y = place.y;
            m.distance = place.distance;
            if (pair)
                next.shiftMs = place.shiftMs;
            show (next);
        });
    });
}

void MainComponent::show (const State& s)
{
    state = s;
    noteDownload();
    const auto [cab, setup] = CabPicker::setupOf (s);
    picker.show (cab, setup);
    render (s, false);
    repaint (header());
}

// ---------------------------------------------------------------- presets
juce::File MainComponent::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Darwin's Cat").getChildFile (flavour::product).getChildFile ("Presets");
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
    const auto s = knownState (toml.toStdString());
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
    constexpr int defaultId = 100002;
    menu.addItem (defaultId, "DEFAULT");
    menu.addSeparator();
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
                            if (id == defaultId)
                            {
                                show (defaultState());
                                presetName.clear();
                                presetText.clear();
                            }
                            else if (id == saveId)
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
    if (engine == nullptr)
        return;
    const bool wasPlaying = engine->isPlaying();
    if (! engine->loadSample (file))
        return;
    properties.getUserSettings()->setValue ("sample", file.getFullPathName());
    riffNow = -1;
    engine->play (wasPlaying);
    repaint (strip());
}

void MainComponent::loadRiff (int i)
{
    if (engine == nullptr)
        return;
    const bool wasPlaying = engine->isPlaying();
    if (i < 0 || ! engine->loadSample (riffFile (i), riffs[i].title))
        return;
    properties.getUserSettings()->setValue ("sample", riffKey + riffs[i].file);
    riffNow = i;
    engine->play (wasPlaying);
    repaint (strip());
}

void MainComponent::useInput (bool play)
{
    if (engine == nullptr)
        return;
    engine->useInput (play);   // silent until PLAY: a laptop's own mic beside its speakers would howl
    properties.getUserSettings()->setValue ("sample", inputKey);
    riffNow = -1;
    repaint (strip());
}

void MainComponent::showSamples()
{
    // The live input first, the instrument through the cabinet as through an amp's (the owner, 07.11); then our riffs.
    juce::PopupMenu menu;
    constexpr int inputId = 2000;
    menu.addItem (inputId, "INPUT (AUDIO INTERFACE)", true, engine->usingInput());
    menu.addSeparator();
    for (int i = 0; i < numRiffs; ++i)
        if (riffFile (i).existsAsFile())
            menu.addItem (i + 1, riffs[i].title, true, i == riffNow);
    menu.addSeparator();
    constexpr int fileId = 1000;
    menu.addItem (fileId, "OPEN A FILE...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (sampleArea())),
                        [this] (int id)
                        {
                            if (id == inputId)
                                useInput();
                            else if (id == fileId)
                                openSample();
                            else if (id >= 1 && id <= numRiffs)
                                loadRiff (id - 1);
                        });
}

void MainComponent::openSample()
{
    chooser = std::make_unique<juce::FileChooser> (flavour::samplePrompt, juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) loadSample (c.getResult()); });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray&) { return player; }   // a sample for the player

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (player && ! files.isEmpty())
        loadSample (juce::File (files[0]));
}

void MainComponent::showAudio()
{
    // The output, and the input INPUT plays: a device and its channels, every one open heard, summed; opening one plays it.
    felitronics::appkit::AudioSettingsPanel::Options o;
    o.minInputs = 0;
    o.maxInputs = 2;
    auto panel = std::make_unique<felitronics::appkit::AudioSettingsPanel> (engine->devices, o);
    panel->setSize (460, 380);
    juce::CallOutBox::launchAsynchronously (std::move (panel), audioArea().translated (0, 0), this);
}

#if CABIR_LISTEN
void MainComponent::showSetup()
{
    auto view = std::make_unique<SetupView> (PlayedTable::options(), PlayedTable::get().optionsOff(),
                                             [this] (const std::vector<std::string>& off) { optionsChosen (off); });
    juce::CallOutBox::launchAsynchronously (std::move (view), setupArea(), this);
}

void MainComponent::optionsChosen (const std::vector<std::string>& off)
{
    // The table without the options switched off, kept for the next run, and the cabinet heard through it.
    PlayedTable::get().setOptionsOff (off);
    juce::StringArray words;
    for (const auto& n : off)
        words.add (n);
    properties.getUserSettings()->setValue ("optionsOff", words.joinIntoString (" "));
    render (state, false);
}
#endif

synth::WavMeta MainComponent::metaOf (const State& s) const
{
    // What the WAV says of itself (synth::WavMeta): the preset's name while it is the one that plays, else the cabinet as a
    // line says it; the link that opens it on the page; and its TOML, named the same.
    synth::WavMeta meta;
    meta.name = (presetName.isNotEmpty() && toToml (s) == presetText ? presetName : CabPicker::labelOf (s)).toStdString();
    meta.software = std::string (flavour::product) + " " + CABIR_VERSION;
    meta.comment = std::string (flavour::page) + "#" + linkOf (s);   // its own page: the bass's, or the guitar's
    meta.preset = "name = \"" + meta.name + "\"\n\n" + toToml (s);
    return meta;
}

void MainComponent::download()
{
    if (keptWav.isEmpty())
        return;
    // The cabinet that plays, synthesised again at the format's rate — no resampling — cut to its length and written at its
    // bits; the format said in the file's name where it is not the usual one.
    const auto f = format;
    const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (downloadName (state, f).c_str()));
    auto folder = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Download IR", folder.getChildFile (name + ".wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [s = state, f, meta = metaOf (state)] (const juce::FileChooser& c)
                          {
                              const auto file = c.getResult();
                              if (file == juce::File())
                                  return;
                              const auto& table = playedTable();
                              const auto wav = downloadWav (table, s, f, meta);
                              file.withFileExtension ("wav").replaceWithData (wav.data(), wav.size());
                          });
}

void MainComponent::showFormats()
{
    // Rate, bits and length, each ticked where it stands; a length in samples, as a loader's maker writes it, and the ms it
    // comes to at the rate.
    juce::PopupMenu menu;
    menu.addSectionHeader ("RATE");
    for (const int r : { 44100, 48000, 96000 })
        menu.addItem (r == 44100 ? "44.1 kHz" : r == 48000 ? "48 kHz" : "96 kHz", true, format.rate == r, [this, r] { format.rate = r; formatChosen(); });
    menu.addSectionHeader ("BITS");
    for (const int b : { 24, 16 })
        menu.addItem (juce::String (b) + " bit", true, format.bits == b, [this, b] { format.bits = b; formatChosen(); });
    menu.addSectionHeader ("LENGTH");
    menu.addItem ("FULL (" + juce::String (wholeMs (cabinetTable(), state)) + " ms)", true, format.samples == 0,
                  [this] { format.samples = 0; formatChosen(); });
    for (const int n : { 2048, 1024, 512 })
        menu.addItem (juce::String (n) + " samples (" + juce::String (juce::roundToInt (1000.0 * n / format.rate)) + " ms)", true,
                      format.samples == n, [this, n] { format.samples = n; formatChosen(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MainComponent::formatChosen()
{
    properties.getUserSettings()->setValue ("downloadFormat", juce::String (format.rate) + " " + juce::String (format.bits) + " " + juce::String (format.samples));
    noteDownload();
}

void MainComponent::noteDownload()
{
    // The whole IR as long as the cabinet's: a room's tail makes it longer (the guitar's).
    picker.setDownloadNote (juce::String::fromUTF8 (formatNote (format, wholeMs (cabinetTable(), state)).c_str()));
}

void MainComponent::timerCallback()
{
    history.tick();
    sound().flush();

    // SPECTRUM: what plays and what goes in, while it is ticked, the EQ on or off — the feeds asked for them only then.
    const bool on = picker.spectrumOn();
    sound().spectrum().want (on);
    sound().spectrumPre().want (on);
    if (on)
        picker.showSpectrum (spectrumView.tick (sound().spectrum()), spectrumPreView.tick (sound().spectrumPre()));
    else if (spectrumShown)
    {
        picker.showSpectrum ({}, {});
        spectrumView.reset();
        spectrumPreView.reset();
    }
    spectrumShown = on;
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const bool over = brandLink().contains (e.getPosition());
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (over != brandHover)
    {
        brandHover = over;
        repaint (brandRow());
    }
}

void MainComponent::mouseExit (const juce::MouseEvent&)
{
    brandHover = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint (brandRow());
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('z', command, 0))
        return history.undo(), true;
    if (key == juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress ('y', command, 0))
        return history.redo(), true;
    // SPACE plays and stops the sample (the player's); the arrows step through the presets, as ◀ ▶ do (the owner, 05.11).
    if (key == juce::KeyPress::spaceKey && player && engine->hasSample())
    {
        engine->play (! engine->isPlaying());
        repaint (strip());
        return true;
    }
    if (key == juce::KeyPress::leftKey)
        return stepPreset (-1), true;
    if (key == juce::KeyPress::rightKey)
        return stepPreset (1), true;
    return false;
}

// ---------------------------------------------------------------- the header
// One row: the brand — the cat, the mark, the name with the byline under its last word, as the family's headers carry
// them — then the preset, then UNDO, REDO and A/B/C/D (the owner, 05.11); A/B/C/D and the gaps between close, so the
// preset has room for a long name beside the guitar's longer one (the owner, 07.11).
juce::Rectangle<int> MainComponent::brandRow() const   { return header(); }
juce::Rectangle<int> MainComponent::registerArea (int i) const
{
    const auto b = brandRow();
    return { b.getRight() - 6 - (4 - i) * 28 + 2, b.getCentreY() - 13, 26, 26 };
}
juce::Rectangle<int> MainComponent::redoArea() const   { return registerArea (0).withY (brandRow().getCentreY() - 15).withHeight (30).withWidth (30).translated (-34, 0); }
juce::Rectangle<int> MainComponent::undoArea() const   { return redoArea().translated (-32, 0); }
juce::Rectangle<int> MainComponent::prevArea() const
{
    return { brandLink().getRight() + 10, brandRow().getCentreY() - 15, 30, 30 };
}
juce::Rectangle<int> MainComponent::nextArea() const   { return prevArea().withX (undoArea().getX() - 10 - 30); }
juce::Rectangle<int> MainComponent::presetArea() const
{
    return { prevArea().getRight() + 6, prevArea().getY(), nextArea().getX() - 6 - prevArea().getRight() - 6, prevArea().getHeight() };
}
juce::Rectangle<int> MainComponent::brandLink() const
{
    const float h = (float) brandH;
    const float right = h * 2.0f + 18.0f + look().trackedWidth (flavour::product, look().font (h * nameShare), 0.0f) + 6.0f;
    return { 0, 0, juce::roundToInt (right), brandH };
}

// ---------------------------------------------------------------- the strip
juce::Rectangle<int> MainComponent::openArea() const    { return strip().reduced (8, 5).withWidth (70); }
juce::Rectangle<int> MainComponent::playArea() const    { return openArea().translated (78, 0).withWidth (70); }
juce::Rectangle<int> MainComponent::sampleArea() const   // the sample after OPEN and PLAY; with no player, the input first
{
    return (player ? playArea().translated (78, 0) : openArea()).withWidth (300);
}
juce::Rectangle<int> MainComponent::audioArea() const   { return versionArea().translated (-82, 0).withWidth (74); }

juce::Rectangle<int> MainComponent::bypassWordArea() const   // after the sample, BYPASS; a plugin's first in the strip
{
    const int x = engine != nullptr ? sampleArea().getRight() + 16 : openArea().getX();
    return sampleArea().withX (x).withWidth (juce::roundToInt (look().trackedWidth ("BYPASS", look().font (13.0f), 0.08f)) + 4);
}

juce::Rectangle<int> MainComponent::hearArea (int i) const   // ...then A, B, A+B, a button a word
{
    int x = bypassWordArea().getRight() + 8;
    for (int k = 0; k < i; ++k)
        x += juce::roundToInt (look().trackedWidth (bypassWords[k], look().font (13.0f), 0.06f)) + 22 + 4;
    return sampleArea().withX (x).withWidth (juce::roundToInt (look().trackedWidth (bypassWords[i], look().font (13.0f), 0.06f)) + 22);
}

MainComponent::Hear MainComponent::hearing() const
{
    if (offA && offB)
        return Hear::bypass;
    const bool parts = ! irA.empty() && ! irB.empty();
    return ! parts ? Hear::pair : offA ? Hear::b : offB ? Hear::a : Hear::pair;
}

void MainComponent::playHeard()
{
    const auto now = hearing();
    const auto& ir = now == Hear::a ? irA : now == Hear::b ? irB : irMix;
    if (! ir.empty())
        sound().setIr (ir, 48000.0, now == Hear::a ? levelA : now == Hear::b ? levelB : levelMix);
    sound().setBypass (now == Hear::bypass);
}
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

    // The header's brand row: the cat, the mark, the name and the byline; UNDO and REDO; A/B/C/D.
    {
        const auto b = brandRow().toFloat();
        const float h = b.getHeight();
        if (brandHover)
        {
            g.setColour (look().violet.withAlpha (0.14f));   // the link run lit under the hand
            g.fillRoundedRectangle (brandLink().toFloat().reduced (2.0f), 8.0f);
        }
        if (cat != nullptr)
            cat->drawWithin (g, juce::Rectangle<float> (6.0f, 4.0f, h - 8.0f, h - 8.0f), juce::RectanglePlacement::centred, 1.0f);
        if (mark != nullptr)
            mark->drawWithin (g, juce::Rectangle<float> (h + 6.0f, 4.0f, h - 8.0f, h - 8.0f), juce::RectanglePlacement::centred, 1.0f);
        // The name high, the byline under it, its right edge under the name's last word.
        const auto name = look().font (h * nameShare), by = look().font (h * 0.25f);
        const float x = h * 2.0f + 18.0f, nameW = look().trackedWidth (flavour::product, name, 0.0f);
        g.setColour (brandHover ? juce::Colours::white : look().tx);
        look().drawTracked (g, flavour::product,{ x, b.getY() + 5.0f, nameW + 4.0f, h * 0.5f }, name, 0.0f, juce::Justification::centredLeft);
        g.setColour (brandHover ? look().violet.brighter (0.3f) : look().violet);
        look().drawTracked (g, "by Darwin's Cat", { x, b.getY() + h * 0.54f, nameW, h * 0.36f }, by, 0.0f, juce::Justification::centredRight);

        const auto arrow = [&] (juce::Rectangle<int> r, bool back, bool enabled)
        {
            // a round arrow, as the family's headers draw undo and redo
            const auto c = r.toFloat().getCentre();
            const float rad = 8.0f;
            juce::Path p;
            p.addCentredArc (c.x, c.y, rad, rad, 0.0f, back ? 1.1f : -1.1f, back ? 1.1f + 4.6f : -1.1f - 4.6f, true);
            g.setColour ((enabled ? look().txDim : look().txFaint).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float a = back ? 1.1f : -1.1f;
            const juce::Point<float> tip (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
            juce::Path head;
            head.addTriangle (tip.translated (back ? -4.5f : 4.5f, -3.0f), tip.translated (back ? 3.0f : -3.0f, -4.0f), tip.translated (0.0f, 4.0f));
            g.fillPath (head);
        };
        arrow (undoArea(), true, history.canUndo());
        arrow (redoArea(), false, history.canRedo());

        static const char* const letters[] { "A", "B", "C", "D" };
        const auto letter = look().font (16.0f);
        for (int i = 0; i < 4; ++i)
        {
            const auto r = registerArea (i).toFloat();
            const bool on = i == history.active();
            if (on)
            {
                g.setColour (look().panel2);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (look().lilac);
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.4f);
            }
            g.setColour (on ? juce::Colours::white : look().txDim);
            look().drawTracked (g, letters[i], r, letter, 0.0f, juce::Justification::centred);
            if (history.registerEdited (i))   // a register changed since it was filled: an orange dot at its corner
            {
                g.setColour (look().orange);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (r.getTopRight().translated (-3.0f, 3.0f)));
            }
        }
        g.setColour (look().hair);
        g.fillRect (header().withTop (header().getBottom() - 1));
    }

    // ...and in it the preset: one before, the preset, one after.
    {
        const bool edited = presetName.isEmpty() || toToml (state) != presetText;
        const auto pr = presetArea().toFloat().reduced (0.5f);
        g.setColour (look().panel2);
        g.fillRoundedRectangle (pr, 4.0f);
        g.setColour (look().txFaint);
        g.drawRoundedRectangle (pr, 4.0f, 1.0f);
        g.setColour (presetName.isEmpty() ? look().txFaint : edited ? look().txDim : look().tx);
        // The name as it fits before the ▾: whole; else without its style (the menu says it), "2X12 INVASION"; else cut.
        const juce::String mark = edited && presetName.isNotEmpty() ? " *" : "";
        juce::String shown = presetName.isEmpty() ? juce::String ("PRESETS") : presetName.toUpperCase();
        const float room = pr.getWidth() - 20.0f - 16.0f;
        const auto fits = [&] (const juce::String& t) { return look().trackedWidth (t + mark, font, 0.06f) <= room; };
        if (! fits (shown))
            for (const char* by : { ": ", "\xc2\xb7 " })
                if (shown.contains (juce::String::fromUTF8 (by)))
                    shown = shown.fromFirstOccurrenceOf (juce::String::fromUTF8 (by), false, false).trim();
        while (shown.length() > 1 && ! fits (shown))
            shown = shown.dropLastCharacters (shown.endsWith ("...") ? 4 : 1).trimEnd() + "...";
        look().drawTracked (g, shown + mark, pr.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
        look().drawTracked (g, juce::String::fromUTF8 ("\xe2\x96\xbe"), pr.reduced (10.0f, 0.0f), font, 0.0f, juce::Justification::centredRight);
        button (juce::String::fromUTF8 ("\xe2\x97\x80"), prevArea(), false);
        button (juce::String::fromUTF8 ("\xe2\x96\xb6"), nextArea(), false);
    }

    const auto s = strip();
    g.setColour (look().hair);
    g.fillRect (s.withHeight (1));

    {   // BYPASS: each mic out lit, A+B with both; A and B dim with one mic heard
        const bool parts = ! irA.empty() && ! irB.empty();
        g.setColour (look().txFaint);
        look().drawTracked (g, "BYPASS", bypassWordArea().toFloat(), font, 0.08f, juce::Justification::centredLeft);
        button (bypassWords[0], hearArea (0), offA && parts, parts);
        button (bypassWords[1], hearArea (1), offB && parts, parts);
        button (bypassWords[2], hearArea (2), offA && offB);
    }

    if (engine != nullptr)   // the app's input and, where its riffs are, its player; a plugin's track plays through it
    {
        if (player)
        {
            button ("OPEN", openArea(), false);
            if (engine->hasSample())
                button (engine->isPlaying() ? "STOP" : "PLAY", playArea(), engine->isPlaying());
        }
        button ("AUDIO", audioArea(), false);
#if CABIR_LISTEN
        button ("SETUP", setupArea(), false);
#endif

        // The sample: a box like the preset's, its menu the riffs we carry and a file of the player's own; without the
        // player, the input alone, a click on it AUDIO.
        const auto sa = sampleArea().toFloat().reduced (0.5f);
        g.setColour (look().panel2);
        g.fillRoundedRectangle (sa, 4.0f);
        g.setColour (look().txFaint);
        g.drawRoundedRectangle (sa, 4.0f, 1.0f);
        const auto input = "INPUT: " + (engine->inputName().isNotEmpty() ? engine->inputName().toUpperCase() : juce::String ("NONE, SEE AUDIO"));
        if (player)
        {
            g.setColour (engine->hasSample() ? look().tx : look().txFaint);
            const auto source = engine->usingInput() ? input
                              : engine->hasSample()  ? engine->sampleName().toUpperCase()
                                                     : juce::String ("DROP A ") + flavour::sampleWord + " SAMPLE ON THE WINDOW";
            look().drawTracked (g, source,
                                sa.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
            look().drawTracked (g, juce::String::fromUTF8 ("\xe2\x96\xbe"), sa.reduced (10.0f, 0.0f), font, 0.0f, juce::Justification::centredRight);
        }
        else
        {
            g.setColour (engine->inputName().isNotEmpty() ? look().tx : look().txFaint);
            look().drawTracked (g, input, sa.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
        }
    }

    g.setColour (checker.updateAvailable() ? look().orange : look().txDim);
    look().drawTracked (g, "V" + checker.currentVersion(), versionArea().toFloat(), font, 0.06f, juce::Justification::centredRight);
}

void MainComponent::resized()
{
    picker.setBounds (0, headerH, CabPicker::panelW, CabPicker::panelH - CabPicker::footRowH);   // no foot: no DEFAULT, no CLOSE
    badge.setBounds (versionArea());
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (brandLink().contains (p))
    {
        juce::URL (productUrl).launchInDefaultBrowser();
        return;
    }
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
    else if (player && openArea().contains (p))
        openSample();
    else if (player && sampleArea().contains (p))
        showSamples();
    else if (player && playArea().contains (p) && engine->hasSample())
    {
        engine->play (! engine->isPlaying());
        repaint (strip());
    }
    else if (engine != nullptr && (audioArea().contains (p) || sampleArea().contains (p)))   // the input's box: AUDIO, without the player
        showAudio();
#if CABIR_LISTEN
    else if (engine != nullptr && setupArea().contains (p))
        showSetup();
#endif
    else if (const int h = [&] { for (int i = 0; i < 3; ++i) if (hearArea (i).contains (p)) return i; return -1; }(); h >= 0)
    {
        if (h < 2 && (irA.empty() || irB.empty()))
            return;   // A or B with one mic heard: nothing to take out
        if (h == 0)
            offA = ! offA;
        else if (h == 1)
            offB = ! offB;
        else
            offA = offB = ! (offA && offB);   // both out, or both back
        playHeard();
        repaint (strip());
    }
    else if (versionArea().contains (p))
        badge.showAbout();
}

} // namespace cabir::app
