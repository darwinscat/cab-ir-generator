// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <panel/Look.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

namespace cabir::app
{

/** An input's meter: a bar from -60 dB to full scale, violet as the panel's lines, orange from -3 dB, where a hotter
    string would clip. */
inline void drawInputMeter (juce::Graphics& g, juce::Rectangle<float> r, float level)
{
    g.setColour (look().bezel);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    const float db = level > 0.0f ? 20.0f * std::log10 (level) : -100.0f;
    const float share = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (share <= 0.0f)
        return;
    g.setColour (db >= -3.0f ? look().orange : look().violet);
    g.fillRoundedRectangle (r.withWidth (std::max (r.getHeight(), r.getWidth() * share)), r.getHeight() * 0.5f);
}

/** THE INPUTS (the owner, 15.11): IN 1 to IN n of the open input device, each with its meter, the one heard lit; a click
    plays another, and the list stays open, so a plucked string shows where it is plugged in. Eight to a column. */
class InputList final : public juce::Component,
                        private juce::Timer
{
public:
    static constexpr int rowH = 28, colW = 156, top = 38, pad = 10, perColumn = 8;

    InputList (juce::String deviceName, int inputs, std::function<int()> heardNow, std::function<float (int)> levelOf,
               std::function<void (int)> picked)
        : device (std::move (deviceName)), count (std::max (1, inputs)), heard (std::move (heardNow)),
          level (std::move (levelOf)), onPicked (std::move (picked))
    {
        columns = (count + perColumn - 1) / perColumn;
        rows = (count + columns - 1) / columns;
        setSize (pad * 2 + columns * colW, top + rows * rowH + pad);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (look().panel);
        const auto font = look().font (13.0f);
        g.setColour (look().txFaint);
        look().drawTracked (g, device.toUpperCase(), getLocalBounds().removeFromTop (top).reduced (pad + 8, 0).toFloat(),
                            font, 0.06f, juce::Justification::centredLeft);
        const int now = heard();
        for (int i = 0; i < count; ++i)
        {
            const auto cell = cellArea (i).toFloat();
            const bool on = i == now;
            if (on)
            {
                g.setColour (look().panel2);
                g.fillRoundedRectangle (cell.reduced (2.0f), 4.0f);
                g.setColour (look().orange);
                g.drawRoundedRectangle (cell.reduced (2.0f), 4.0f, 1.0f);
            }
            g.setColour (on ? look().orange : look().txDim);
            look().drawTracked (g, "IN " + juce::String (i + 1), cell.withWidth (54.0f).withTrimmedLeft (10.0f), font, 0.06f,
                                juce::Justification::centredLeft);
            drawInputMeter (g, cell.withTrimmedLeft (58.0f).withTrimmedRight (12.0f).withSizeKeepingCentre (cell.getWidth() - 70.0f, 6.0f),
                            level (i));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < count; ++i)
            if (cellArea (i).contains (e.getPosition()))
            {
                onPicked (i);
                repaint();
            }
    }

private:
    juce::String device;
    int count, columns = 1, rows = 1;
    std::function<int()> heard;
    std::function<float (int)> level;
    std::function<void (int)> onPicked;

    juce::Rectangle<int> cellArea (int i) const   // down a column, then the next
    {
        return { pad + (i / rows) * colW, top + (i % rows) * rowH, colW, rowH };
    }

    void timerCallback() override { repaint(); }
};

} // namespace cabir::app
