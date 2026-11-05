// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "MainComponent.h"

#include <BinaryData.h>
#include <RiffData.h>
#include <felitronics/appkit/AudioSettingsPanel.h>
#include <felitronics/appkit/BrandAssets.h>
#include <felitronics/toml/Toml.h>

#include <cstring>
#include <iterator>
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
    const juce::String productUrl = "https://darwinscat.com/felitronics/cab-ir-generator?utm_source=cab-ir-generator&utm_medium=app";

    /** THE RIFFS THE APP CARRIES: Oleh's bass, played DI — the preamp already in it, so it goes straight into the cabinet —
        each a seamless loop (mono, 48 kHz, 24-bit FLAC), in the menu's order. The first plays until another is picked. */
    struct Riff { const char* file; const char* title; };
    constexpr Riff riffs[] { { "11yl.flac",              "Eleven Light Years" },
                             { "fifth-dimension-1.flac", "Fifth Dimension 1" },
                             { "fifth-dimension-2.flac", "Fifth Dimension 2" },
                             { "deep-space-1.flac",      "Deep Space is My Home 1" },
                             { "deep-space-2.flac",      "Deep Space is My Home 2" },
                             { "im-projection.flac",     "I am Projection" } };
    constexpr int numRiffs = (int) std::size (riffs);
    const juce::String riffKey = "riff:";   // the setting's prefix for one of these, else a file's path

    const char* riffBytes (int i, int& size)
    {
        for (int r = 0; r < RiffData::namedResourceListSize; ++r)
            if (juce::String (RiffData::originalFilenames[r]) == riffs[i].file)
                return RiffData::getNamedResource (RiffData::namedResourceList[r], size);
        size = 0;
        return nullptr;
    }

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
    cat = catOf();

    // The panel in the family's look: its palette, Michroma, and the tracking the panel lays its words out by.
    look().font = brandFont;

    picker.onSetup    = [this] (int cab, CabPicker::Setup setup) { keep (CabPicker::stateOf (cab, setup)); };
    picker.onMove     = [this] (int cab, CabPicker::Setup setup) { render (CabPicker::stateOf (cab, setup), true); };
    picker.onDownload = [this] { download(); };
    picker.onFind     = [this] (int cab, CabPicker::Setup setup) { findPlace (CabPicker::stateOf (cab, setup), true); };   // FIND looks harder
    picker.showDefault = false;   // DEFAULT is the preset menu's first item here
    addAndMakeVisible (picker);

    badge.setBrandTypeface (felitronics::appkit::brand::wordmarkTypeface());
    addChildComponent (badge);   // never shown: the strip draws the version, the badge holds the window it opens

    // The workspace the last run left — A/B/C/D and the one that plays — else a 4x10 with its own mic at its cap.
    history.onHistoryChanged = [this] { repaint (header()); };
    const auto workspace = juce::parseXML (properties.getUserSettings()->getValue ("workspace"));
    if (workspace == nullptr || ! history.fromTree (juce::ValueTree::fromXml (*workspace)))
    {
        show (defaultState());
        history.reset();
    }

    // The output the last run used, else the system's.
    const auto audio = juce::parseXML (properties.getUserSettings()->getValue ("audio"));
    engine.devices.initialise (0, 2, audio.get(), true);
    engine.devices.addAudioCallback (&engine);

    // The sample the last run played — a riff of ours or a file — else the first riff.
    const auto last = properties.getUserSettings()->getValue ("sample");
    int riff = 0;
    for (int i = 0; i < numRiffs; ++i)
        if (last == riffKey + riffs[i].file)
            riff = i;
    if (! last.startsWith (riffKey) && juce::File::isAbsolutePath (last) && juce::File (last).existsAsFile())
        loadSample (juce::File (last));
    if (! engine.hasSample())
        loadRiff (riff);

    setSize (CabPicker::panelW, headerH + CabPicker::panelH - CabPicker::footRowH + stripH);
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
        // The mic that did not move kept, the one that did rendered at half the size while the hand is on it.
        auto ir = renderer.render (table, s.box, synth::pairOf (s), live ? (size_t) 1 << 13 : (size_t) 1 << 14);
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
            auto now = synth::responseOf (synth::as24Bit (ir.mix), 48000.0);   // the IR as the WAV holds it, as the page draws it
            auto comb = ir.a.empty() ? std::vector<float>() : synth::combOf (ir.a, ir.b);
            auto before = keptCurve;
            irMix = std::move (ir.mix);
            irA = std::move (ir.a);
            irB = std::move (ir.b);
            playHeard();
            repaint (strip());   // HEAR's A and B, lit with two mics heard
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
    // A 4x10, ported as eight boxes of the ten are, an MD421 close and a U47 at 12 cm where the two comb least: green
    // from 40 Hz to 12 kHz (synth::quietPlaceForB found the place; the owner, 05.11).
    State s;
    s.box = "SVT-410HE";
    s.back = 1;
    s.a = { true, "md421", -0.3, 0.0, 0.05, 0.0 };
    s.b = { true, "u47", -0.65, 0.0, 0.12, 0.0 };
    s.blend = 0.35;
    s.align = true;
    return s;
}

