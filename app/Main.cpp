// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "MainComponent.h"

namespace cabir::app
{

class Window final : public juce::DocumentWindow
{
public:
    Window()
        : DocumentWindow ("Cab IR Generator", look().panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Cab IR Generator"; }
    const juce::String getApplicationVersion() override { return CABIR_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override                       { window.reset(); }
    void systemRequestedQuit() override            { quit(); }

private:
    std::unique_ptr<Window> window;
};

} // namespace cabir::app

START_JUCE_APPLICATION (cabir::app::Application)
