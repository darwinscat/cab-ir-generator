// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

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

/** THE WINDOW'S SHAPE AS ITS CONTENT KEEPS IT: the content's own aspect, the frame's rows on top of it (a native title
    bar, counted once the window is shown) — a plain fixed aspect holds the whole window to it, title bar and all, and the
    content, a bar's height short, stands with a margin either side (the owner, 07.11). */
class ContentShape final : public juce::ComponentBoundsConstrainer
{
public:
    ContentShape (int width, int height) : w (width), h (height) {}
    int frame = 0;   // the frame's rows above and below the content (a native title bar), the window's peer says

    void checkBounds (juce::Rectangle<int>& b, const juce::Rectangle<int>& old, const juce::Rectangle<int>& limits,
                      bool top, bool left, bool bottom, bool right) override
    {
        juce::ComponentBoundsConstrainer::checkBounds (b, old, limits, top, left, bottom, right);
        if ((top || bottom) && ! (left || right))   // the hand on an edge above or below: the width follows
        {
            const int nw = juce::roundToInt ((b.getHeight() - frame) * (double) w / h);
            if (left) b.setLeft (b.getRight() - nw); else b.setWidth (nw);
        }
        else   // else the height follows the width
        {
            const int nh = juce::roundToInt (b.getWidth() * (double) h / w) + frame;
            if (top) b.setTop (b.getBottom() - nh); else b.setHeight (nh);
        }
    }

private:
    int w, h;
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
        // Drawn from 60 % to three times its size, its shape kept as the hand drags an edge (the owner, 05.11) — the system's
        // edges, no corner of JUCE's: the system hands the shape its frame, title bar and all, the corner would its content.
        setResizable (true, false);
        shape = std::make_unique<ContentShape> (w, h);
        shape->setSizeLimits (w * 6 / 10, h * 6 / 10, w * 3, h * 3 + 64);
        setConstrainer (shape.get());
        const auto kept = scaler->main.settings().getValue ("window");
        if (kept.isEmpty() || ! restoreWindowStateFromString (kept))
            centreWithSize (w, h);
        setVisible (true);
        // Once shown the frame says its title bar: the window fitted to its content's shape, its width kept — no margin
        // either side, whatever shape a kept window had (an older build's).
        if (auto* peer = getPeer())
            shape->frame = peer->getFrameSize().getTopAndBottom();
        setSize (getWidth(), juce::roundToInt (getWidth() * (double) h / w));
    }

    ~Window() override
    {
        if (auto* scaler = dynamic_cast<Scaler*> (getContentComponent()))
            scaler->main.settings().setValue ("window", getWindowStateAsString());   // where it was and how large
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    std::unique_ptr<ContentShape> shape;
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
