// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "MicSvgs.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

namespace cabir
{

/** THE MICS' GLYPHS as JUCE draws them: the SVGs of MicSvgs.h parsed once each, for a caller to recolour (tintGlyph).
    Message thread. */
inline const juce::Drawable* micGlyph (const juce::String& name)
{
    static std::map<juce::String, std::unique_ptr<juce::Drawable>> parsed;

    if (auto it = parsed.find (name); it != parsed.end())
        return it->second.get();

    const auto* svg = micSvg (name.toStdString());
    if (svg == nullptr)
        return nullptr;

    std::unique_ptr<juce::Drawable> d;
    if (auto xml = juce::parseXML (svg))
        d = juce::Drawable::createFromSVG (*xml);
    return (parsed[name] = std::move (d)).get();
}

/** A glyph in `colour`: its black, at whatever opacity each part has, turned to the colour at that opacity — its white
    highlights left as they are. */
inline void tintGlyph (juce::Drawable& d, juce::Colour colour)
{
    const auto tint = [colour] (juce::FillType f)
    {
        if (f.isColour() && (f.colour.getARGB() & 0x00ffffffu) == 0)
            f.setColour (colour.withMultipliedAlpha (f.colour.getFloatAlpha()));
        return f;
    };
    if (auto* shape = dynamic_cast<juce::DrawableShape*> (&d))
    {
        shape->setFill (tint (shape->getFill()));
        shape->setStrokeFill (tint (shape->getStrokeFill()));
    }
    for (auto* child : d.getChildren())
        if (auto* inner = dynamic_cast<juce::Drawable*> (child))
            tintGlyph (*inner, colour);
}

} // namespace cabir
