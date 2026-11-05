// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "Engine.h"

#include <felitronics/appkit/UpdateChecker.h>
#include <felitronics/appkit/VersionBadge.h>
#include <panel/CabPicker.h>
#include <synth/State.h>

#include <juce_gui_extra/juce_gui_extra.h>

namespace cabir::app
{

/** THE GENERATOR'S WINDOW: the cabinets' panel over a strip — the sample (OPEN, a file dropped anywhere on the window,
    PLAY / STOP), AUDIO's settings, and at the right the version, which opens the family's version window. What the
    panel sets is rendered on a worker, the latest asked winning — heard live while the hand moves, kept when it lets
    go — and kept between runs as the state's TOML. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    static constexpr int stripH = 34;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::ApplicationProperties properties;
    felitronics::appkit::UpdateChecker checker;
    felitronics::appkit::VersionBadge badge;
    Engine engine;
    CabPicker picker;

    State state;                              // what the panel last kept
    juce::MemoryBlock keptWav;                // ...rendered, for DOWNLOAD IR
    std::vector<float> keptCurve;             // ...its response: the ghost under the next one

    juce::ThreadPool worker { 1 };
    std::atomic<int> latest { 0 };
    int heard = 0;                            // the latest render loaded (message thread)
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::unique_ptr<juce::FileChooser> chooser;

    void render (const State&, bool live);
    void openSample();
    void loadSample (const juce::File&);
    void showAudio();
    void download();
    void timerCallback() override;

    juce::Rectangle<int> strip() const { return getLocalBounds().removeFromBottom (stripH); }
    juce::Rectangle<int> openArea() const;
    juce::Rectangle<int> playArea() const;
    juce::Rectangle<int> audioArea() const;
    juce::Rectangle<int> versionArea() const;
};

} // namespace cabir::app
