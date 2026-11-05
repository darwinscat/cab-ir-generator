// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

#include "ui/Ui.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cabir::ui
{

/** THE PANEL'S CALLS WRITTEN AS SVG — a surface like any host's, its shapes kept as the page's own vector: rectangles,
    rounded ones, ellipses, lines and paths (JUCE's addCentredArc as SVG arcs), plain fills and gradients, clipping in
    nested groups. Words and mics' glyphs are not written: what it is drawn for (a box alone, --stills) has none.
    Self-contained — nothing outside the file — and the same calls give the same bytes. */
class SvgGraphics final : public Graphics
{
public:
    SvgGraphics (float width, float height) : w (width), h (height) {}

    /** The document, every group closed. */
    std::string text() const
    {
        std::string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num (w) + "\" height=\"" + num (h)
                        + "\" viewBox=\"0 0 " + num (w) + " " + num (h) + "\">\n";
        if (! defs.empty())
            out += "<defs>\n" + defs + "</defs>\n";
        out += body;
        for (int i = 0; i < depth; ++i)
            out += "</g>\n";
        return out + "</svg>\n";
    }

    void setColour (Colour c) override { gradient = -1; colour = c; }

    void setGradientFill (const ColourGradient& gr) override
    {
        gradient = gradients++;
        const auto id = "g" + std::to_string (gradient);
        const auto stop = [] (const char* at, Colour c)
        { return "<stop offset=\"" + std::string (at) + "\" stop-color=\"" + hex (c) + "\"" + opacity ("stop-opacity", c) + "/>"; };
        if (gr.radial)
            defs += "<radialGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" cx=\"" + num (gr.x1) + "\" cy=\"" + num (gr.y1)
                  + "\" r=\"" + num (std::hypot (gr.x2 - gr.x1, gr.y2 - gr.y1)) + "\">" + stop ("0", gr.colour1) + stop ("1", gr.colour2)
                  + "</radialGradient>\n";
        else
            defs += "<linearGradient id=\"" + id + "\" gradientUnits=\"userSpaceOnUse\" x1=\"" + num (gr.x1) + "\" y1=\"" + num (gr.y1)
                  + "\" x2=\"" + num (gr.x2) + "\" y2=\"" + num (gr.y2) + "\">" + stop ("0", gr.colour1) + stop ("1", gr.colour2)
                  + "</linearGradient>\n";
    }

    void fillRect (Rectangle<float> r) override
    {
        body += "<rect x=\"" + num (r.getX()) + "\" y=\"" + num (r.getY()) + "\" width=\"" + num (r.getWidth()) + "\" height=\""
              + num (r.getHeight()) + "\"" + fill() + "/>\n";
    }

    void fillRoundedRectangle (Rectangle<float> r, float corner) override { body += rounded (r, corner) + fill() + "/>\n"; }

    void drawRoundedRectangle (Rectangle<float> r, float corner, float line) override
    {
        body += rounded (r, corner) + stroke (line) + "/>\n";
    }

    void drawLine (float x1, float y1, float x2, float y2, float line) override
    {
        body += "<line x1=\"" + num (x1) + "\" y1=\"" + num (y1) + "\" x2=\"" + num (x2) + "\" y2=\"" + num (y2) + "\"" + stroke (line) + "/>\n";
    }

    void fillEllipse (Rectangle<float> r) override { body += ellipse (r) + fill() + "/>\n"; }
    void drawEllipse (Rectangle<float> r, float line) override { body += ellipse (r) + stroke (line) + "/>\n"; }

    void strokePath (const Path& p, const PathStrokeType& type) override
    {
        std::string d;
        for (const auto& s : p.getSteps())
        {
            if (s.op == Path::Op::move)
                d += "M" + num (s.a) + " " + num (s.b);
            else if (s.op == Path::Op::line)
                d += (d.empty() ? "M" : "L") + num (s.a) + " " + num (s.b);
            else
                d += arc (s, d.empty());
        }
        const char* cap = type.end == PathStrokeType::rounded ? "round" : type.end == PathStrokeType::square ? "square" : "butt";
        const char* join = type.joint == PathStrokeType::curved ? "round" : type.joint == PathStrokeType::beveled ? "bevel" : "miter";
        body += "<path d=\"" + d + "\"" + stroke (type.thickness) + " stroke-linecap=\"" + cap + "\" stroke-linejoin=\"" + join + "\"/>\n";
    }

    void reduceClipRegion (Rectangle<int> r) override
    {
        const auto id = "c" + std::to_string (clips++);
        defs += "<clipPath id=\"" + id + "\"><rect x=\"" + std::to_string (r.getX()) + "\" y=\"" + std::to_string (r.getY()) + "\" width=\""
              + std::to_string (r.getWidth()) + "\" height=\"" + std::to_string (r.getHeight()) + "\"/></clipPath>\n";
        body += "<g clip-path=\"url(#" + id + ")\">\n";
        ++depth;
        if (! saved.empty())
            ++saved.back().groups;
    }

    void saveState() override { saved.push_back ({ colour, gradient, 0 }); }

    void restoreState() override
    {
        if (saved.empty())
            return;
        for (int i = 0; i < saved.back().groups; ++i)
            body += "</g>\n";
        depth -= saved.back().groups;
        colour = saved.back().colour;
        gradient = saved.back().gradient;
        saved.pop_back();
    }

    void drawTracked (const String&, Rectangle<float>, const Font&, float, Justification) override {}
    void drawGlyph (const String&, Rectangle<float>, Colour, float, Point<float>) override {}

private:
    struct Saved { Colour colour; int gradient; int groups; };

    float w, h;
    std::string defs, body;
    Colour colour;
    int gradient = -1, gradients = 0, clips = 0, depth = 0;
    std::vector<Saved> saved;

    /** A number as short as it reads: two decimals at most, no trailing noughts — the same text for the same value. */
    static std::string num (double v)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.2f", std::abs (v) < 0.005 ? 0.0 : v);
        std::string s (b);
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
        return s;
    }

    static std::string hex (Colour c)
    {
        char b[8];
        std::snprintf (b, sizeof b, "#%02x%02x%02x", (unsigned) ((c.getARGB() >> 16) & 0xff), (unsigned) ((c.getARGB() >> 8) & 0xff),
                       (unsigned) (c.getARGB() & 0xff));
        return b;
    }

    static std::string opacity (const char* name, Colour c)
    {
        return c.getAlpha() == 255 ? std::string() : " " + std::string (name) + "=\"" + num (c.getFloatAlpha()) + "\"";
    }

    std::string fill() const
    {
        if (gradient >= 0)
            return " fill=\"url(#g" + std::to_string (gradient) + ")\"";
        return " fill=\"" + hex (colour) + "\"" + opacity ("fill-opacity", colour);
    }

    std::string stroke (float line) const
    {
        const auto paint = gradient >= 0 ? "url(#g" + std::to_string (gradient) + ")" : hex (colour);
        return " fill=\"none\" stroke=\"" + paint + "\"" + (gradient >= 0 ? std::string() : opacity ("stroke-opacity", colour))
             + " stroke-width=\"" + num (line) + "\"";
    }

    static std::string rounded (Rectangle<float> r, float corner)
    {
        return "<rect x=\"" + num (r.getX()) + "\" y=\"" + num (r.getY()) + "\" width=\"" + num (r.getWidth()) + "\" height=\""
             + num (r.getHeight()) + "\" rx=\"" + num (corner) + "\"";
    }

    static std::string ellipse (Rectangle<float> r)
    {
        return "<ellipse cx=\"" + num (r.getCentreX()) + "\" cy=\"" + num (r.getCentreY()) + "\" rx=\"" + num (0.5f * r.getWidth())
             + "\" ry=\"" + num (0.5f * r.getHeight()) + "\"";
    }

    /** JUCE's arc — angles clockwise from twelve o'clock — as SVG's, split in halves past half a turn's worth. */
    static std::string arc (const Path::Step& s, bool first)
    {
        const double cx = s.a, cy = s.b, rx = s.c, ry = s.d, rot = s.e, from = s.f, to = s.g;
        const auto at = [&] (double t)
        {
            const double x = rx * std::sin (t), y = -ry * std::cos (t);
            return std::pair<double, double> { cx + x * std::cos (rot) - y * std::sin (rot), cy + x * std::sin (rot) + y * std::cos (rot) };
        };
        const auto [x0, y0] = at (from);
        std::string d = (s.newSubPath || first ? "M" : "L") + num (x0) + " " + num (y0);
        const int halves = std::abs (to - from) > 3.14159 ? 2 : 1;
        for (int k = 1; k <= halves; ++k)
        {
            const double t = from + (to - from) * k / halves;
            const auto [x, y] = at (t);
            d += "A" + num (rx) + " " + num (ry) + " " + num (rot * 180.0 / 3.14159265358979) + " 0 " + (to > from ? "1" : "0") + " "
               + num (x) + " " + num (y);
        }
        return d;
    }
};

} // namespace cabir::ui
