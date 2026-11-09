// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PANEL FOR A PAGE: the app's own panel (src/panel/Panel.h), drawn into a buffer of drawing calls a canvas plays
// (src/web/cabir-panel.mjs plays them as the app's JUCE does), the hand's moves handed in, what it asks handed out.
//
//   cabir_panel_size (w, h)                    its size in the panel's units (960 x 446 the app's, no foot)
//   cabir_panel_options (find, download, def, close)   which buttons it offers — 1 each for the app's: 1, 1, 0, 0
//   cabir_panel_show (toml)                    what plays: the state, as the synthesis takes it
//   cabir_panel_response (now, before, comb)   the band's curves, cabir_points() floats each; null for none
//   cabir_panel_search (x, y, score) / cabir_panel_end_search ()      FIND's map as it goes, and done
//   cabir_panel_pointer (kind, x, y)           0 move, 1 down, 2 drag, 3 up, 4 exit, 5 double click
//   cabir_panel_grabs (x, y)                   1 if a press there would start a drag (a mic on the speaker, its distance
//                                              track or angle knob while shown, BLEND, B's TIME), else 0 — a touch's
//                                              host asks it as the finger lands: the panel's touch, or the page's scroll
//   cabir_panel_tick (seconds)                 the page's clock, while cabir_panel_wants_frames () says 1
//   cabir_panel_paint ()                       the calls: their 32-bit words, at cabir_panel_ops (); their words' text
//                                              in cabir_panel_text () (cabir_panel_text_size () bytes, UTF-8)
//   cabir_panel_dirty ()                       1 when it asked to be drawn again since the last paint
//   cabir_panel_event ()                       what it asks, one at a time: 0 nothing, 1 kept (a state set), 2 moving (heard,
//                                              not kept), 3 FIND, 4 DOWNLOAD IR — the state at cabir_panel_event_state ()
//                                              — and 5, DOWNLOAD IR's ▾ (cabir_panel_download_format): the page's format menu,
//                                              anchored at cabir_panel_format_rect () (x, y, w, h, ints)
//   cabir_panel_event_placed ()                1 when the last event, a 1, is a state the panel placed on purpose (FRED):
//                                              keep it, but B turned on by it is not taken up, so no search for B; else 0
//   cabir_panel_download_format (show)         DOWNLOAD IR's ▾ beside it, or none (the default)
//   cabir_panel_rect (name)                    where a page's hint goes, as the panel stands now: "cabinet" (the boxes),
//                                              "mic-a" (A's glyph on the speaker; the speaker while A is off), "download"
//                                              — x, y, w, h (ints, the panel's units); null for another name or none shown
//   cabir_panel_download_note (text)           the line under the button: what the next download will be (UTF-8; "" none)
//   cabir_panel_sound (on)                     the page has sound: SPECTRUM under the curves, as the app has it (none
//                                              until said)
//   cabir_panel_spectrum (on)                  SPECTRUM switched, the app's menu item (on until said); off, it goes
//   cabir_spectrum_wanted ()                   1 while the panel shows it: then the page hands frames in, else needs not
//   cabir_spectrum_size ()                     a frame's length in samples (16384)
//   cabir_spectrum_frame (after, before, cover)   about thirty times a second, as the app's: the latest
//                                              cabir_spectrum_size () samples at 48 kHz of what is heard (after the
//                                              cabinet, BYPASS's dry in it) and of what goes in (before it), floats in
//                                              the module's heap, either null for none; cover, the samples played since the
//                                              last frame — 0 while nothing plays, and the two fade as the app's do
//   cabir_glyph_svg (mic)                      a mic's glyph as SVG, its black for the canvas to recolour
//   cabir_label (toml)                         a state as a line reads it, the app's words: "4x10 PORTED · MD421+U47" (UTF-8)
//
// THE CALLS, 32-bit words — floats, unless said: an opcode, then its arguments.
//    1 colour        argb (uint32)
//    2 gradient      argb1 (uint32) x1 y1 argb2 (uint32) x2 y2 radial — JUCE's ColourGradient, its first colour at
//                    (x1, y1), the second at (x2, y2), radial a circle about (x1, y1)
//    3 fill rect     x y w h
//    4 fill rounded  x y w h corner
//    5 draw rounded  x y w h corner line
//    6 line          x1 y1 x2 y2 line         (butt ends)
//    7 fill ellipse  x y w h
//    8 draw ellipse  x y w h line
//    9 stroke path   line joint end steps (joint: 0 mitered 1 curved 2 beveled; end: 0 butt 1 square 2 rounded),
//                    then `steps` of 9 words: op (0 move 1 line 2 arc), a b c d e f g, newSubPath — an arc is JUCE's
//                    addCentredArc (cx, cy, rx, ry, rotation, from, to): radians clockwise from twelve o'clock
//   10 clip          x y w h                  (intersected, until the restore)
//   11 save
//   12 restore
//   13 words         text offset (uint32) text bytes (uint32) x y w h height tracking justification (uint32: JUCE's
//                    flags — 1 left 2 right 4 centred across 8 top 16 bottom 32 centred down)
//   14 glyph         mic offset (uint32) mic bytes (uint32) x y w h argb (uint32) lean pivotX pivotY
//
// The words are tracked as the app lays them out (Look::drawTrackedPlain): each glyph `tracking` x height further on
// than the one before, the line's width that, placed by the flags; centred down, the baseline at the area's centre
// plus half of ascent less descent. The page measures for the panel: Module.cabirMeasure (text, height, tracking)
// and Module.cabirAscent (height), in the face it draws with.