int MainComponent::writeStills (const juce::File& folder, float scale)
{
    if (! folder.createDirectory())
        return 1;
    look().font = brandFont;
    const auto table = synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));

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
        picker.showResponse (synth::responseOf (synth::as24Bit (ir.mix), 48000.0), {},
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

    // A pair that combs: B out in the room, on a clock of its own.
    auto combing = first;
    combing.b.x = 0.4;
    combing.b.distance = 0.30;
    combing.align = false;
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

    // Each box of the pile alone, as the pile draws it unchosen, for a page's list of sizes (the owner, 05.11): one scale
    // for all, so each stands as big as itself against the others — 420 px a metre, about three and a half times the
    // pile's — on nothing, its tolex opaque; box-<size>.png (at `scale`) and box-<size>.svg in boxes/.
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
            juce::Image image (juce::Image::ARGB, juce::roundToInt ((w + 2.0f * margin) * scale), juce::roundToInt ((h + 2.0f * margin) * scale), true);
            {
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                CabPicker::paintBox (g, i, { margin, margin, w, h });
            }
            save (boxes.getChildFile ("box-" + CabPicker::wordOf (i) + ".png"), image);

            // ...and as SVG beside it, at the same 420 px a metre: crisp at any size, lighter on a page.
            const auto svg = CabPicker::boxSvg (i, w + 2.0f * margin, h + 2.0f * margin, { margin, margin, w, h });
            const auto file = boxes.getChildFile ("box-" + CabPicker::wordOf (i) + ".svg");
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
        static const auto table = synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
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
    const bool wasPlaying = engine.isPlaying();
    if (! engine.loadSample (file))
        return;
    properties.getUserSettings()->setValue ("sample", file.getFullPathName());
    riffNow = -1;
    engine.play (wasPlaying);
    repaint (strip());
}

void MainComponent::loadRiff (int i)
{
    int size = 0;
    const char* data = riffBytes (i, size);
    const bool wasPlaying = engine.isPlaying();
    if (data == nullptr || ! engine.loadSample (data, (size_t) size, riffs[i].title))
        return;
    properties.getUserSettings()->setValue ("sample", riffKey + riffs[i].file);
    riffNow = i;
    engine.play (wasPlaying);
    repaint (strip());
}

