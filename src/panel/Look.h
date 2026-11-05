// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace cabir
{

/** HOW THE PANEL LOOKS: its colours, its display type and how a word is laid out tracked — the family's own by
    default (Darwin's Cat's violet, lilac and orange on a dark panel), and a host's where it sets its own at start
    (look()), so the panel in a plugin is drawn in that plugin's theme. Message thread. */
struct Look
{
    juce::Colour orange  { 0xffff8a3d };   // the hot accent: mic A, what is chosen
    juce::Colour violet  { 0xff9778ff };   // the lines
    juce::Colour lilac   { 0xffb9a6ff };   // mic B
    juce::Colour panel   { 0xff111119 };
    juce::Colour panel2  { 0xff15151f };
    juce::Colour bezel   { 0xff050509 };   // recessed wells
    juce::Colour hair    = violet.withAlpha (0.12f);
    juce::Colour hair2   = violet.withAlpha (0.24f);
    juce::Colour tx      { 0xffece9f6 };
    juce::Colour txDim   { 0xffa6a2bd };
    juce::Colour txFaint { 0xff6f6b86 };

    /** The display face at a height; a plain sans until a host gives its own. */
    std::function<juce::Font (float height)> font = [] (float height) { return juce::Font (juce::FontOptions (height)); };

    /** `text` drawn with per-character tracking (in em) in `area`, as `justification` places it; and the width it
        takes. Laid out a glyph at a time until a host gives its own. */
    std::function<void (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, float, juce::Justification)>
        drawTracked = drawTrackedPlain;
    std::function<float (const juce::String&, const juce::Font&, float)> trackedWidth = trackedWidthPlain;

    static void drawTrackedPlain (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, const juce::Font&,
                                  float trackingEm, juce::Justification);
    static float trackedWidthPlain (const juce::String& text, const juce::Font&, float trackingEm);
};

/** The look the panel draws with — one for the whole process. */
Look& look();

} // namespace cabir