#include <panel/MicSvgs.h>
#include <panel/Panel.h>
#include <synth/State.h>

#include <felitronics/analysis/MultiResSpectrumPane.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
 #include <emscripten/emscripten.h>
 #define CABIR_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE

EM_JS (float, cabir_js_measure, (const char* text, float height, float tracking), {
    const t = UTF8ToString (text);
    return Module.cabirMeasure ? Module.cabirMeasure (t, height, tracking) : t.length * height * (0.6 + tracking);
});
EM_JS (float, cabir_js_ascent, (float height), {
    return Module.cabirAscent ? Module.cabirAscent (height) : 0.8 * height;
});
#else
 #define CABIR_EXPORT extern "C"
static float cabir_js_measure (const char* text, float height, float tracking) { return (float) std::strlen (text) * height * (0.6f + tracking); }
static float cabir_js_ascent (float height) { return 0.8f * height; }
#endif

namespace
{
    using namespace cabir;

    /** The panel's calls, written down. */
    class Recorder final : public ui::Graphics
    {
    public:
        std::vector<uint32_t> words;
        std::string text;

        void clear() { words.clear(); text.clear(); }

        void setColour (ui::Colour c) override { op (1); u (c.getARGB()); }
        void setGradientFill (const ui::ColourGradient& g) override
        {
            op (2); u (g.colour1.getARGB()); f (g.x1); f (g.y1); u (g.colour2.getARGB()); f (g.x2); f (g.y2); f (g.radial ? 1.0f : 0.0f);
        }
        void fillRect (ui::Rectangle<float> r) override { op (3); rect (r); }
        void fillRoundedRectangle (ui::Rectangle<float> r, float corner) override { op (4); rect (r); f (corner); }
        void drawRoundedRectangle (ui::Rectangle<float> r, float corner, float line) override { op (5); rect (r); f (corner); f (line); }
        void drawLine (float x1, float y1, float x2, float y2, float line) override { op (6); f (x1); f (y1); f (x2); f (y2); f (line); }
        void fillEllipse (ui::Rectangle<float> r) override { op (7); rect (r); }
        void drawEllipse (ui::Rectangle<float> r, float line) override { op (8); rect (r); f (line); }
        void strokePath (const ui::Path& p, const ui::PathStrokeType& s) override
        {
            op (9); f (s.thickness); f ((float) s.joint); f ((float) s.end); f ((float) p.getSteps().size());
            for (const auto& step : p.getSteps())
            {
                f ((float) step.op); f (step.a); f (step.b); f (step.c); f (step.d); f (step.e); f (step.f); f (step.g);
                f (step.newSubPath ? 1.0f : 0.0f);
            }
        }
        void reduceClipRegion (ui::Rectangle<int> r) override { op (10); rect (r.toFloat()); }
        void saveState() override { op (11); }
        void restoreState() override { op (12); }
        void drawTracked (const ui::String& t, ui::Rectangle<float> area, const ui::Font& font, float tracking, ui::Justification j) override
        {
            op (13); string (t); rect (area); f (font.height); f (tracking); u ((uint32_t) j.getFlags());
        }
        void drawGlyph (const ui::String& mic, ui::Rectangle<float> box, ui::Colour colour, float lean, ui::Point<float> pivot) override
        {
            op (14); string (mic); rect (box); u (colour.getARGB()); f (lean); f (pivot.x); f (pivot.y);
        }

    private:
        void op (uint32_t code) { words.push_back (code); }
        void u (uint32_t v) { words.push_back (v); }
        void f (float v) { uint32_t w; std::memcpy (&w, &v, 4); words.push_back (w); }
        void rect (ui::Rectangle<float> r) { f (r.getX()); f (r.getY()); f (r.getWidth()); f (r.getHeight()); }
        void string (const ui::String& s)
        {
            u ((uint32_t) text.size());
            u ((uint32_t) s.toStdString().size());
            text += s.toStdString();
        }
    };

