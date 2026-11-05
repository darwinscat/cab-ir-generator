// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

// THE PANEL'S OWN FEW TYPES, JUCE-FREE, so the one panel draws in the app (onto juce::Graphics, src/panel/CabPicker.h)
// and on a page (into a buffer of drawing calls a canvas plays, src/web/PanelWeb.cpp). They carry JUCE's names and do
// what JUCE's do, to the bit — the rounding, the colour arithmetic, the rectangle's edges (JUCE 8, the parts the panel
// uses) — so the app draws exactly what it drew when the panel was written on JUCE's own.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace cabir::ui
{

template <typename T> constexpr T jmin (T a, T b) { return b < a ? b : a; }
template <typename T> constexpr T jmax (T a, T b) { return a < b ? b : a; }
template <typename T> constexpr T jlimit (T lowest, T highest, T v) { return v < lowest ? lowest : (highest < v ? highest : v); }

/** JUCE's: a double's nearest int by the magic-number trick, halves to even. */
inline int roundToInt (double value)
{
    union { int asInt[2]; double asDouble; } n;
    n.asDouble = value + 6755399441055744.0;
    return n.asInt[0];   // little-endian: every build of this panel
}
inline int roundToInt (float value) { return roundToInt ((double) value); }
inline int roundToInt (int value) { return value; }

template <typename T> struct MathConstants
{
    static constexpr T pi = static_cast<T> (3.141592653589793238L);
    static constexpr T twoPi = static_cast<T> (2 * 3.141592653589793238L);
};

//==============================================================================
template <typename T>
struct Point
{
    T x {}, y {};

    constexpr Point() = default;
    constexpr Point (T px, T py) : x (px), y (py) {}

    constexpr Point operator+ (Point o) const { return { x + o.x, y + o.y }; }
    constexpr Point operator- (Point o) const { return { x - o.x, y - o.y }; }
    Point& operator+= (Point o) { x += o.x; y += o.y; return *this; }
    template <typename S> constexpr Point operator* (S k) const { return { (T) (x * k), (T) (y * k) }; }
    template <typename S> constexpr Point operator/ (S k) const { return { (T) (x / k), (T) (y / k) }; }
    template <typename S> Point& operator*= (S k) { x = (T) (x * k); y = (T) (y * k); return *this; }

    T getDistanceFrom (Point o) const { return (T) std::hypot (x - o.x, y - o.y); }
    T getDistanceFromOrigin() const { return (T) std::hypot (x, y); }
    constexpr Point translated (T dx, T dy) const { return { x + dx, y + dy }; }
    Point<int> roundToInt() const { return { ui::roundToInt (x), ui::roundToInt (y) }; }
    constexpr Point<float> toFloat() const { return { (float) x, (float) y }; }
};

//==============================================================================
template <typename T>
class Rectangle
{
public:
    constexpr Rectangle() = default;
    constexpr Rectangle (T px, T py, T pw, T ph) : x (px), y (py), w (pw), h (ph) {}
    constexpr Rectangle (T pw, T ph) : w (pw), h (ph) {}

    constexpr T getX() const { return x; }
    constexpr T getY() const { return y; }
    constexpr T getWidth() const { return w; }
    constexpr T getHeight() const { return h; }
    constexpr T getRight() const { return x + w; }
    constexpr T getBottom() const { return y + h; }
    constexpr T getCentreX() const { return x + w / (T) 2; }
    constexpr T getCentreY() const { return y + h / (T) 2; }
    constexpr Point<T> getCentre() const { return { x + w / (T) 2, y + h / (T) 2 }; }
    constexpr bool isEmpty() const { return w <= T() || h <= T(); }

    constexpr Rectangle withPosition (T nx, T ny) const { return { nx, ny, w, h }; }
    constexpr Rectangle withPosition (Point<T> p) const { return { p.x, p.y, w, h }; }
    constexpr Rectangle withCentre (Point<T> c) const { return { c.x - w / (T) 2, c.y - h / (T) 2, w, h }; }
    constexpr Rectangle withWidth (T nw) const { return { x, y, jmax (T(), nw), h }; }
    constexpr Rectangle withHeight (T nh) const { return { x, y, w, jmax (T(), nh) }; }
    constexpr Rectangle withSizeKeepingCentre (T nw, T nh) const { return { x + (w - nw) / (T) 2, y + (h - nh) / (T) 2, nw, nh }; }
    constexpr Rectangle withLeft (T nl) const { return { nl, y, jmax (T(), x + w - nl), h }; }
    constexpr Rectangle withTop (T nt) const { return { x, nt, w, jmax (T(), y + h - nt) }; }
    constexpr Rectangle withTrimmedLeft (T a) const { return withLeft (x + a); }
    constexpr Rectangle withTrimmedRight (T a) const { return withWidth (w - a); }
    constexpr Rectangle withTrimmedTop (T a) const { return withTop (y + a); }
    constexpr Rectangle withTrimmedBottom (T a) const { return withHeight (h - a); }
    constexpr Rectangle translated (T dx, T dy) const { return { x + dx, y + dy, w, h }; }

    constexpr Rectangle expanded (T dx, T dy) const
    {
        const T nw = jmax (T(), w + dx * 2), nh = jmax (T(), h + dy * 2);
        return { x - dx, y - dy, nw, nh };
    }
    constexpr Rectangle expanded (T d) const { return expanded (d, d); }
    constexpr Rectangle reduced (T dx, T dy) const { return expanded (-dx, -dy); }
    constexpr Rectangle reduced (T d) const { return reduced (d, d); }

    Rectangle removeFromTop (T a)
    {
        const Rectangle r (x, y, w, jmin (a, h));
        y += r.h; h -= r.h;
        return r;
    }
    Rectangle removeFromLeft (T a)
    {
        const Rectangle r (x, y, jmin (a, w), h);
        x += r.w; w -= r.w;
        return r;
    }
    Rectangle removeFromRight (T a)
    {
        a = jmin (a, w);
        const Rectangle r (x + w - a, y, a, h);
        w -= a;
        return r;
    }
    Rectangle removeFromBottom (T a)
    {
        a = jmin (a, h);
        const Rectangle r (x, y + h - a, w, a);
        h -= a;
        return r;
    }

    constexpr bool contains (T px, T py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    constexpr bool contains (Point<T> p) const { return contains (p.x, p.y); }

    constexpr Rectangle<float> toFloat() const { return { (float) x, (float) y, (float) w, (float) h }; }
    Rectangle<int> toNearestInt() const { return { roundToInt (x), roundToInt (y), roundToInt (w), roundToInt (h) }; }

private:
    T x {}, y {}, w {}, h {};
};

//==============================================================================
/** JUCE's colour: ARGB, unpremultiplied, and its arithmetic to the byte. */
class Colour
{
public:
    constexpr Colour() = default;
    constexpr explicit Colour (uint32_t argbValue) : argb (argbValue) {}
    constexpr Colour (uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        : argb (((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b) {}

    constexpr uint32_t getARGB() const { return argb; }
    constexpr uint8_t getAlpha() const { return (uint8_t) (argb >> 24); }
    constexpr uint8_t getRed() const   { return (uint8_t) (argb >> 16); }
    constexpr uint8_t getGreen() const { return (uint8_t) (argb >> 8); }
    constexpr uint8_t getBlue() const  { return (uint8_t) argb; }
    float getFloatAlpha() const { return (float) getAlpha() / 255.0f; }

    Colour withAlpha (float a) const
    {
        const uint8_t na = a <= 0.0f ? 0 : (a >= 1.0f ? 255 : (uint8_t) roundToInt (a * 255.0f));
        return Colour ((argb & 0x00ffffffu) | ((uint32_t) na << 24));
    }

    Colour withMultipliedAlpha (float k) const
    {
        const auto na = (uint8_t) jmin (0xff, roundToInt (k * (float) getAlpha()));
        return Colour ((argb & 0x00ffffffu) | ((uint32_t) na << 24));
    }

    Colour interpolatedWith (Colour other, float proportionOfOther) const
    {
        if (proportionOfOther <= 0)
            return *this;
        if (proportionOfOther >= 1.0f)
            return other;
        uint32_t c1 = premultiplied (argb), c2 = premultiplied (other.argb);
        const auto amount = (uint32_t) roundToInt (proportionOfOther * 255.0f);
        uint32_t even = c1 & 0x00ff00ffu;
        even += (((c2 & 0x00ff00ffu) - even) * amount) >> 8;
        even &= 0x00ff00ffu;
        uint32_t odd = (c1 >> 8) & 0x00ff00ffu;
        odd += ((((c2 >> 8) & 0x00ff00ffu) - odd) * amount) >> 8;
        odd &= 0x00ff00ffu;
        c1 = (odd << 8) | even;
        return Colour (unpremultiplied (c1));
    }

    Colour brighter (float amount) const
    {
        amount = 1.0f / (1.0f + amount);
        return Colour ((uint8_t) (255 - (amount * (float) (255 - getRed()))),
                       (uint8_t) (255 - (amount * (float) (255 - getGreen()))),
                       (uint8_t) (255 - (amount * (float) (255 - getBlue()))),
                       getAlpha());
    }

    constexpr bool operator== (const Colour& o) const { return argb == o.argb; }

private:
    uint32_t argb = 0;

    static uint32_t premultiplied (uint32_t c)
    {
        const uint32_t a = c >> 24;
        if (a >= 0xff)
            return c;
        if (a == 0)
            return 0;
        const auto ch = [a] (uint32_t v) { return (v * a + 0x7f) >> 8; };
        return (a << 24) | (ch ((c >> 16) & 0xff) << 16) | (ch ((c >> 8) & 0xff) << 8) | ch (c & 0xff);
    }

    static uint32_t unpremultiplied (uint32_t c)
    {
        const uint32_t a = c >> 24;
        if (a >= 0xff)
            return c;
        if (a == 0)
            return 0;
        const auto ch = [a] (uint32_t v) { return std::min (0xffu, (v * 0xffu) / a); };
        return (a << 24) | (ch ((c >> 16) & 0xff) << 16) | (ch ((c >> 8) & 0xff) << 8) | ch (c & 0xff);
    }
};

namespace Colours { inline constexpr Colour black { 0xff000000u }; }

/** A two-colour gradient, linear or radial from (x1, y1), as JUCE's ColourGradient. */
struct ColourGradient
{
    Colour colour1;
    float x1 = 0, y1 = 0;
    Colour colour2;
    float x2 = 0, y2 = 0;
    bool radial = false;

    ColourGradient (Colour c1, float px1, float py1, Colour c2, float px2, float py2, bool isRadial)
        : colour1 (c1), x1 (px1), y1 (py1), colour2 (c2), x2 (px2), y2 (py2), radial (isRadial) {}
};

//==============================================================================
/** JUCE's flags, the ones the panel places words by. */
class Justification
{
public:
    enum Flags { left = 1, right = 2, horizontallyCentred = 4, top = 8, bottom = 16, verticallyCentred = 32,
                 centred = 36, centredLeft = 33, centredRight = 34, topLeft = 9 };
    constexpr Justification (int f) : flags (f) {}
    constexpr int getFlags() const { return flags; }
    constexpr bool testFlags (int f) const { return (flags & f) != 0; }

private:
    int flags;
};

//==============================================================================
/** A path as the calls that made it — replayed onto juce::Path in the app, onto a canvas on a page. */
class Path
{
public:
    enum class Op : uint8_t { move, line, arc };
    struct Step
    {
        Op op;
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
        bool newSubPath = false;
    };

    void startNewSubPath (float x, float y) { steps.push_back ({ Op::move, x, y }); }
    void startNewSubPath (Point<float> p) { startNewSubPath (p.x, p.y); }
    void lineTo (float x, float y) { steps.push_back ({ Op::line, x, y }); }
    void lineTo (Point<float> p) { lineTo (p.x, p.y); }

    /** JUCE's addCentredArc: centre, radii, the ellipse's rotation, from and to (radians, clockwise from twelve o'clock). */
    void addCentredArc (float cx, float cy, float rx, float ry, float rotation, float from, float to, bool startAsNewSubPath)
    {
        steps.push_back ({ Op::arc, cx, cy, rx, ry, rotation, from, to, startAsNewSubPath });
    }

    const std::vector<Step>& getSteps() const { return steps; }

private:
    std::vector<Step> steps;
};

struct PathStrokeType
{
    enum JointStyle { mitered, curved, beveled };
    enum EndCapStyle { butt, square, rounded };

    PathStrokeType (float strokeThickness, JointStyle j = mitered, EndCapStyle e = butt)
        : thickness (strokeThickness), joint (j), end (e) {}

    float thickness;
    JointStyle joint;
    EndCapStyle end;
};

//==============================================================================
/** UTF-8 text with the few of juce::String's calls the panel makes. */
class String
{
public:
    String() = default;
    String (const char* t) : s (t != nullptr ? t : "") {}
    String (std::string t) : s (std::move (t)) {}
    explicit String (int v) : s (std::to_string (v)) {}
    String (float v, int decimals)
    {
        char b[64];
        std::snprintf (b, sizeof b, "%.*f", decimals, (double) v);
        s = b;
    }
    static String fromUTF8 (const char* t) { return String (t); }

    bool isEmpty() const { return s.empty(); }
    bool isNotEmpty() const { return ! s.empty(); }
    String toUpperCase() const
    {
        auto u = s;
        for (auto& ch : u)
            if (ch >= 'a' && ch <= 'z')
                ch = (char) (ch - 'a' + 'A');
        return u;
    }
    const std::string& toStdString() const { return s; }
    const char* toRawUTF8() const { return s.c_str(); }

    String operator+ (const String& o) const { return s + o.s; }
    String operator+ (const char* o) const { return s + o; }
    friend String operator+ (const char* a, const String& b) { return std::string (a) + b.s; }
    bool operator== (const String& o) const { return s == o.s; }
    bool operator== (const char* o) const { return s == o; }
    bool operator< (const String& o) const { return s < o.s; }

private:
    std::string s;
};

class StringArray
{
public:
    StringArray (std::initializer_list<String> items) : v (items) {}
    int size() const { return (int) v.size(); }
    const String& operator[] (int i) const { return v[(size_t) i]; }
    bool contains (const String& t) const { return std::find (v.begin(), v.end(), t) != v.end(); }
    auto begin() const { return v.begin(); }
    auto end() const { return v.end(); }

private:
    std::vector<String> v;
};

//==============================================================================
/** A display face at a height: the host measures and draws it (Look). */
struct Font
{
    float height = 13.0f;
    float getHeight() const { return height; }
    float getAscent() const;
};

struct MouseEvent
{
    Point<float> position;
    Point<int> getPosition() const { return position.roundToInt(); }
};

//==============================================================================
/** WHAT THE PANEL DRAWS WITH — juce::Graphics's calls, the ones it makes, and two of its own: a word tracked as the
    host lays words out, and a mic's glyph. The app draws them onto juce::Graphics; a page records them. */
class Graphics
{
public:
    virtual ~Graphics() = default;

    virtual void setColour (Colour) = 0;
    virtual void setGradientFill (const ColourGradient&) = 0;
    virtual void fillRect (Rectangle<float>) = 0;
    virtual void fillRoundedRectangle (Rectangle<float>, float cornerSize) = 0;
    virtual void drawRoundedRectangle (Rectangle<float>, float cornerSize, float lineThickness) = 0;
    virtual void drawLine (float x1, float y1, float x2, float y2, float lineThickness) = 0;
    virtual void fillEllipse (Rectangle<float>) = 0;
    virtual void drawEllipse (Rectangle<float>, float lineThickness) = 0;
    virtual void strokePath (const Path&, const PathStrokeType&) = 0;
    virtual void reduceClipRegion (Rectangle<int>) = 0;
    virtual void saveState() = 0;
    virtual void restoreState() = 0;

    /** `text` with per-character tracking (em) in `area`, as `justification` places it, in the current colour. */
    virtual void drawTracked (const String& text, Rectangle<float> area, const Font&, float trackingEm, Justification) = 0;

    /** A mic's glyph (MicGlyphs.h) in `box`, tinted `colour`, leaning `lean` radians about `pivot`. */
    virtual void drawGlyph (const String& mic, Rectangle<float> box, Colour, float lean, Point<float> pivot) = 0;

    struct ScopedSaveState
    {
        explicit ScopedSaveState (Graphics& graphics) : g (graphics) { g.saveState(); }
        ~ScopedSaveState() { g.restoreState(); }
        ScopedSaveState (const ScopedSaveState&) = delete;
        ScopedSaveState& operator= (const ScopedSaveState&) = delete;
        Graphics& g;
    };
};

//==============================================================================
/** HOW THE PANEL LOOKS, without JUCE: the colours, and the host's type — how wide a tracked word is and how high its
    ascent — given by whoever draws (the app from its Look, a page from its canvas). */
struct Look
{
    Colour orange  { 0xffff8a3du };
    Colour violet  { 0xff9778ffu };
    Colour lilac   { 0xffb9a6ffu };
    Colour panel   { 0xff111119u };
    Colour panel2  { 0xff15151fu };
    Colour bezel   { 0xff050509u };
    Colour hair    = violet.withAlpha (0.12f);
    Colour hair2   = violet.withAlpha (0.24f);
    Colour tx      { 0xffece9f6u };
    Colour txDim   { 0xffa6a2bdu };
    Colour txFaint { 0xff6f6b86u };

    std::function<float (const String& text, float height, float trackingEm)> measure
        = [] (const String& text, float height, float trackingEm)
          { return (float) text.toStdString().size() * height * (0.6f + trackingEm); };
    std::function<float (float height)> ascent = [] (float height) { return 0.8f * height; };

    Font font (float height) const { return { height }; }
    float trackedWidth (const String& text, const Font& f, float trackingEm) const { return measure (text, f.height, trackingEm); }
    void drawTracked (Graphics& g, const String& text, Rectangle<float> area, const Font& f, float trackingEm, Justification j) const
    {
        g.drawTracked (text, area, f, trackingEm, j);
    }
};

/** The look the panel draws with — one for the whole process. */
inline Look& look()
{
    static Look one;
    return one;
}

inline float Font::getAscent() const { return look().ascent (height); }

} // namespace cabir::ui
