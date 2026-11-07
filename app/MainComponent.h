// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include "Engine.h"

#include <felitronics/appkit/CompareHistory.h>
#include <felitronics/appkit/UpdateChecker.h>
#include <felitronics/appkit/VersionBadge.h>
#include <panel/CabPicker.h>
#include <panel/Download.h>
#include <synth/CabSynth.h>
#include <synth/State.h>

#include <juce_gui_extra/juce_gui_extra.h>

namespace cabir::app
{

/** THE GENERATOR'S WINDOW: a header — the mark and the name, UNDO and REDO, the preset (the factory's by style, the
    player's own, SAVE AS), and A/B/C/D, each with its own history; the cabinets' panel; and a strip — the sample (OPEN,
    a file dropped anywhere on the window, PLAY / STOP), AUDIO's settings, and the version, which opens the family's
    version window. What the panel sets is rendered on a worker, the latest asked winning — heard live while the hand
    moves, kept when it lets go — and the whole workspace, A/B/C/D with it, is kept between runs.
    IN A PLUGIN the same window, handed the plugin's Session: no sample, no PLAY, no AUDIO — the track plays through the
    cabinet — and the workspace kept in the session, so the host saves it with its project. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    /** The app's window, with no session: its own player and its settings' workspace. */
    explicit MainComponent (Session* session = nullptr);

    /** THE PANEL AS STILLS (--stills <folder>): drawn as the app draws it, with no window, no audio, no hand — the first
        run's state; FIND's map and the place it found; a pair that combs; one mic alone — and each box of the pile alone
        (boxes/box-<size>.png) — at `scale`, PNG. For the page (its narrow screens, its link image, its list of sizes)
        and for comparing the panel with itself. 0 when all were written. */
    static int writeStills (const juce::File& folder, float scale = 2.0f);

    ~MainComponent() override;

    static constexpr int brandH = 60, headerH = brandH, stripH = 34;
    static constexpr float nameShare = 0.36f;   // the name's type, a share of the header's height

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return engine != nullptr; }

    /** The app's own settings — the window keeps where it was and how large there. */
    juce::PropertiesFile& settings() { return *properties.getUserSettings(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    Session* const session;                   // a plugin's, else none
    juce::ApplicationProperties properties;
    felitronics::appkit::UpdateChecker checker;
    felitronics::appkit::VersionBadge badge;
    std::unique_ptr<Engine> engine;           // the app's player; none in a plugin
    Sound& sound() { return session != nullptr ? static_cast<Sound&> (*session) : *engine; }
    CabPicker picker;

    State state;                              // what the panel last kept: the live state history snapshots
    juce::MemoryBlock keptWav;                // ...rendered, for DOWNLOAD IR
    std::vector<float> keptCurve;             // ...its response: the ghost under the next one

    /** BYPASS, in the strip: A, B, A+B — a mic taken out of what the sample is played through, the other heard as it stands
        in the pair; both out, no cabinet. A+B lit while both are out, a click on it taking both out or, both out, putting
        both back; none pressed, the pair (the owner, 07.11). A and B only while two mics are heard. The player's alone:
        nothing of the preset, nothing DOWNLOAD IR writes (the owner, 05.11). */
    enum class Hear { pair, a, b, bypass };
    static constexpr const char* bypassWords[] { "A", "B", "A+B" };
    bool offA = false, offB = false;          // BYPASS A, BYPASS B
    std::vector<float> irMix, irA, irB;       // the last rendered: the pair and each mic's part of it (none with one mic)
    Hear hearing() const;                     // what plays: one mic out with one mic heard plays the pair
    void playHeard();

    juce::ThreadPool worker { 1 };
    synth::PairRenderer renderer;             // the worker's own: it keeps each mic's last IR
    std::atomic<int> latest { 0 };
    int heard = 0;                            // the latest render loaded (message thread)
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::unique_ptr<juce::FileChooser> chooser;

    felitronics::appkit::CompareHistory history;   // undo/redo and A/B/C/D over `state`, as its TOML
    std::unique_ptr<juce::Drawable> mark, cat;     // the logo's mark and the family's cat, in the header
    bool brandHover = false;                       // the hand over the brand: it links to the product's page
    juce::String presetName;                       // the preset last loaded or saved — dim once edited
    std::string presetText;                        // ...as its state's TOML, to tell
    int riffNow = -1;                              // the riff playing, by its place in the menu; -1 for a file of the player's own

    void render (const State&, bool live);
    static State defaultState();                   // the first run's, and the preset menu's DEFAULT
    void keep (const State&);                      // the panel's state, kept: what history and the next run see
    void carryFormerName();                        // a first run under the new name: the old one's settings and presets copied
    void loadWorkspace (const juce::String& xml);  // A/B/C/D as kept, else the default alone
    void keepSession();                            // ...and, in a plugin, the workspace handed to its session
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the host handed the session a project; the device changed
    void findPlace (const State&, bool thorough = false);   // B where it combs least with A, or one mic where it has no pit
    void show (const State&);                      // ...and one put on the panel from elsewhere (undo, A/B/C/D, a preset)
    void showPresets();
    void savePreset();
    void loadPreset (const juce::String& toml, const juce::String& name);
    static juce::File presetFolder();
    static std::vector<std::pair<juce::String, juce::String>> allPresets();   // toml, name — in the menu's order
    void stepPreset (int by);                                                 // the one before or after, round the end
    void openSample();
    void loadSample (const juce::File&);
    void loadRiff (int);                           // one of the riffs the app carries, by its place in the menu
    void useInput();                               // the live input instead, silent until played
    void showSamples();
    void showAudio();
    void download();
    /** DOWNLOAD IR's format (the owner, 06.11): rate, bits, and the length a loader takes — 0, the whole IR — kept between
        runs; the panel's ▾ chooses, the line under the button says it. */
    DownloadFormat format;                         // panel/Download.h
    void showFormats();
    void formatChosen();
    void noteDownload();                           // the line under DOWNLOAD IR, the whole IR as long as the cabinet's
    synth::WavMeta metaOf (const State&) const;   // what a WAV of `s` says of itself
    void timerCallback() override;

    juce::Rectangle<int> header() const { return getLocalBounds().removeFromTop (headerH); }
    juce::Rectangle<int> brandRow() const;
    juce::Rectangle<int> brandLink() const;
    juce::Rectangle<int> undoArea() const;
    juce::Rectangle<int> redoArea() const;
    juce::Rectangle<int> presetArea() const;
    juce::Rectangle<int> prevArea() const;
    juce::Rectangle<int> nextArea() const;
    juce::Rectangle<int> registerArea (int) const;
    juce::Rectangle<int> strip() const { return getLocalBounds().removeFromBottom (stripH); }
    juce::Rectangle<int> openArea() const;
    juce::Rectangle<int> playArea() const;
    juce::Rectangle<int> sampleArea() const;
    juce::Rectangle<int> bypassWordArea() const;
    juce::Rectangle<int> hearArea (int) const;     // BYPASS's A, B, A+B
    juce::Rectangle<int> audioArea() const;
    juce::Rectangle<int> versionArea() const;
};

} // namespace cabir::app