    struct Event { int kind; std::string state; bool placed = false; };

    /** SPECTRUM, one of its two: the page's frames read as the app reads its own (app/Spectrum.h) — tabbyEQ's pane, in
        1/24-octave bands at the panel's columns, tilted +4.5 dB an octave about 1 kHz. */
    struct Heard
    {
        std::unique_ptr<felitronics::analysis::MultiResSpectrumPane> pane = std::make_unique<felitronics::analysis::MultiResSpectrumPane>();
        std::vector<float> db;

        void take (const float* frame, int cover)
        {
            if (frame != nullptr && cover > 0)
            {
                std::copy (frame, frame + pane->frameSize(), pane->frameInput());
                pane->coverSamples = std::min (cover, pane->frameSize());
                pane->ingest (pane->frameOrder());
            }
            else
                pane->starve();
            db.resize ((size_t) Panel::spectrumColumns);
            for (int i = 0; i < Panel::spectrumColumns; ++i)
                db[(size_t) i] = (float) pane->readDb (Panel::spectrumHz (i), 48000.0, 4.5, 1000.0);
        }
    };

    struct PanelSession
    {
        Panel panel;
        Recorder drawn;
        bool dirty = true;
        std::deque<Event> events;
        Event last;
        std::vector<float> now, before, comb;
        int formatRect[4] {};
        int hintRect[4] {};
        Heard post, pre;   // what is heard, and what goes into the cabinet (tabbyEQ's post and pre)
        bool spectrumShown = false;

        PanelSession()
        {
            auto& look = ui::look();
            look.measure = [] (const ui::String& t, float height, float tracking) { return cabir_js_measure (t.toRawUTF8(), height, tracking); };
            look.ascent = [] (float height) { return cabir_js_ascent (height); };
            panel.onRepaint = [this] { dirty = true; };
            panel.showDefault = false;
            panel.setSize (Panel::panelW, Panel::panelH - Panel::footRowH);
            options (true, true, false, false);
        }

        void ask (int kind, int cab, const Panel::Setup& setup) { events.push_back ({ kind, toToml (Panel::stateOf (cab, setup)) }); }

        void options (bool find, bool download, bool def, bool close)
        {
            panel.onSetup = [this] (int cab, Panel::Setup s) { ask (1, cab, s); };
            panel.onPlaced = [this] (int cab, Panel::Setup s) { ask (1, cab, s); events.back().placed = true; };   // FRED's
            panel.onMove = [this] (int cab, Panel::Setup s) { ask (2, cab, s); };
            panel.onFind = find ? std::function<void (int, Panel::Setup)> ([this] (int cab, Panel::Setup s) { ask (3, cab, s); }) : nullptr;
            panel.onDownload = download ? std::function<void()> ([this] { events.push_back ({ 4, {} }); }) : nullptr;
            panel.onClose = close ? std::function<void()> ([] {}) : nullptr;
            panel.showDefault = def;
            dirty = true;
        }
    };

    PanelSession& session()
    {
        static PanelSession s;
        return s;
    }

    std::vector<float> curve (const float* v)
    {
        return v != nullptr ? std::vector<float> (v, v + Panel::responsePoints) : std::vector<float>();
    }
}

CABIR_EXPORT void cabir_panel_size (int w, int h) { session().panel.setSize (w, h); }

CABIR_EXPORT void cabir_panel_options (int find, int download, int def, int close)
{
    session().options (find != 0, download != 0, def != 0, close != 0);
}

CABIR_EXPORT int cabir_panel_show (const char* toml)
{
    const auto state = knownState (toml != nullptr ? toml : "");
    if (! state)
        return 0;
    const auto [cab, setup] = Panel::setupOf (*state);
    session().panel.show (cab, setup);
    return 1;
}

CABIR_EXPORT void cabir_panel_response (const float* now, const float* before, const float* comb)
{
    session().panel.showResponse (curve (now), curve (before), curve (comb));
}

CABIR_EXPORT void cabir_panel_search (float x, float y, float score) { session().panel.showSearch (x, y, score); }
CABIR_EXPORT void cabir_panel_end_search() { session().panel.endSearch(); }

CABIR_EXPORT void cabir_panel_pointer (int kind, float x, float y)
{
    auto& p = session().panel;
    const ui::MouseEvent e { { x, y } };
    switch (kind)
    {
        case 0: p.mouseMove (e); break;
        case 1: p.mouseDown (e); break;
        case 2: p.mouseDrag (e); break;
        case 3: p.mouseUp (e); break;
        case 4: p.mouseExit (e); break;
        case 5: p.mouseDoubleClick (e); break;
        default: break;
    }
}

