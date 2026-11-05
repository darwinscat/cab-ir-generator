// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#include "Look.h"

namespace cabir
{

Look& look()
{
    static Look one;
    return one;
}

namespace
{
    // `text` on a baseline at y = 0; its advance, tracking included — so the measure and the draw agree.
    float layOut (juce::GlyphArrangement& glyphs, const juce::String& text, const juce::Font& font, float trackingEm)
    {
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        const int n = glyphs.getNumGlyphs();
        if (n == 0)
            return 0.0f;
        return glyphs.getGlyph (n - 1).getRight() - glyphs.getGlyph (0).getLeft() + trackingEm * font.getHeight() * (float) (n - 1);
    }
}

float Look::trackedWidthPlain (const juce::String& text, const juce::Font& font, float trackingEm)
{
    juce::GlyphArrangement glyphs;
    return layOut (glyphs, text, font, trackingEm);
}

void Look::drawTrackedPlain (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                             float trackingEm, juce::Justification justification)
{
    juce::GlyphArrangement glyphs;
    const float total = layOut (glyphs, text, font, trackingEm);
    const int n = glyphs.getNumGlyphs();
    if (n == 0)
        return;

    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x += (area.getWidth() - total) * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x += area.getWidth() - total;

    float y = area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;
    if (justification.testFlags (juce::Justification::top))
        y = area.getY() + font.getAscent();
    else if (justification.testFlags (juce::Justification::bottom))
        y = area.getBottom() - font.getDescent();

    const float step = trackingEm * font.getHeight(), x0 = glyphs.getGlyph (0).getLeft();
    for (int i = 0; i < n; ++i)
        glyphs.getGlyph (i).draw (g, juce::AffineTransform::translation (x - x0 + step * (float) i, y));
}

} // namespace cabir