void MainComponent::showSamples()
{
    juce::PopupMenu menu;
    for (int i = 0; i < numRiffs; ++i)
        menu.addItem (i + 1, riffs[i].title, true, i == riffNow);
    menu.addSeparator();
    constexpr int fileId = 1000;
    menu.addItem (fileId, "OPEN A FILE...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (sampleArea())),
                        [this] (int id)
                        {
                            if (id == fileId)
                                openSample();
                            else if (id >= 1 && id <= numRiffs)
                                loadRiff (id - 1);
                        });
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
    // SPACE plays and stops the sample; the arrows step through the presets, as ◀ ▶ do (the owner, 05.11).
    if (key == juce::KeyPress::spaceKey && engine.hasSample())
    {
        engine.play (! engine.isPlaying());
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
// them — then the preset, then UNDO, REDO and A/B/C/D (the owner, 05.11).
juce::Rectangle<int> MainComponent::brandRow() const   { return header(); }
juce::Rectangle<int> MainComponent::registerArea (int i) const
{
    const auto b = brandRow();
    return { b.getRight() - 10 - (4 - i) * 34 + 4, b.getCentreY() - 15, 30, 30 };
}
juce::Rectangle<int> MainComponent::redoArea() const   { return registerArea (0).translated (-46, 0); }
juce::Rectangle<int> MainComponent::undoArea() const   { return redoArea().translated (-36, 0); }
juce::Rectangle<int> MainComponent::prevArea() const
{
    return { brandLink().getRight() + 14, brandRow().getCentreY() - 15, 30, 30 };
}
juce::Rectangle<int> MainComponent::nextArea() const   { return prevArea().withX (undoArea().getX() - 18 - 30); }
juce::Rectangle<int> MainComponent::presetArea() const
{
    return { prevArea().getRight() + 6, prevArea().getY(), nextArea().getX() - 6 - prevArea().getRight() - 6, prevArea().getHeight() };
}
juce::Rectangle<int> MainComponent::brandLink() const
{
    const float h = (float) brandH;
    const float right = h * 2.0f + 18.0f + look().trackedWidth ("Cab IR Generator", look().font (h * nameShare), 0.0f) + 6.0f;
    return { 0, 0, juce::roundToInt (right), brandH };
}

// ---------------------------------------------------------------- the strip
juce::Rectangle<int> MainComponent::openArea() const    { return strip().reduced (8, 5).withWidth (70); }
juce::Rectangle<int> MainComponent::playArea() const    { return openArea().translated (78, 0).withWidth (70); }
juce::Rectangle<int> MainComponent::sampleArea() const  { return playArea().translated (78, 0).withWidth (300); }
juce::Rectangle<int> MainComponent::audioArea() const   { return versionArea().translated (-82, 0).withWidth (74); }

juce::Rectangle<int> MainComponent::hearArea (int i) const   // after the sample, a button a word
{
    int x = sampleArea().getRight() + 16;
    for (int k = 0; k < i; ++k)
        x += juce::roundToInt (look().trackedWidth (hearWords[k], look().font (13.0f), 0.06f)) + 22 + 4;
    return sampleArea().withX (x).withWidth (juce::roundToInt (look().trackedWidth (hearWords[i], look().font (13.0f), 0.06f)) + 22);
}

MainComponent::Hear MainComponent::hearing() const
{
    const bool parts = ! irA.empty() && ! irB.empty();
    return (hear == Hear::a || hear == Hear::b) && ! parts ? Hear::pair : hear;
}

void MainComponent::playHeard()
{
    const auto now = hearing();
    const auto& ir = now == Hear::a ? irA : now == Hear::b ? irB : irMix;
    if (! ir.empty())
        engine.setIr (ir, 48000.0);
    engine.setBypass (now == Hear::bypass);
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
        const float x = h * 2.0f + 18.0f, nameW = look().trackedWidth ("Cab IR Generator", name, 0.0f);
        g.setColour (brandHover ? juce::Colours::white : look().tx);
        look().drawTracked (g, "Cab IR Generator", { x, b.getY() + 5.0f, nameW + 4.0f, h * 0.5f }, name, 0.0f, juce::Justification::centredLeft);
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
        look().drawTracked (g, (presetName.isEmpty() ? juce::String ("PRESETS") : presetName.toUpperCase()) + (edited && presetName.isNotEmpty() ? " *" : ""),
                            pr.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
        look().drawTracked (g, juce::String::fromUTF8 ("\xe2\x96\xbe"), pr.reduced (10.0f, 0.0f), font, 0.0f, juce::Justification::centredRight);
        button (juce::String::fromUTF8 ("\xe2\x97\x80"), prevArea(), false);
        button (juce::String::fromUTF8 ("\xe2\x96\xb6"), nextArea(), false);
    }

    const auto s = strip();
    g.setColour (look().hair);
    g.fillRect (s.withHeight (1));

    button ("OPEN", openArea(), false);
    if (engine.hasSample())
        button (engine.isPlaying() ? "STOP" : "PLAY", playArea(), engine.isPlaying());
    button ("AUDIO", audioArea(), false);
    for (int i = 0; i < 4; ++i)   // HEAR: the one playing lit; A and B dim with one mic heard
        button (hearWords[i], hearArea (i), (int) hearing() == i, i == 0 || i == 3 || (! irA.empty() && ! irB.empty()));

    // The sample: a box like the preset's, its menu the riffs we carry and a file of the player's own.
    const auto sa = sampleArea().toFloat().reduced (0.5f);
    g.setColour (look().panel2);
    g.fillRoundedRectangle (sa, 4.0f);
    g.setColour (look().txFaint);
    g.drawRoundedRectangle (sa, 4.0f, 1.0f);
    g.setColour (engine.hasSample() ? look().tx : look().txFaint);
    look().drawTracked (g, engine.hasSample() ? engine.sampleName().toUpperCase() : juce::String ("DROP A BASS SAMPLE ON THE WINDOW"),
                        sa.reduced (10.0f, 0.0f), font, 0.06f, juce::Justification::centredLeft);
    look().drawTracked (g, juce::String::fromUTF8 ("\xe2\x96\xbe"), sa.reduced (10.0f, 0.0f), font, 0.0f, juce::Justification::centredRight);

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
    else if (openArea().contains (p))
        openSample();
    else if (sampleArea().contains (p))
        showSamples();
    else if (playArea().contains (p) && engine.hasSample())
    {
        engine.play (! engine.isPlaying());
        repaint (strip());
    }
    else if (audioArea().contains (p))
        showAudio();
    else if (const int h = [&] { for (int i = 0; i < 4; ++i) if (hearArea (i).contains (p)) return i; return -1; }(); h >= 0)
    {
        if ((h == 1 || h == 2) && (irA.empty() || irB.empty()))
            return;   // A or B with one mic heard: nothing to pick
        hear = (Hear) h;
        playHeard();
        repaint (strip());
    }
    else if (versionArea().contains (p))
        badge.showAbout();
}

} // namespace cabir::app