CABIR_EXPORT int cabir_panel_grabs (float x, float y) { return session().panel.grabs ({ x, y }) ? 1 : 0; }
CABIR_EXPORT void cabir_panel_tick (double seconds) { session().panel.tick (seconds); }
CABIR_EXPORT int cabir_panel_wants_frames() { return session().panel.wantsFrames() ? 1 : 0; }

CABIR_EXPORT int cabir_panel_paint()
{
    auto& s = session();
    s.drawn.clear();
    s.panel.paint (s.drawn);
    s.dirty = false;
    return (int) s.drawn.words.size();
}

CABIR_EXPORT const uint32_t* cabir_panel_ops() { return session().drawn.words.data(); }
CABIR_EXPORT const char* cabir_panel_text() { return session().drawn.text.data(); }
CABIR_EXPORT int cabir_panel_text_size() { return (int) session().drawn.text.size(); }
CABIR_EXPORT int cabir_panel_dirty() { return session().dirty ? 1 : 0; }

CABIR_EXPORT int cabir_panel_event()
{
    auto& s = session();
    if (s.events.empty())
        return 0;
    s.last = std::move (s.events.front());
    s.events.pop_front();
    return s.last.kind;
}

CABIR_EXPORT const char* cabir_panel_event_state() { return session().last.state.c_str(); }
CABIR_EXPORT int cabir_panel_event_placed() { return session().last.placed ? 1 : 0; }

CABIR_EXPORT void cabir_panel_download_format (int show)
{
    auto& s = session();
    s.panel.onDownloadFormat = show != 0 ? std::function<void()> ([&s] { s.events.push_back ({ 5, {} }); }) : nullptr;
    s.dirty = true;
}

CABIR_EXPORT const int* cabir_panel_format_rect()
{
    auto& s = session();
    const auto r = s.panel.formatButton();
    s.formatRect[0] = r.getX();
    s.formatRect[1] = r.getY();
    s.formatRect[2] = r.getWidth();
    s.formatRect[3] = r.getHeight();
    return s.formatRect;
}

CABIR_EXPORT const int* cabir_panel_rect (const char* name)
{
    auto& s = session();
    const auto r = s.panel.hintRect (ui::String (name != nullptr ? name : "")).toNearestInt();
    if (r.isEmpty())
        return nullptr;
    s.hintRect[0] = r.getX();
    s.hintRect[1] = r.getY();
    s.hintRect[2] = r.getWidth();
    s.hintRect[3] = r.getHeight();
    return s.hintRect;
}

CABIR_EXPORT void cabir_panel_download_note (const char* text)
{
    auto& s = session();
    s.panel.downloadNote = ui::String (text != nullptr ? text : "");
    s.dirty = true;
}

CABIR_EXPORT int cabir_spectrum_frame (const float* after, const float* before, int cover)
{
    // As the app's tick: while shown, both frames in and the panel handed them; gone, both cleared once.
    auto& s = session();
    if (! s.panel.spectrumOn())
    {
        if (s.spectrumShown)
        {
            s.panel.showSpectrum ({}, {});
            s.post.pane->reset();
            s.pre.pane->reset();
        }
        s.spectrumShown = false;
        return 0;
    }
    s.post.take (after, cover);
    s.pre.take (before, cover);
    s.panel.showSpectrum (s.post.db, s.pre.db);
    s.spectrumShown = true;
    return 1;
}

CABIR_EXPORT void cabir_panel_sound (int on)
{
    auto& s = session();
    s.panel.hasSound = on != 0;
    if (! s.panel.hasSound && s.spectrumShown)
        (void) cabir_spectrum_frame (nullptr, nullptr, 0);   // its spectrum gone at once, not at a frame that may not come
    s.dirty = true;
}

CABIR_EXPORT void cabir_panel_spectrum (int on) { session().panel.setSpectrumOn (on != 0); }
CABIR_EXPORT int cabir_spectrum_wanted() { return session().panel.spectrumOn() ? 1 : 0; }
CABIR_EXPORT int cabir_spectrum_size() { return session().post.pane->frameSize(); }

CABIR_EXPORT const char* cabir_glyph_svg (const char* mic) { return micSvg (mic != nullptr ? mic : ""); }

CABIR_EXPORT const char* cabir_label (const char* toml)
{
    static std::string label;
    const auto state = knownState (toml != nullptr ? toml : "");
    label = state ? Panel::labelOf (*state).toStdString() : std::string();
    return label.c_str();
}
