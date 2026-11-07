// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "Flavour.h"
#include "MainComponent.h"

#if CABIR_GUITAR
 #include <panel/GuitarCatalog.h>
#endif

namespace cabir::app
{

/** THE WINDOW'S CONTENT, SCALED: the generator laid out at its one size, drawn as large as the window lets it — the same
    shape, a little smaller or several times larger, with a margin either side where the window is of another shape (a
    full screen). */
class Scaler final : public juce::Component
{
public:
    Scaler()
    {
        addAndMakeVisible (main);
        setSize (main.getWidth(), main.getHeight());
    }

    void paint (juce::Graphics& g) override { g.fillAll (look().panel); }

    void resized() override
    {
        const float s = juce::jmin ((float) getWidth() / (float) main.getWidth(), (float) getHeight() / (float) main.getHeight());
        const float x = 0.5f * ((float) getWidth() - (float) main.getWidth() * s), y = 0.5f * ((float) getHeight() - (float) main.getHeight() * s);
        main.setTransform (juce::AffineTransform::scale (s).translated (x, y));
    }

    MainComponent main;
};

class Window final : public juce::DocumentWindow
{
public:
    Window()
        : DocumentWindow (flavour::product, look().panel,
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton | DocumentWindow::maximiseButton)
    {
        setUsingNativeTitleBar (true);
        auto* scaler = new Scaler();
        setContentOwned (scaler, true);
        const int w = scaler->getWidth(), h = scaler->getHeight();
        // Drawn from 60 % to three times its size, its shape kept as the hand drags a corner (the owner, 05.11).
        setResizable (true, true);
        setResizeLimits (w * 6 / 10, h * 6 / 10, w * 3, h * 3);
        getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
        const auto kept = scaler->main.settings().getValue ("window");
        if (kept.isEmpty() || ! restoreWindowStateFromString (kept))
            centreWithSize (w, h);
        setVisible (true);
    }

    ~Window() override
    {
        if (auto* scaler = dynamic_cast<Scaler*> (getContentComponent()))
            scaler->main.settings().setValue ("window", getWindowStateAsString());   // where it was and how large
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return flavour::product; }
    const juce::String getApplicationVersion() override { return CABIR_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameterArray().contains ("--stills"); }

    void initialise (const juce::String&) override
    {
#if CABIR_GUITAR
        Panel::useCatalog (guitarCatalog());   // before the first panel: the guitar's boxes and mics
#endif
        // --stills <folder>: the panel drawn to files, and out — no window, no audio (MainComponent::writeStills).
        if (const auto args = getCommandLineParameterArray(); args.contains ("--stills"))
        {
            const int at = args.indexOf ("--stills");
            const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1].isNotEmpty() ? args[at + 1] : "stills");
            setApplicationReturnValue (MainComponent::writeStills (folder));
            quit();
            return;
        }
        window = std::make_unique<Window>();
    }
    void shutdown() override                       { window.reset(); }
    void systemRequestedQuit() override            { quit(); }

private:
    std::unique_ptr<Window> window;
};

} // namespace cabir::app

START_JUCE_APPLICATION (cabir::app::Application)
