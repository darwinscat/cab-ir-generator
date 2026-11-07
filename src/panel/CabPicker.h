// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include "Look.h"
#include "MicGlyphs.h"
#include "Panel.h"
#include "SvgGraphics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace cabir
{

/** THE PANEL IN A JUCE HOST: the bass's cabinets as Panel.h draws them, onto juce::Graphics in the host's Look, the
    hand's moves handed in and the time ticked. The panel itself — the pile, the speaker and its mics, the response — is
    JUCE-free (Panel), so a page draws the same one; this is the host's half, and what a host calls. */
class CabPicker final : public juce::Component,
                        private juce::Timer
{
public:
    using Spot = Panel::Spot;
    using Setup = Panel::Setup;
    using Cab = Panel::Cab;

    static constexpr int panelW = Panel::panelW, panelH = Panel::panelH, footRowH = Panel::footRowH;
    static constexpr int responsePoints = Panel::responsePoints;
    static float responseHz (int i) { return Panel::responseHz (i); }

    CabPicker()
    {
        panel.onRepaint = [this] { repaint(); };
        panel.onRepaintArea = [this] (ui::Rectangle<int> r) { repaint (r.getX(), r.getY(), r.getWidth(), r.getHeight()); };
        startTimerHz (30);   // DOWNLOAD IR's pulse, the search map's fade, DEFAULT's question
    }

    // What a host sets and is told — the panel's own, by reference.
    std::function<void (int cab, Setup setup)>& onSetup { panel.onSetup };
    std::function<void (int cab, Setup setup)>& onMove { panel.onMove };
    std::function<void()>& onClose { panel.onClose };
    std::function<void()>& onDownload { panel.onDownload };
    std::function<void()>& onDownloadFormat { panel.onDownloadFormat };   // its ▾: the host's format menu
    void setDownloadNote (const juce::String& note) { panel.downloadNote = ui::String (note.toStdString()); repaint(); }
    std::function<void (int cab, Setup setup)>& onFind { panel.onFind };
    bool& showDefault { panel.showDefault };

    void show (int cab, Setup setup) { panel.show (cab, setup); }
    void showSearch (float x, float y, float score) { panel.showSearch (x, y, score); }
    void endSearch() { panel.endSearch(); }
    void showResponse (std::vector<float> now, std::vector<float> before, std::vector<float> comb = {})
    {
        panel.showResponse (std::move (now), std::move (before), std::move (comb));
    }

    static const std::vector<Cab>& cabs() { return Panel::cabs(); }
    static std::pair<int, Setup> setupOf (const State& s) { return Panel::setupOf (s); }
    static State stateOf (int cab, const Setup& su) { return Panel::stateOf (cab, su); }
    static juce::String wordOf (int cab) { return juce::String::fromUTF8 (Panel::wordOf (cab).toRawUTF8()); }
    static juce::String labelOf (const State& s) { return juce::String::fromUTF8 (Panel::labelOf (s).toRawUTF8()); }

    /** The cabinet that plays, small, in `area` — a host's cabinet block draws it over its IR (Panel::paintSketch). */
    static void paintSketch (juce::Graphics& g, int cab, const Setup& setup, juce::Rectangle<float> area)
    {
        useHostLook();
        Drawn drawn (g);
        Panel::paintSketch (drawn, cab, setup, { area.getX(), area.getY(), area.getWidth(), area.getHeight() });
    }

    /** One box alone in `box`, as the pile draws it unchosen: its speakers, its own back's port, its horn — no mics. */
    static void paintBox (juce::Graphics& g, int cab, juce::Rectangle<float> box)
    {
        useHostLook();
        Drawn drawn (g);
        Panel::paintCab (drawn, cabs()[(size_t) cab], { box.getX(), box.getY(), box.getWidth(), box.getHeight() }, {}, false, false);
    }

    /** ...and the same box as SVG, `width` x `height` with the box at `box` in it (ui::SvgGraphics). */
    static std::string boxSvg (int cab, float width, float height, juce::Rectangle<float> box)
    {
        useHostLook();
        ui::SvgGraphics svg (width, height);
        Panel::paintCab (svg, cabs()[(size_t) cab], { box.getX(), box.getY(), box.getWidth(), box.getHeight() }, {}, false, false);
        return svg.text();
    }

    void paint (juce::Graphics& g) override
    {
        useHostLook();
        Drawn drawn (g);
        panel.paint (drawn);
    }

    void resized() override { panel.setSize (getWidth(), getHeight()); }

    void mouseMove (const juce::MouseEvent& e) override        { panel.mouseMove (hand (e)); }
    void mouseExit (const juce::MouseEvent& e) override        { panel.mouseExit (hand (e)); }
    void mouseDown (const juce::MouseEvent& e) override        { panel.mouseDown (hand (e)); }
    void mouseDrag (const juce::MouseEvent& e) override        { panel.mouseDrag (hand (e)); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { panel.mouseDoubleClick (hand (e)); }
    void mouseUp (const juce::MouseEvent& e) override          { panel.mouseUp (hand (e)); }

private:
    Panel panel;

    void timerCallback() override { panel.tick (juce::Time::getMillisecondCounterHiRes() * 0.001); }

    static ui::MouseEvent hand (const juce::MouseEvent& e) { return { { e.position.x, e.position.y } }; }

    /** The host's Look as the panel's: its colours, and its type to measure words by. */
    static void useHostLook()
    {
        auto& to = ui::look();
        const auto& from = look();
        const auto c = [] (juce::Colour colour) { return ui::Colour (colour.getARGB()); };
        to.orange = c (from.orange);
        to.violet = c (from.violet);
        to.lilac = c (from.lilac);
        to.panel = c (from.panel);
        to.panel2 = c (from.panel2);
        to.bezel = c (from.bezel);
        to.hair = c (from.hair);
        to.hair2 = c (from.hair2);
        to.tx = c (from.tx);
        to.txDim = c (from.txDim);
        to.txFaint = c (from.txFaint);
        to.measure = [] (const ui::String& text, float height, float trackingEm)
        { return look().trackedWidth (juce::String::fromUTF8 (text.toRawUTF8()), look().font (height), trackingEm); };
        to.ascent = [] (float height) { return look().font (height).getAscent(); };
    }

    /** The panel's calls onto juce::Graphics, one for one. */
    class Drawn final : public ui::Graphics
    {
    public:
        explicit Drawn (juce::Graphics& graphics) : g (graphics) {}

        void setColour (ui::Colour c) override { g.setColour (juce::Colour (c.getARGB())); }
        void setGradientFill (const ui::ColourGradient& f) override
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (f.colour1.getARGB()), f.x1, f.y1,
                                                     juce::Colour (f.colour2.getARGB()), f.x2, f.y2, f.radial));
        }
        void fillRect (ui::Rectangle<float> r) override { g.fillRect (rect (r)); }
        void fillRoundedRectangle (ui::Rectangle<float> r, float corner) override { g.fillRoundedRectangle (rect (r), corner); }
        void drawRoundedRectangle (ui::Rectangle<float> r, float corner, float line) override { g.drawRoundedRectangle (rect (r), corner, line); }
        void drawLine (float x1, float y1, float x2, float y2, float line) override { g.drawLine (x1, y1, x2, y2, line); }
        void fillEllipse (ui::Rectangle<float> r) override { g.fillEllipse (rect (r)); }
        void drawEllipse (ui::Rectangle<float> r, float line) override { g.drawEllipse (rect (r), line); }
        void strokePath (const ui::Path& p, const ui::PathStrokeType& s) override
        {
            juce::Path path;
            for (const auto& step : p.getSteps())
            {
                if (step.op == ui::Path::Op::move)
                    path.startNewSubPath (step.a, step.b);
                else if (step.op == ui::Path::Op::line)
                    path.lineTo (step.a, step.b);
                else
                    path.addCentredArc (step.a, step.b, step.c, step.d, step.e, step.f, step.g, step.newSubPath);
            }
            const auto joint = s.joint == ui::PathStrokeType::curved ? juce::PathStrokeType::curved
                             : s.joint == ui::PathStrokeType::beveled ? juce::PathStrokeType::beveled : juce::PathStrokeType::mitered;
            const auto end = s.end == ui::PathStrokeType::rounded ? juce::PathStrokeType::rounded
                           : s.end == ui::PathStrokeType::square ? juce::PathStrokeType::square : juce::PathStrokeType::butt;
            g.strokePath (path, juce::PathStrokeType (s.thickness, joint, end));
        }
        void reduceClipRegion (ui::Rectangle<int> r) override { g.reduceClipRegion (r.getX(), r.getY(), r.getWidth(), r.getHeight()); }
        void saveState() override { g.saveState(); }
        void restoreState() override { g.restoreState(); }

        void drawTracked (const ui::String& text, ui::Rectangle<float> area, const ui::Font& font, float trackingEm,
                          ui::Justification j) override
        {
            look().drawTracked (g, juce::String::fromUTF8 (text.toRawUTF8()), rect (area), look().font (font.height), trackingEm,
                                juce::Justification (j.getFlags()));
        }

        void drawGlyph (const ui::String& mic, ui::Rectangle<float> box, ui::Colour colour, float lean, ui::Point<float> pivot) override
        {
            if (const auto* d = micGlyph (juce::String::fromUTF8 (mic.toRawUTF8())))
            {
                auto copy = d->createCopy();
                tintGlyph (*copy, juce::Colour (colour.getARGB()));
                juce::Graphics::ScopedSaveState keep (g);
                g.addTransform (juce::AffineTransform::rotation (lean, pivot.x, pivot.y));
                copy->drawWithin (g, rect (box), juce::RectanglePlacement::centred, 1.0f);
            }
        }

    private:
        juce::Graphics& g;
        static juce::Rectangle<float> rect (ui::Rectangle<float> r) { return { r.getX(), r.getY(), r.getWidth(), r.getHeight() }; }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPicker)
};

} // namespace cabir
