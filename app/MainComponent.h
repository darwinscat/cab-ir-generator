// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "Engine.h"

#include <felitronics/appkit/CompareHistory.h>
#include <felitronics/appkit/UpdateChecker.h>
#include <felitronics/appkit/VersionBadge.h>
#include <panel/CabPicker.h>
#include <synth/CabSynth.h>
#include <synth/State.h>

#include <juce_gui_extra/juce_gui_extra.h>

namespace cabir::app
{

/** THE GENERATOR'S WINDOW: a header — the mark and the name, UNDO and REDO, the preset (the factory's by style, the
    player's own, SAVE AS), and A/B/C/D, each with its own history; the cabinets' panel; and a strip — the sample (OPEN,
    a file dropped anywhere on the window, PLAY / STOP), AUDIO's settings, and the version, which opens the family's
    version window. What the panel sets is rendered on a worker, the latest asked winning — heard live while the hand
    moves, kept when it lets go — and the whole workspace, A/B/C/D with it, is kept between runs. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    static constexpr int headerH = 38, stripH = 34;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::ApplicationProperties properties;
    felitronics::appkit::UpdateChecker checker;
    felitronics::appkit::VersionBadge badge;
    Engine engine;
    CabPicker picker;

    State state;                              // what the panel last kept: the live state history snapshots
    juce::MemoryBlock keptWav;                // ...rendered, for DOWNLOAD IR
    std::vector<float> keptCurve;             // ...its response: the ghost under the next one

    juce::ThreadPool worker { 1 };
    synth::PairRenderer renderer;             // the worker's own: it keeps each mic's last IR
    std::atomic<int> latest { 0 };
    int heard = 0;                            // the latest render loaded (message thread)
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::unique_ptr<juce::FileChooser> chooser;

    felitronics::appkit::CompareHistory history;   // undo/redo and A/B/C/D over `state`, as its TOML
    std::unique_ptr<juce::Drawable> mark;          // the logo's mark, in the header
    juce::String presetName;                       // the preset last loaded or saved — dim once edited
    std::string presetText;                        // ...as its state's TOML, to tell

    void render (const State&, bool live);
    void keep (const State&);                      // the panel's state, kept: what history and the next run see
    void placeB (const State&, bool thorough = false);   // B moved where it combs least with A — FIND looks harder
    void show (const State&);                      // ...and one put on the panel from elsewhere (undo, A/B/C/D, a preset)
    void showPresets();
    void savePreset();
    void loadPreset (const juce::String& toml, const juce::String& name);
    static juce::File presetFolder();
    static std::vector<std::pair<juce::String, juce::String>> allPresets();   // toml, name — in the menu's order
    void stepPreset (int by);                                                 // the one before or after, round the end
    void openSample();
    void loadSample (const juce::File&);
    void showAudio();
    void download();
    void timerCallback() override;

    juce::Rectangle<int> header() const { return getLocalBounds().removeFromTop (headerH); }
    juce::Rectangle<int> undoArea() const;
    juce::Rectangle<int> redoArea() const;
    juce::Rectangle<int> presetArea() const;
    juce::Rectangle<int> prevArea() const;
    juce::Rectangle<int> nextArea() const;
    juce::Rectangle<int> registerArea (int) const;
    juce::Rectangle<int> strip() const { return getLocalBounds().removeFromBottom (stripH); }
    juce::Rectangle<int> openArea() const;
    juce::Rectangle<int> playArea() const;
    juce::Rectangle<int> audioArea() const;
    juce::Rectangle<int> versionArea() const;
};

} // namespace cabir::app
