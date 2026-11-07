// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include "ui/Ui.h"

#include <array>
#include "../synth/Response.h"
#include "../synth/State.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cabir
{

/** THE BASS'S CABINETS, DRAWN — one panel, the boxes at the left and the chosen one's speaker at the right.

    THE PILE: a box of each size, no maker's name over them (the owner, 05.11), to scale and stacked as a backline stands
    them, the small on the big; each with its speakers, its horn and its port where it has them; the chosen box's size
    big at their top left; under them DOWNLOAD IR, the cabinet that plays saved as a file; over them its BACK, CLOSED, PORTED or
    OPEN, each the box drawn cut through from the side, a click each — the back goes on from box to box unchanged, as
    the mics do, the box's own marked by a dot and DEFAULT's to go back to (the owner, 05.11); a sealed one turned
    ported gets a port at its foot. Every bass of the family offers the same ten (the owner, 05.11): none knows a
    cabinet of its own.
    A click on a box picks it, the mics staying where they stand, and the horn too — but a box whose horn comes off
    (Cab::hornOn, the 1x15-classic's and the 8x10's) takes it off, and the next box puts it back; a click on the chosen box's
    horn turns the horn off and on, its step kept.

    THE SPEAKER: the chosen box's first speaker, flat, its neighbours at its edge, its horn big in the corner on the horn's
    side — the speaker moved away from it — and beside it HORN's attenuator, OFF -12 -6 0; the mics in perspective on
    the speaker, each taken by any part of it, its capsule or its body, and dragged from where the hand took it across
    the speaker and just past its rim — a click on the bare cone moves none (the owner, 05.11); their two columns either side, each its mic, its places (CAP,
    EDGE, OFF and ROOM, one click each), distance and angle; BLEND, ALIGN and Ø under it, and two buttons: DEFAULT, the
    box's own mic back at its cap — SURE? at the first click, done at the second — and CLOSE. A mic's row clicked again
    while both are heard turns that mic off where it stands, its column dark, BLEND left where it is and not heard while
    one mic is (the owner, 04.11); a click in the dark column brings it back; the last one stays. While the hand moves,
    the cabinet is heard as it goes (onMove) — each position the worker reaches next, a tenth of a second for one mic,
    near a second for two on the 8x10 — and what is set is kept when the hand lets go (onSetup).

    UNDER THE HAND whatever a click there works is lit (the owner, 05.11): a box and a back framed, a word brighter, a
    button's line, a mic on the speaker ringed at its capsule — the same test the click goes by (hotAt).

    THE RESPONSE, across the panel under it all: what plays, 30 Hz to 12 kHz — the cabinet heard, following a moving
    hand, green where it is fine and red where it falls into a pit or its two mics cancel (the owner, 05.11), and the
    one kept before it a faint grey ghost, so every click and every drag reads as before and after; with two mics
    heard, green under it where they add in phase and red where they cancel.

    A dumb view: the cabinets in (cabs(), geometry from data/cabinets.txt — metres, the baffle's centre the
    origin, up positive), what plays in (show, showResponse), a pick out (onSetup). */
class Panel final
{
public:
    Panel() = default;

    // ---------------------------------------------------------------- what a host gives it
    // Its size, a call to draw it again, and the time — ticked about thirty times a second while it wants frames (DOWNLOAD
    // IR's pulse, the search map's fade, DEFAULT's question running out). A host draws it with paint and hands it the
    // hand's moves; JUCE's CabPicker is one host, the page's module another.
    void setSize (int width, int height) { wide = width; high = height; repaint(); }
    int getWidth() const { return wide; }
    int getHeight() const { return high; }
    ui::Rectangle<int> getLocalBounds() const { return { 0, 0, wide, high }; }
    std::function<void()> onRepaint;
    std::function<void (ui::Rectangle<int>)> onRepaintArea;   // ...or only a part, where it can say which

    void tick (double nowSeconds)
    {
        clock = nowSeconds;
        if (defaultArmed && clock >= armedUntil)
            disarmDefault();
        timerCallback();
    }

    /** True while something moves on its own: a host ticks it then, and need not otherwise. */
    bool wantsFrames() const { return ! downloadArea().isEmpty() || searchFade > 0.0f || defaultArmed; }

    /** The size it is drawn for — small enough to open over a cabin's face too. */
    static constexpr int panelW = 906, panelH = 518;

    struct Spot
    {
        float x = 0.0f, y = 0.0f, distance = 0.05f;   // across the first speaker in radii from the cap; in front of it, m
        bool horn = true;
        ui::String mic;                             // one of mics(), or empty for the cabinet's own
        float angle = 0.0f;                           // degrees it leans back toward the cap
        float hornDb = 0.0f;                          // the horn's attenuator while it is on: 0, -6, -12 (A's counts)
    };

    /** Two mics on the box (the engine's setup, as a host keeps it), each on or off: both on, B mixed into A by `blend` on one clock
        unless `align`, B turned over by `invert`; one on, that one alone, and `blend` not heard. The horn is A's. ALIGN
        from the start: two mics at two distances, unaligned, comb — rarely what a second mic is put up for. */
    struct Setup
    {
        Spot a { -0.3f, 0.0f, 0.05f, true, {} }, b { 0.4f, 0.0f, 0.25f, true, "u87" };   // A a little left of the cap, B right of it
        float blend = 0.5f;
        bool align = true, invert = false;
        bool aOn = true, bOn = false;
        int back = -1;   // the box's back as the player turned it — an index of backWords — or -1, the box's own (ownBack)
        float shiftMs = 0.0f;   // B moved in time against A, ms, later positive (TIME)
        int speaker = -1;   // the box's speaker as the player turned it — an index of the catalog's speakers — or -1, its own
        bool wall = false;  // the wall behind the box, the guitar's switch (WALL)
        bool cone = false;  // the speakers' cone as the catalog's variant has it (the guitar's PAPER, SPEAKER clicked)
        int room = -1;      // the room round the box — an index of the catalog's rooms — or -1, none (ROOM's OFF)
    };

    struct Cab
    {
        ui::String name;                       // as the table names it: "4x10-classic"
        int family = 0;                          // whose box it is, for its corners: 1 the rounded ones
        float w = 0.6f, h = 0.6f;                // the baffle
        std::vector<ui::Point<float>> drivers; // the first is the one the mic stands at
        float radius = 0.105f;                   // a cone's radiating radius
        bool horn = false;
        ui::Point<float> hornAt;
        bool port = false;
        ui::Point<float> portAt;
        ui::String mic = "md421";              // the one its table's cap section stands
        bool hornOn = true;                      // how its horn comes when the box is picked: the 1x15-classic's and the 8x10's off
        ui::String speaker;                      // its own speaker, one of the catalog's — empty where it offers none (the bass's)
    };

    /** WHAT A PANEL OFFERS: its boxes, how they stand, its mics, and which boxes the table seals and which it leaves open.
        The bass's (bassCatalog) unless a host hands it another before it draws one (useCatalog): the guitar's app hands
        its own (GuitarCatalog.h). One for the whole program — every panel and every static below reads it. */
    struct Catalog
    {
        std::vector<Cab> cabs;
        // The boxes it offers, as they stand: its columns left to right, each from the floor up. The table's other boxes
        // stay in cabs: a state that kept one still reads as its size.
        std::vector<ui::StringArray> pile;
        // The mics to choose from, as the table names them (its "mic-<name>" sections), and as the columns write them.
        ui::StringArray mics, micWords;
        // The boxes the table calls sealed and open (its `alignment`); every other one is ported.
        ui::StringArray sealed, open;
        // The speakers to choose from, as the table names them (its "speaker-<name>" sections), and as their row writes
        // them — the guitar's; none, no row (the bass's).
        ui::StringArray speakers, speakerWords;
        // The boxes whose own back is half closed (`alignment = half`), and the back's cells left to right by synth::Back
        // — none said, CLOSED PORTED OPEN (the bass's); the guitar's has HALF too.
        ui::StringArray half;
        std::vector<int> backRow;
        // WALL under BACK, the switch for a wall behind the box (the guitar's): false, none (the bass's).
        bool wallSwitch = false;
        // The speakers' other cone, its table's "speaker-<name>-<variant>" sections — SPEAKER itself the switch, lit
        // where it is on (the guitar's "paper"); empty, none.
        ui::String coneVariant;
        // How far a mic goes and where ROOM puts it: the bass's 30 cm, the guitar's a metre and a half, ROOM at one.
        float farthestCm = 30.0f, roomM = 0.30f;
        // The rooms round the box, as the table names them (its "room-<name>" sections), and as their row writes them after
        // OFF — the guitar's; none, no row (the bass's).
        ui::StringArray rooms, roomWords;
        // Where the response's line reddens with a pit, dB — the bass's 3; the guitar's 5, its speakers' own dips deeper
        // (the owner, 07.11). The table says the same (pit_red_db, synth::pitRedOf) for FIND and the tools.
        float pitRedDb = 3.0f;
    };

    static const Catalog& bassCatalog()
    {
        using P = ui::Point<float>;
        static const Catalog bass {
          {
            { "1x15-classic",   0, 0.61f, 0.56f, { P (0, 0.02f) }, 0.165f, true, P (0.22f, 0.20f), true, P (0, -0.22f), "re20", false },
            { "2x10-classic-tall",  0, 0.33f, 0.61f, { P (0, 0.14f), P (0, -0.14f) }, 0.105f, false, {}, false, {} },
            { "2x10-classic",  0, 0.61f, 0.46f, { P (-0.14f, 0), P (0.14f, 0) }, 0.105f, true, P (0, 0.16f), false, {} },
            { "4x10-classic",  0, 0.61f, 0.66f, { P (-0.145f, 0.145f), P (0.145f, 0.145f), P (-0.145f, -0.145f), P (0.145f, -0.145f) },
              0.105f, true, P (0, 0), false, {} },
            { "4x10-classic-tall", 0, 0.61f, 0.76f, { P (-0.145f, 0.19f), P (0.145f, 0.19f), P (-0.145f, -0.10f), P (0.145f, -0.10f) },
              0.105f, true, P (0, 0.045f), true, P (0, -0.31f) },
            { "6x10-classic", 0, 0.61f, 1.02f, { P (-0.145f, 0.32f), P (0.145f, 0.32f), P (-0.145f, 0.03f), P (0.145f, 0.03f),
                                                P (-0.145f, -0.26f), P (0.145f, -0.26f) },
              0.105f, true, P (0, 0.175f), true, P (0, -0.45f) },
            { "8x10-classic",    0, 0.66f, 1.22f, { P (-0.15f, 0.42f), P (0.15f, 0.42f), P (-0.15f, 0.14f), P (0.15f, 0.14f),
                                                P (-0.15f, -0.14f), P (0.15f, -0.14f), P (-0.15f, -0.42f), P (0.15f, -0.42f) },
              0.105f, true, P (0, 0.565f), false, {}, "re20", false },
            { "1x12-round",     1, 0.52f, 0.40f, { P (-0.09f, 0) }, 0.13f, true, P (0.17f, 0.08f), true, P (0.17f, -0.12f) },
            { "1x15-round",     1, 0.56f, 0.58f, { P (0, -0.04f) }, 0.165f, true, P (0.18f, 0.19f), true, P (0, -0.25f), "d112" },
            { "2x10-round",     1, 0.56f, 0.42f, { P (-0.135f, -0.02f), P (0.135f, -0.02f) }, 0.105f, true, P (0, 0.13f), true, P (0, -0.17f) },
            { "2x12-round",     1, 0.63f, 0.48f, { P (-0.155f, -0.02f), P (0.155f, -0.02f) }, 0.13f, true, P (0, 0.16f), true, P (0, -0.19f) },
            { "4x10-round",     1, 0.58f, 0.66f, { P (-0.14f, 0.145f), P (0.14f, 0.145f), P (-0.14f, -0.145f), P (0.14f, -0.145f) },
              0.105f, true, P (0, 0), true, P (0, -0.30f) },
            { "8x8-round",      1, 0.56f, 0.98f, { P (-0.13f, 0.33f), P (0.13f, 0.33f), P (-0.13f, 0.11f), P (0.13f, 0.11f),
                                                P (-0.13f, -0.11f), P (0.13f, -0.11f), P (-0.13f, -0.33f), P (0.13f, -0.33f) },
              0.085f, true, P (0, 0), true, P (0, -0.45f) },
            { "1x15-plain",     2, 0.60f, 0.66f, { P (0, 0.03f) }, 0.165f, false, {}, false, {}, "d112" },
            { "2x10-plain",     2, 0.60f, 0.40f, { P (-0.14f, 0), P (0.14f, 0) }, 0.105f, false, {}, false, {} },
            { "4x10-plain",     2, 0.64f, 0.64f, { P (-0.15f, 0.15f), P (0.15f, 0.15f), P (-0.15f, -0.15f), P (0.15f, -0.15f) },
              0.105f, false, {}, false, {} },
            { "8x10-plain",     2, 0.64f, 1.24f, { P (-0.15f, 0.43f), P (0.15f, 0.43f), P (-0.15f, 0.145f), P (0.15f, 0.145f),
                                                P (-0.15f, -0.145f), P (0.15f, -0.145f), P (-0.15f, -0.43f), P (0.15f, -0.43f) },
              0.105f, false, {}, false, {}, "re20" },
            { "4x10-neo",  3, 0.59f, 0.60f, { P (-0.14f, 0.16f), P (0.14f, 0.16f), P (-0.14f, -0.10f), P (0.14f, -0.10f) },
              0.105f, true, P (0, 0.03f), true, P (0, -0.265f) },
            { "2x10-neo",  3, 0.59f, 0.475f, { P (-0.14f, -0.03f), P (0.14f, -0.03f) }, 0.105f, true, P (0, 0.16f), true, P (0, -0.20f) },
            { "1x15-neo",  3, 0.59f, 0.65f, { P (0, -0.04f) }, 0.165f, true, P (0, 0.23f), false, {}, "d112" },
            { "4x6-neo",     3, 0.40f, 0.40f, { P (-0.09f, 0.07f), P (0.09f, 0.07f), P (-0.09f, -0.11f), P (0.09f, -0.11f) },
              0.06f, true, P (0, 0.165f), false, {} },
            { "2x12-neo",     3, 0.72f, 0.39f, { P (-0.17f, -0.01f), P (0.17f, -0.01f) }, 0.13f, true, P (0, 0.14f), false, {} },
            { "1x12-light", 4, 0.49f, 0.39f, { P (-0.06f, -0.01f) }, 0.13f, true, P (0.17f, 0.10f), true, P (0.17f, -0.13f) },
            { "1x15-light", 4, 0.49f, 0.49f, { P (0, -0.03f) }, 0.165f, true, P (0.17f, 0.19f), true, P (-0.17f, -0.21f), "d112" },
            { "2x10-light", 4, 0.49f, 0.53f, { P (-0.11f, 0.12f), P (0.11f, -0.11f) }, 0.105f, true, P (0.13f, 0.17f), true, P (-0.13f, -0.20f) },
            { "2x12-light", 4, 0.50f, 0.72f, { P (0, 0.13f), P (0, -0.19f) }, 0.13f, true, P (0, 0.31f), true, P (0.17f, -0.30f) },
            { "4x10-light", 4, 0.56f, 0.58f, { P (-0.13f, 0.15f), P (0.13f, 0.15f), P (-0.13f, -0.11f), P (0.13f, -0.11f) },
              0.105f, true, P (0, 0.02f), true, P (0, -0.26f) },
            { "2x15-light", 4, 0.49f, 0.87f, { P (0, 0.20f), P (0, -0.16f) }, 0.165f, true, P (0, 0.40f), true, P (0, -0.38f), "d112" },
          },
            // A box of each size, no maker's name over them (the owner, 05.11) — the small on the big, the tall ones
            // alone, the way a backline is stacked. Every size the table holds — 1x15, 1x12, 8x10, 4x10, 2x10, 6x10,
            // 2x12, 2x15, 4x6, 8x8 — the same for every bass of the family (the owner, 05.11).
          { { "1x15-classic", "1x12-round" }, { "8x10-classic" }, { "4x10-classic", "2x10-round" }, { "6x10-classic", "2x12-round" },
            { "2x15-light", "4x6-neo" }, { "8x8-round" } },
          { "md421", "sm57", "re20", "d112", "u87", "u47", "ribbon" },
          { "MD421", "SM57", "RE20", "D112", "U87", "U47", "RIBB" },
          { "2x10-classic-tall", "4x10-classic", "8x10-classic", "1x15-plain", "2x10-plain", "4x10-plain", "8x10-plain" },
          {} };
        return bass;
    }

    /** The catalog every panel draws from here on: before the first is made, and once — a program offers one set. */
    static void useCatalog (const Catalog& c) { current() = &c; }
    static const Catalog& catalog() { return *current(); }
private:
    static const Catalog*& current() { static const Catalog* c = &bassCatalog(); return c; }
public:

    static const std::vector<Cab>& cabs() { return catalog().cabs; }
    static const std::vector<ui::StringArray>& pile() { return catalog().pile; }
    static const ui::StringArray& mics() { return catalog().mics; }
    static const ui::StringArray& micWords() { return catalog().micWords; }

    /** A BOX AS THE PLAYER READS IT: its speakers and their size — "4x10" — and no maker's model (the owner, 05.11). The
        table's name stays the key a state keeps. */
    static ui::String wordOf (int cab)
    {
        const auto& c = cabs()[(size_t) cab];
        const int inches = c.radius > 0.15f ? 15 : c.radius > 0.12f ? 12 : c.radius > 0.095f ? 10 : c.radius > 0.07f ? 8 : 6;
        return ui::String ((int) c.drivers.size()) + "x" + ui::String (inches);
    }

    /** The boxes the table calls sealed (data/cabinets.txt, `alignment`). */
    static bool sealed (const ui::String& name) { return catalog().sealed.contains (name); }

    /** THE BOX'S BACK, as its switch is marked — a click each, the one it stands at in the block's orange; the index the
        engine's (synth::Back). A box comes with its own (ownBack) until one is turned, and that one stays from
        box to box; turned, a sealed one gets a port at its foot. */
    static constexpr const char* backWords[] { "CLOSED", "PORTED", "OPEN", "HALF" };
    static int backs() { return catalog().backRow.empty() ? 3 : (int) catalog().backRow.size(); }
    static int backAt (int cell) { return catalog().backRow.empty() ? cell : catalog().backRow[(size_t) cell]; }
    static int ownBackOf (const ui::String& name)
    {
        return sealed (name) ? 0 : catalog().open.contains (name) ? 2 : catalog().half.contains (name) ? 3 : 1;
    }
    static int ownBack (int cab) { return ownBackOf (cabs()[(size_t) cab].name); }
    static int backOf (int cab, const Setup& s) { return s.back >= 0 ? s.back : ownBack (cab); }

    /** THE BOX'S SPEAKER, the guitar's, as its row under the back is marked: the box's own (ownSpeaker) until one is
        turned, and that one stays from box to box, as the back does. -1 where the catalog offers none. */
    static bool hasSpeakers() { return ! catalog().speakers.isEmpty(); }
    static int ownSpeaker (int cab) { return cab < 0 ? -1 : catalog().speakers.indexOf (cabs()[(size_t) cab].speaker); }
    static int speakerOf (int cab, const Setup& s) { return s.speaker >= 0 ? s.speaker : ownSpeaker (cab); }

    /** Where a box's port is drawn: the table's, or the one a sealed box is given when turned ported — in the middle of
        its foot (synth::portAtFoot). */
    static ui::Point<float> portOf (const Cab& c) { return c.port ? c.portAt : ui::Point<float> (0.0f, -0.5f * c.h + 0.045f); }

    /** THE MICS A BOX IS PICKED WITH, `from` the one that was playing: where they stood — the same cap, the same
        centimetres, the way a cabinet is compared — only a mic never chosen following the box's own; the horn as it
        stood, unless the two boxes come with theirs otherwise (Cab::hornOn): the 1x15-classic's and the 8x10's go off, the
        next one's on — and the back as it stood: a box changed never changes it (the owner, 05.11), a back still the
        box's own becoming that one, said. A click in the pile picks by it. */
    static Setup pickedWith (int from, int to, Setup s)
    {
        if (s.back < 0 && from >= 0)
            s.back = ownBack (from);
        if (s.speaker < 0 && from >= 0)
            s.speaker = ownSpeaker (from);
        if (from < 0 || cabs()[(size_t) from].hornOn != cabs()[(size_t) to].hornOn)
            s.a.horn = cabs()[(size_t) to].hornOn;
        if (s.a.mic.isEmpty())
            s.a.mic = cabs()[(size_t) to].mic;
        return s;
    }

    /** THE STATE AND THE PANEL'S SETUP, one into the other (synth/State.h) — the horn and its step the panel keeps on
        A's spot. A state's box not in cabs() comes back as -1. */
    static std::pair<int, Setup> setupOf (const State& s)
    {
        const auto spot = [&s] (const Mic& m)
        { return Spot { (float) m.x, (float) m.y, (float) m.distance, s.horn, ui::String (m.model), (float) m.angle, (float) s.hornDb }; };
        return { indexOf (ui::String (s.box)), { spot (s.a), spot (s.b), (float) s.blend, s.align, s.invert, s.a.on, s.b.on, s.back,
                                                   (float) s.shiftMs, catalog().speakers.indexOf (ui::String (s.speaker)), s.wall,
                                                   ! s.cone.empty(), catalog().rooms.indexOf (ui::String (s.room)) } };
    }

    /** A STATE AS A LINE READS IT: its box's size, its back where turned from the box's own, and the mics heard —
        "4x10 · MD421", "4x10 PORTED · MD421+U87", B's alone with A off. */
    static ui::String labelOf (const State& s)
    {
        const int at = indexOf (ui::String (s.box));
        const auto word = [at] (const Mic& m)
        { return (m.model.empty() ? at >= 0 ? cabs()[(size_t) at].mic : ui::String() : ui::String (m.model)).toUpperCase(); };
        const int speaker = catalog().speakers.indexOf (ui::String (s.speaker));   // the guitar's, where turned from the box's own
        const auto box = at < 0 ? ui::String (s.box)
                       : wordOf (at) + (s.back >= 0 && s.back != ownBack (at) ? " " + ui::String (backWords[s.back]) : ui::String())
                             + (speaker >= 0 && speaker != ownSpeaker (at) ? " " + catalog().speakerWords[speaker] : ui::String())
                             + (s.wall ? " WALL" : "") + (s.cone.empty() ? ui::String() : " " + ui::String (s.cone).toUpperCase())
                             + (catalog().rooms.indexOf (ui::String (s.room)) >= 0
                                    ? " " + catalog().roomWords[catalog().rooms.indexOf (ui::String (s.room))] : ui::String());
        return box + ui::String::fromUTF8 (" \xc2\xb7 ")
             + (s.a.on ? word (s.a) : ui::String()) + (s.a.on && s.b.on ? "+" : "") + (s.b.on ? word (s.b) : ui::String());
    }

    static State stateOf (int cab, const Setup& su)
    {
        const auto mic = [] (const Spot& sp, bool on) { return Mic { on, sp.mic.toStdString(), sp.x, sp.y, sp.distance, sp.angle }; };
        State s;
        s.box = cabs()[(size_t) cab].name.toStdString();
        s.back = su.back;
        if (su.speaker >= 0 && su.speaker < catalog().speakers.size())
            s.speaker = catalog().speakers[su.speaker].toStdString();
        s.wall = su.wall;
        if (su.cone && ! catalog().coneVariant.isEmpty())
            s.cone = catalog().coneVariant.toStdString();
        if (su.room >= 0 && su.room < catalog().rooms.size())
            s.room = catalog().rooms[su.room].toStdString();
        s.horn = su.a.horn;
        s.hornDb = su.a.hornDb;
        s.a = mic (su.a, su.aOn);
        s.b = mic (su.b, su.bOn);
        s.blend = su.blend;
        s.align = su.align;
        s.invert = su.invert;
        s.shiftMs = su.shiftMs;
        return s;
    }

    static int indexOf (const ui::String& name)
    {
        for (int i = 0; i < (int) cabs().size(); ++i)
            if (cabs()[(size_t) i].name == name)
                return i;
        return -1;
    }

    /** What plays: a cabinet of cabs() and its mics — or none (an IR of the player's own, or the factory's). */
    void show (int cab, Setup setup)
    {
        if (dragging)
            return;   // the hand's, until it lets go
        chosen = cab;
        setup_ = setup;
        repaint();
    }

    std::function<void (int cab, Setup setup)> onSetup;

    /** The hand still moving: what it would sound like there, heard and not kept — onSetup follows when it lets go. */
    std::function<void (int cab, Setup setup)> onMove;

    /** CLOSE: whoever opened the panel takes it away. No button without it. */
    std::function<void()> onClose;

    /** DOWNLOAD IR: whoever opened the panel saves the cabinet that plays as a file — a button over the response's top
        right corner, its line pulsing gently. No button without it. */
    std::function<void()> onDownload;
    /** A host's format menu beside DOWNLOAD IR — rate, bits, length (the owner, 06.11) — its ▾ there only where this is
        set; and what the next download will be, said in a line under them. */
    std::function<void()> onDownloadFormat;
    ui::String downloadNote;
    /** Where the ▾ is, so a host anchors its menu there; empty where there is none. */
    ui::Rectangle<int> formatButton() const { return downloadFormatArea(); }

    /** WHERE A HOST'S HINT GOES (the site's rings over its live panel), in the panel's units, as it stands now: "cabinet",
        the boxes a click picks one in; "mic-a", mic A's glyph on the speaker as it leans — the speaker itself while A is
        not heard; "download", DOWNLOAD IR. Empty for another name, or for what the panel does not show. */
    ui::Rectangle<float> hintRect (const ui::String& name) const
    {
        if (name == "cabinet")
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& column : pile())
                for (const auto& box : column)
                    if (const int i = indexOf (box); i >= 0)
                    {
                        const auto r = boxOf (i);
                        x0 = ui::jmin (x0, r.getX()); y0 = ui::jmin (y0, r.getY());
                        x1 = ui::jmax (x1, r.getRight()); y1 = ui::jmax (y1, r.getBottom());
                    }
            return x1 > x0 ? ui::Rectangle<float> { x0, y0, x1 - x0, y1 - y0 } : ui::Rectangle<float> {};
        }
        if (name == "mic-a" && chosen >= 0)
        {
            const auto o = coneCentre();
            const float R = radius();
            if (! heard (0))
                return { o.x - R, o.y - R, 2.0f * R, 2.0f * R };
            const auto gl = glyphOf (mic (0), o, R, 1.0f);
            const float c = std::cos (gl.lean), sn = std::sin (gl.lean);
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            const auto& g = gl.box;
            for (const auto& u : { ui::Point<float> (g.getX(), g.getY()), ui::Point<float> (g.getRight(), g.getY()),
                                   ui::Point<float> (g.getX(), g.getBottom()), ui::Point<float> (g.getRight(), g.getBottom()) })
            {
                const auto d = u - gl.at;   // as drawn: the upright glyph turned by its lean about its capsule
                const float x = gl.at.x + c * d.x - sn * d.y, y = gl.at.y + sn * d.x + c * d.y;
                x0 = ui::jmin (x0, x); y0 = ui::jmin (y0, y); x1 = ui::jmax (x1, x); y1 = ui::jmax (y1, y);
            }
            return { x0, y0, x1 - x0, y1 - y0 };
        }
        if (name == "download")
            return downloadArea().toFloat();
        return {};
    }

    /** DEFAULT as a button in the foot — or not, where the host offers it another way (the app's preset menu). With
        neither DEFAULT nor CLOSE the foot's row goes, and the panel is footRowH shorter. */
    bool showDefault = true;
    static constexpr int footRowH = 26;

    /** FIND, asked of whoever opened the panel: with both mics heard where B combs least with A (synth::quietPlaceForB),
        with one where that one stands without a pit, about where it is (synth::smoothPlaceForMic, the owner, 05.11) — B
        is taken up by its column, and placed then on its own. No button without it. The search draws itself as it goes
        (showSearch) and lets go when it is done (endSearch). */
    std::function<void (int cab, Setup setup)> onFind;

    /** A place the search judged, across the first speaker in radii, and how much B there cancels against A — 0 none,
        the more negative the more: a dot on the speaker, green to red, the best of its distances. Message thread. */
    void showSearch (float x, float y, float score)
    {
        if (! searching)
        {
            searchDots.clear();
            searching = true;
        }
        searchFade = 1.0f;
        for (auto& d : searchDots)
            if (std::abs (d.x - x) < 1.0e-3f && std::abs (d.y - y) < 1.0e-3f)
            {
                d.score = ui::jmax (d.score, score);
                repaint();
                return;
            }
        searchDots.push_back ({ x, y, score });
        repaint();
    }

    /** ...and the search done: the map stays a moment, then fades. */
    void endSearch()
    {
        searching = false;
        searchEnded = -1.0;    // from the next tick: a second and a half held, then a second's fade, however often it comes
    }

    /** THE RESPONSE of what plays, in dB at responseHz — `now` the cabinet heard, `before` what was kept before it
        (empty, none): each level to its own middle, so the two read as shapes; and with two mics heard `comb`, where
        they meet: dB above nought where they add in phase, below where they cancel (empty, one mic). */
    static constexpr int responsePoints = synth::responsePoints;
    static float responseHz (int i) { return synth::responseHz (i); }

    void showResponse (std::vector<float> now, std::vector<float> before, std::vector<float> comb = {})
    {
        responseNow = std::move (now);
        responseBefore = std::move (before);
        responseComb = std::move (comb);
        responseBad = synth::pitsOf (responseNow);
        for (auto& b : responseBad)   // a pit judged from where this catalog's line reddens: the guitar's 5 dB, the bass's 3
            b = ui::jmax (0.0f, b - (catalog().pitRedDb - 3.0f));
        if (responseComb.size() == responseBad.size())
            for (size_t i = 0; i < responseBad.size(); ++i)
                responseBad[i] = ui::jmax (responseBad[i], -responseComb[i]);
        repaint (responseArea());
    }


    void paint (ui::Graphics& g) const
    {
        paintPile (g);
        g.setColour (ui::look().hair);
        const float x = (float) leftW + 0.5f * gutter;
        g.fillRect (ui::Rectangle<float> (x, 0.0f, 1.0f, (float) bodyH()));
        g.fillRect (ui::Rectangle<float> (x, (float) (getHeight() - footHeight()), 1.0f, (float) footHeight()));
        paintSpeaker (g);
        paintResponse (g);
    }

    void mouseMove (const ui::MouseEvent& e)
    {
        const auto was = hot;
        hot = hotAt (e.position);
        if (hot != was)
            repaint();
    }

    void mouseExit (const ui::MouseEvent&)
    {
        hot = {};
        repaint();
    }

    void mouseDown (const ui::MouseEvent& e)
    {
        if (defaultArmed && ! defaultArea().contains (e.getPosition()))
            disarmDefault();   // a click anywhere else: DEFAULT's question withdrawn
        press (hotAt (e.position), e);
    }

    void mouseDrag (const ui::MouseEvent& e)
    {
        if (chosen < 0 || drag == Drag::none)
            return;
        auto& m = mic (dragMic);
        if (drag == Drag::spot)
        {
            const auto o = coneCentre();
            const float R = radius();
            auto p = (e.position + grabOffset - o) / R;   // the capsule where it was against the hand: no jump to it
            micMoved = true;
            const float len = p.getDistanceFromOrigin();
            if (len > maxReach)
                p *= maxReach / len;
            m.x = p.x;
            m.y = -p.y;
        }
        else if (drag == Drag::distance)
        {
            const auto t = dragTrack;
            m.distance = minCm * std::pow (maxCm() / minCm, ui::jlimit (0.0f, 1.0f, (t.getBottom() - e.position.y) / t.getHeight())) / 100.0f;
        }
        else if (drag == Drag::angle)
            m.angle = maxAngle * along (angleTrack (dragMic), e.position.x);
        else if (drag == Drag::shift)
        {
            const float v = (2.0f * along (timeTrack(), e.position.x) - 1.0f) * maxShiftMs;
            setup_.shiftMs = std::abs (v) < 0.03f * maxShiftMs ? 0.0f : v;   // a little stick at nought, where ALIGN leaves B
        }
        else if (drag == Drag::blend)
            setup_.blend = along (blendTrack(), e.position.x);
        repaint();
        if (onMove != nullptr)
            onMove (chosen, setup_);
    }

    void mouseDoubleClick (const ui::MouseEvent& e)
    {
        if (chosen >= 0 && timeTrack().expanded (2.0f, 10.0f).contains (e.position))
        {
            setup_.shiftMs = 0.0f;   // TIME back to where ALIGN leaves B
            send();
        }
        else if (chosen >= 0 && bothHeard() && blendTrack().expanded (8.0f, 8.0f).contains (e.position))
        {
            setup_.blend = 0.5f;     // BLEND back to the middle, the two even (the owner, 05.11)
            send();
        }
    }

    void mouseUp (const ui::MouseEvent& e)
    {
        if (drag != Drag::none)
        {
            // A mic taken and let go where it stood changes nothing: nothing kept, the ghost of before left as it was.
            const bool still = drag == Drag::spot && ! micMoved;
            drag = Drag::none;
            dragging = false;
            if (still)
                repaint();
            else
                send();
        }
        hot = hotAt (e.position);
    }

private:
    static constexpr float maxReach = 1.1f;    // how far from the cap the mic may go, in radii: just past the rim, in sight
    static constexpr float minCm = 1.0f, maxAngle = 60.0f;
    static float maxCm() { return catalog().farthestCm; }
    static constexpr int leftW = 460, gutter = 14;   // the pile's half; the hair between the halves
    static constexpr int footH = footRowH, responseH = 208;  // the foot's row under it all; the response's band over it
    int footHeight() const { return onClose != nullptr || showDefault ? footH : 0; }

    /** Down to the response's band: the pile, the columns and the speaker over it, the feet under it. */
    int bodyH() const { return getHeight() - footHeight() - responseH; }

    enum class Drag { none, spot, distance, angle, blend, shift };

    /** WHAT A CLICK AT A POINT WORKS — the one thing both the hand's light and its press go by (hotAt), so what is lit is
        what a click does. */
    enum class Part { none, download, downloadFormat, back, speaker, speakerCone, wall, room, cab, cabHorn, horn, distance, place, micRow, angle, shift, mic, close, reset, find,
                      align, invert, blend };
    struct Hot
    {
        Part part = Part::none;
        int m = -1, i = -1;   // the mic's column; the box, the back, the step, the place or the row
        bool operator== (const Hot& o) const { return part == o.part && m == o.m && i == o.i; }
        bool operator!= (const Hot& o) const { return ! (*this == o); }
    };

    int chosen = -1;
    Hot hot;                          // under the hand while it moves with no button down
    ui::Point<float> grabOffset;      // a mic taken: its capsule against the hand, kept while it drags
    bool micMoved = false;            // ...and moved since, or let go where it stood
    bool isHot (Part p, int m = -1, int i = -1) const { return hot == Hot { p, m, i }; }
    int hoverCab() const { return hot.part == Part::cab || hot.part == Part::cabHorn ? hot.i : -1; }
    struct SearchDot { float x, y, score; };
    std::vector<SearchDot> searchDots;  // the search's map (showSearch), fading once it is done
    bool searching = false;
    float searchFade = 0.0f;
    double searchEnded = 0;            // the host's clock when it was done; -1 until the tick after

    void timerCallback()
    {
        if (const auto d = downloadArea(); ! d.isEmpty())
            repaint (d);
        if (searchFade <= 0.0f || searching)
            return;
        if (searchEnded < 0.0)
            searchEnded = clock;
        const double since = clock - searchEnded;
        if (since < 1.5)
            return;
        searchFade = (float) (1.0 - (since - 1.5));
        if (searchFade <= 0.0f)
        {
            searchFade = 0.0f;
            searchDots.clear();
        }
        repaint();
    }
    ui::Rectangle<float> dragTrack;   // the distance track a drag began on: the column's, or the one out on the speaker
    Setup setup_;
    bool dragging = false;
    Drag drag = Drag::none;
    int dragMic = 0;
    bool defaultArmed = false;   // DEFAULT asked SURE? and waits for its second click
    double armedUntil = 0;       // ...for three seconds
    int wide = panelW, high = panelH;
    double clock = 0;            // seconds, the host's (tick)

    void repaint() const { if (onRepaint != nullptr) onRepaint(); }
    void repaint (ui::Rectangle<int> r) const { if (onRepaintArea != nullptr) onRepaintArea (r); else repaint(); }

    void disarmDefault()
    {
        if (! defaultArmed)
            return;
        defaultArmed = false;
        repaint();
    }

    Spot& mic (int m) { return m == 0 ? setup_.a : setup_.b; }
    const Spot& mic (int m) const { return m == 0 ? setup_.a : setup_.b; }

    void send()
    {
        repaint();
        if (onSetup != nullptr && chosen >= 0)
            onSetup (chosen, setup_);
    }

    void toggleHorn()
    {
        setup_.a.horn = ! setup_.a.horn;
        send();
    }

    // ---------------------------------------------------------------- the pile
    // Over it the chosen box's back, in the room the pile leaves there (the owner, 05.11); under it the box's size.
    static constexpr int backTop = 6, backIconH = 40, backWordH = 16, nameH = footH;
    static constexpr int pileTop = backTop + backIconH + 2 + backWordH + 6;
    // ...and under the back, where the catalog offers speakers, their row: the pile a row lower.
    static constexpr int speakerRowH = 24;
    static int pileTopNow() { return pileTop + (hasSpeakers() ? speakerRowH : 0) + (hasRooms() ? speakerRowH : 0); }

    ui::Rectangle<int> pileArea() const { return { 0, pileTopNow(), leftW, bodyH() - pileTopNow() - 2 }; }

    /** The line under the pile: DOWNLOAD IR at its right. */
    ui::Rectangle<int> nameRow() const { return { 0, getHeight() - nameH + 4, leftW, nameH - 4 }; }

    ui::Rectangle<int> downloadArea() const
    {
        if (onDownload == nullptr || chosen < 0)
            return {};
        const auto plot = responsePlot().toNearestInt();
        const int w = wordW ("DOWNLOAD IR") + 14, menu = onDownloadFormat != nullptr ? 24 + 4 : 0;
        return { plot.getRight() - w - menu - 6, plot.getY() + 6, w, 24 };
    }

    /** ...and its format's ▾ at its right, where a host offers one. */
    ui::Rectangle<int> downloadFormatArea() const
    {
        const auto d = downloadArea();
        if (onDownloadFormat == nullptr || d.isEmpty())
            return {};
        return { d.getRight() + 4, d.getY(), 24, d.getHeight() };
    }

    /** THE BACK over the pile, centred: BACK, then a cell each — the box cut through from the side (paintBack), its word
        under it. */
    static constexpr float backCellW = 84.0f, backCellGap = 18.0f, backLabelGap = 16.0f;
    static float backGap() { return backs() > 3 ? 8.0f : backCellGap; }   // four cells closer, to stay in the half

    ui::Rectangle<float> backWordArea() const
    {
        const float label = ui::look().trackedWidth ("BACK", ui::look().font (13.0f), 0.08f) + 4.0f;
        const float total = label + backLabelGap + (float) backs() * backCellW + (float) (backs() - 1) * backGap();
        return { 0.5f * ((float) leftW - total), (float) backTop, label, (float) backIconH };
    }

    /** WALL, the guitar's: in the back's label column, on its words' line, under BACK. */
    ui::Rectangle<float> wallArea() const
    {
        const auto label = backWordArea();
        return { label.getX(), (float) (backTop + backIconH + 2), label.getWidth(), (float) backWordH };
    }

    ui::Rectangle<float> backCell (int i) const
    {
        const float x = backWordArea().getRight() + backLabelGap + (float) i * (backCellW + backGap());
        return { x, (float) backTop, backCellW, (float) (backIconH + 2 + backWordH) };
    }

    /** THE SPEAKERS under the back, the guitar's: SPEAKER, then a word each, the row centred as the back's is. */
    static constexpr float speakerGap = 12.0f;

    ui::Rectangle<float> speakerWordArea() const
    {
        const auto font = ui::look().font (13.0f);
        float total = ui::look().trackedWidth ("SPEAKER", font, 0.08f) + 4.0f + backLabelGap;
        for (const auto& w : catalog().speakerWords)
            total += ui::look().trackedWidth (w, font, 0.06f) + speakerGap;
        total -= speakerGap;
        return { 0.5f * ((float) leftW - total), (float) pileTop - 2.0f, ui::look().trackedWidth ("SPEAKER", font, 0.08f) + 4.0f,
                 (float) speakerRowH - 4.0f };
    }

    ui::Rectangle<float> speakerCell (int i) const
    {
        const auto font = ui::look().font (13.0f);
        float x = speakerWordArea().getRight() + backLabelGap;
        for (int k = 0; k < i; ++k)
            x += ui::look().trackedWidth (catalog().speakerWords[k], font, 0.06f) + speakerGap;
        return { x, speakerWordArea().getY(), ui::look().trackedWidth (catalog().speakerWords[i], font, 0.06f),
                 speakerWordArea().getHeight() };
    }

    /** THE ROOM under the speakers, the guitar's: ROOM in SPEAKER's column, then OFF and a word a room, from under the
        first speaker's word. Cell -1 is OFF. */
    static bool hasRooms() { return ! catalog().rooms.isEmpty(); }
    ui::String roomWord (int i) const { return i < 0 ? ui::String ("OFF") : catalog().roomWords[i]; }

    ui::Rectangle<float> roomWordArea() const
    {
        return speakerWordArea().translated (0.0f, (float) speakerRowH);
    }

    ui::Rectangle<float> roomCell (int i) const
    {
        const auto font = ui::look().font (13.0f);
        float x = roomWordArea().getRight() + backLabelGap;
        for (int k = -1; k < i; ++k)
            x += ui::look().trackedWidth (roomWord (k), font, 0.06f) + speakerGap;
        return { x, roomWordArea().getY(), ui::look().trackedWidth (roomWord (i), font, 0.06f), roomWordArea().getHeight() };
    }

    /** A BOX CUT THROUGH FROM THE SIDE, as its back is: the baffle at the left with the speaker on it in profile, its cone
        narrowing in to the magnet, the top and the bottom, and at the right the back — whole (CLOSED); whole, the port's
        tube in from the baffle under the speaker (PORTED); gone but for a lip at the top, the sound out of it (OPEN); its
        upper half, the sound out of the lower (HALF, the guitar's). */
    static void paintBack (ui::Graphics& g, int kind, ui::Rectangle<float> b, ui::Colour colour)
    {
        const float x0 = b.getX(), x1 = b.getRight(), y0 = b.getY(), y1 = b.getBottom(), w = b.getWidth(), h = b.getHeight();
        const float line = 1.2f, baffle = 1.8f;
        g.setColour (colour);
        g.drawLine (x0, y0, x1, y0, line);
        g.drawLine (x0, y1, x1, y1, line);

        const float portTop = y1 - 0.30f * h, portBottom = y1 - 0.12f * h;
        if (kind == 1)
        {
            g.drawLine (x0, y0, x0, portTop, baffle);
            g.drawLine (x0, portBottom, x0, y1, baffle);
            g.drawLine (x0, portTop, x0 + 0.45f * w, portTop, line);
            g.drawLine (x0, portBottom, x0 + 0.45f * w, portBottom, line);
        }
        else
            g.drawLine (x0, y0, x0, y1, baffle);

        if (kind == 3)
        {
            g.drawLine (x1, y0, x1, y0 + 0.5f * h, line);
            ui::Path out;
            out.addCentredArc (x1 - 1.0f, y0 + 0.78f * h, 5.0f, 5.0f, 0.0f, 0.5f * ui::MathConstants<float>::pi - 0.7f,
                               0.5f * ui::MathConstants<float>::pi + 0.7f, true);
            g.strokePath (out, ui::PathStrokeType (line));
        }
        else if (kind != 2)
            g.drawLine (x1, y0, x1, y1, line);
        else
        {
            g.drawLine (x1, y0, x1, y0 + 0.22f * h, line);
            ui::Path out;
            for (const float r : { 5.0f, 10.0f })
                out.addCentredArc (x1 - 1.0f, y0 + 0.62f * h, r, r, 0.0f, 0.5f * ui::MathConstants<float>::pi - 0.7f,
                                   0.5f * ui::MathConstants<float>::pi + 0.7f, true);
            g.strokePath (out, ui::PathStrokeType (line));
        }

        const float cy = kind == 1 ? y0 + 0.40f * h : b.getCentreY(), half = 0.27f * h;
        ui::Path cone;
        cone.startNewSubPath (x0, cy - half);
        cone.lineTo (x0 + 0.22f * w, cy - 0.09f * h);
        cone.lineTo (x0 + 0.22f * w, cy + 0.09f * h);
        cone.lineTo (x0, cy + half);
        g.strokePath (cone, ui::PathStrokeType (line));
        g.fillRect (ui::Rectangle<float> (0.10f * w, 0.18f * h).withPosition (x0 + 0.22f * w, cy - 0.09f * h));
    }

    /** The chosen box's horn over its speaker, big, always in the scene's top right corner, wherever it stands on the
        box — a horn that moves with the box jumps under the eye (the owner, 05.11); none where the box has none. */
    ui::Rectangle<float> hornCorner() const
    {
        if (chosen < 0 || ! cabs()[(size_t) chosen].horn)
            return {};
        const auto sc = scene();
        const float w = 0.30f * sc.getWidth(), h = 0.54f * w;
        return { sc.getRight() - 6.0f - w, sc.getY() + 6.0f, w, h };
    }

    /** HORN's attenuator, as a cabinet's switch is marked: OFF, -12, -6, 0 — a click each. */
    static constexpr const char* hornWords[] { "OFF", "-12", "-6", "0" };
    static constexpr float hornSteps[] { 0.0f, -12.0f, -6.0f, 0.0f };   // dB; OFF takes the horn out instead
    static constexpr float hornGap = 8.0f;

    int hornStep() const
    {
        if (! setup_.a.horn)
            return 0;
        return setup_.a.hornDb < -9.0f ? 1 : setup_.a.hornDb < -3.0f ? 2 : 3;
    }

    /** Left of the horn, two lines: HORN, then its steps — flush to the horn's side. */
    ui::Rectangle<float> hornBlock() const
    {
        const auto hc = hornCorner();
        const auto font = ui::look().font (13.0f);
        float w = 3.0f * hornGap;
        for (const auto* word : hornWords)
            w += ui::look().trackedWidth (word, font, 0.06f) + 4.0f;
        return { hc.getX() - 8.0f - w, hc.getCentreY() - 15.0f, w, 30.0f };
    }

    ui::Rectangle<float> hornWordArea() const { return hornBlock().withHeight (14.0f); }

    ui::Rectangle<float> hornStepArea (int i) const
    {
        const auto font = ui::look().font (13.0f);
        const auto block = hornBlock();
        float x = block.getX();
        for (int k = 0; k < i; ++k)
            x += ui::look().trackedWidth (hornWords[k], font, 0.06f) + 4.0f + hornGap;
        return { x, block.getBottom() - 14.0f, ui::look().trackedWidth (hornWords[i], font, 0.06f) + 4.0f, 14.0f };
    }

    /** The speaker's centre: the scene's, moved down under a horn, so the horn has its room. */
    ui::Point<float> coneCentre() const
    {
        const auto sc = scene();
        if (chosen < 0 || ! cabs()[(size_t) chosen].horn)
            return sc.getCentre();
        const float shift = ui::jlimit (0.0f, 28.0f, 0.5f * (sc.getHeight() - 2.12f * radius()) - 4.0f);
        return sc.getCentre().translated (0.0f, shift);
    }

    static constexpr float columnGapM = 0.04f;   // between a pile's columns, m
    static constexpr float stackGapPx = 2.0f;    // between a box and the one on it

    /** One scale for the whole pile, so a box is as big as itself against the others: its width and its tallest column
        fitted to the area. */
    float pileScale() const
    {
        const auto area = pileArea().toFloat();
        float w = 0, h = 0;
        for (const auto& column : pile())
        {
            float cw = 0, ch = 0;
            for (const auto& name : column)
            {
                const auto& c = cabs()[(size_t) indexOf (name)];
                cw = ui::jmax (cw, c.w);
                ch += c.h;
            }
            w += cw;
            h = ui::jmax (h, ch);
        }
        w += columnGapM * (float) (pile().size() - 1);
        return ui::jmin ((area.getWidth() - 8.0f) / w, (area.getHeight() - 8.0f) / h);
    }

    /** Each box, to scale, standing as pile() stacks them, the pile centred on its floor. */
    std::vector<std::pair<int, ui::Rectangle<float>>> layOut() const
    {
        const auto area = pileArea().toFloat();
        const float s = pileScale();
        const auto& columns = pile();

        float width = columnGapM * (float) (columns.size() - 1);
        for (const auto& column : columns)
        {
            float cw = 0;
            for (const auto& name : column)
                cw = ui::jmax (cw, cabs()[(size_t) indexOf (name)].w);
            width += cw;
        }

        std::vector<std::pair<int, ui::Rectangle<float>>> out;
        float x = area.getCentreX() - 0.5f * width * s;
        const float floor = area.getBottom() - 2.0f;
        for (const auto& column : columns)
        {
            float cw = 0;
            for (const auto& name : column)
                cw = ui::jmax (cw, cabs()[(size_t) indexOf (name)].w);
            float bottom = floor;
            for (const auto& name : column)
            {
                const int i = indexOf (name);
                const auto& c = cabs()[(size_t) i];
                out.push_back ({ i, { x + 0.5f * (cw - c.w) * s, bottom - c.h * s, c.w * s, c.h * s } });
                bottom -= c.h * s + stackGapPx;
            }
            x += (cw + columnGapM) * s;
        }
        return out;
    }

    ui::Rectangle<float> boxOf (int index) const
    {
        for (const auto& [i, box] : layOut())
            if (i == index)
                return box;
        return {};
    }

    int cabAt (ui::Point<float> p) const
    {
        for (const auto& [i, box] : layOut())
            if (box.expanded (1.0f).contains (p))
                return i;
        return -1;
    }

    static ui::Point<float> onBox (ui::Rectangle<float> box, float s, ui::Point<float> p)
    {
        return { box.getCentreX() + p.x * s, box.getCentreY() - p.y * s };
    }

    /** What a click at `p` works, in the order a click looks for it. */
    Hot hotAt (ui::Point<float> p) const
    {
        const auto pos = p.roundToInt();
        if (downloadArea().contains (pos))
            return { Part::download };
        if (downloadFormatArea().contains (pos))
            return { Part::downloadFormat };

        if (p.x < (float) leftW)
        {
            // The chosen box's back, a cell each.
            if (chosen >= 0)
                for (int i = 0; i < backs(); ++i)
                    if (backCell (i).expanded (backGap() / 2, 2.0f).contains (p))
                        return { Part::back, -1, i };
            // ...the wall behind it, where the catalog has the switch...
            if (chosen >= 0 && catalog().wallSwitch && wallArea().expanded (4.0f, 2.0f).contains (p))
                return { Part::wall };
            // ...and its speaker, where it has a row of them — SPEAKER itself the cone's switch, where there is a variant.
            if (chosen >= 0 && hasSpeakers() && catalog().coneVariant.isNotEmpty() && speakerWordArea().expanded (4.0f, 2.0f).contains (p))
                return { Part::speakerCone };
            if (chosen >= 0 && hasSpeakers())
                for (int i = 0; i < catalog().speakers.size(); ++i)
                    if (speakerCell (i).expanded (speakerGap / 2, 2.0f).contains (p))
                        return { Part::speaker, -1, i };
            // ...and the room round it, OFF first.
            if (chosen >= 0 && hasRooms())
                for (int i = -1; i < catalog().rooms.size(); ++i)
                    if (roomCell (i).expanded (speakerGap / 2, 2.0f).contains (p))
                        return { Part::room, -1, i };

            const int at = cabAt (p);
            if (at < 0)
                return {};
            // The chosen box again: its horn, where it has one.
            const auto& cab = cabs()[(size_t) at];
            const auto box = boxOf (at);
            const float s = box.getWidth() / cab.w;
            if (at == chosen && cab.horn && p.getDistanceFrom (onBox (box, s, cab.hornAt)) < ui::jmax (7.0f, 0.06f * s))
                return { Part::cabHorn, -1, at };
            return { Part::cab, -1, at };
        }
        if (chosen < 0)
            return {};

        if (! hornCorner().isEmpty())
            for (int i = 0; i < 4; ++i)
                if (hornStepArea (i).expanded (hornGap / 2, 3.0f).contains (p))
                    return { Part::horn, -1, i };

        for (int m = 0; m < 2; ++m)
            if (distArea (m).contains (pos))
                return { Part::distance, m };

        // FIND, in A's column under its angle: before the column, which keeps every other click in it for its own.
        if (findArea().contains (pos))
            return { Part::find };

        for (int m = 0; m < 2; ++m)
        {
            if (! column (m).contains (pos))
                continue;
            for (int i = 0; i < 4; ++i)
                if (placeArea (m, i).contains (pos))
                    return { Part::place, m, i };
            for (int i = 0; i < mics().size(); ++i)
                if (micRow (m, i).contains (pos))
                    return { Part::micRow, m, i };
            if (angleTrack (m).expanded (2.0f, 10.0f).contains (p))
                return { Part::angle, m };
            if (m == 1 && timeTrack().expanded (2.0f, 10.0f).contains (p))
                return { Part::shift, 1 };
            return {};
        }

        if (onClose != nullptr && closeArea().contains (pos)) return { Part::close };
        if (defaultArea().contains (pos))                     return { Part::reset };
        if (alignArea().expanded (4, 2).contains (pos))       return { Part::align };
        if (invertArea().expanded (4, 2).contains (pos))      return { Part::invert };
        if (bothHeard() && blendTrack().expanded (8.0f, 8.0f).contains (p)) return { Part::blend };
        if (const int m = micAt (p); m >= 0)                  return { Part::mic, m };
        return {};
    }

    /** A drag begun: the hand's until it lets go, heard as it goes. */
    void grab (Drag d, int m, const ui::MouseEvent& e)
    {
        drag = d;
        dragMic = m;
        dragging = true;
        mouseDrag (e);
    }

    void press (Hot h, const ui::MouseEvent& e)
    {
        switch (h.part)
        {
            case Part::none:
                return;

            case Part::download:
                onDownload();
                return;

            case Part::downloadFormat:
                onDownloadFormat();
                return;

            // The back: kept as turned, its own one too, so every box after comes with it.
            case Part::back:
                setup_.back = backAt (h.i);
                send();
                return;

            // The speakers' other cone, on and off; kept from box to box as the speaker is.
            case Part::speakerCone:
                setup_.cone = ! setup_.cone;
                send();
                return;

            // The wall behind the box, on and off; kept from box to box as the back is.
            case Part::wall:
                setup_.wall = ! setup_.wall;
                send();
                return;

            // The speaker, kept as the back is.
            case Part::speaker:
                setup_.speaker = h.i;
                send();
                return;

            // The room round the box, or none; kept from box to box as the back is.
            case Part::room:
                setup_.room = h.i;
                send();
                return;

            case Part::cab:
                if (h.i != chosen)
                {
                    setup_ = pickedWith (chosen, h.i, setup_);
                    chosen = h.i;
                    send();
                }
                return;

            case Part::cabHorn:
                toggleHorn();
                return;

            case Part::horn:
                setup_.a.horn = h.i > 0;
                if (h.i > 0)
                    setup_.a.hornDb = hornSteps[h.i];
                send();
                return;

            case Part::distance:
                takeUp (h.m);
                dragTrack = distTrack (h.m);
                grab (Drag::distance, h.m, e);
                return;

            case Part::place:
                putAt (h.m, placeAt (h.i));
                return;

            case Part::micRow:
            {
                // The lit one clicked again, with both mics heard: that mic goes off where it stands, the other stays
                // where it is, BLEND too. The last mic never goes.
                const auto& own = mic (h.m).mic.isNotEmpty() ? mic (h.m).mic : cabs()[(size_t) chosen].mic;
                if (bothHeard() && mics()[h.i] == own)
                    (h.m == 0 ? setup_.aOn : setup_.bOn) = false;
                else
                {
                    takeUp (h.m);
                    mic (h.m).mic = mics()[h.i];
                }
                send();
                return;
            }

            case Part::angle:
                takeUp (h.m);
                grab (Drag::angle, h.m, e);
                return;

            case Part::shift:
                takeUp (1);
                grab (Drag::shift, 1, e);
                return;

            // A mic taken by any part of it moves with the hand from there — its capsule does not jump under it.
            case Part::mic:
                grabOffset = spotOf (h.m) - e.position;
                micMoved = false;
                drag = Drag::spot;
                dragMic = h.m;
                dragging = true;
                repaint();
                return;

            case Part::close:
                onClose();
                return;

            // DEFAULT asks first: the first click turns it into SURE? for three seconds, the second resets.
            case Part::reset:
                if (! defaultArmed)
                {
                    defaultArmed = true;
                    armedUntil = clock + 3.0;
                    repaint();
                    return;
                }
                disarmDefault();
                setup_ = {};
                setup_.a.mic = cabs()[(size_t) chosen].mic;
                setup_.a.horn = cabs()[(size_t) chosen].hornOn;
                send();
                return;

            // With both mics heard B is looked for, with one that one (the host's: synth::BSearch, synth::MicSearch).
            case Part::find:
                onFind (chosen, setup_);
                return;

            case Part::align:
                setup_.align = ! setup_.align;
                send();
                return;

            case Part::invert:
                setup_.invert = ! setup_.invert;
                send();
                return;

            case Part::blend:
                grab (Drag::blend, dragMic, e);
                return;
        }
    }

    /** A HF horn as it faces the player: its mouth a rounded rectangle, its walls narrowing to the throat, the driver a
        dark slot at the bottom of it — `s` px a metre; dim when it is turned off. */
    static void paintHorn (ui::Graphics& g, ui::Point<float> c, float s, ui::Colour colour)
    {
        // A tunnel: the mouth, the flare narrowing inside it a little above the centre, the throat dark at its end.
        const auto mouth  = ui::Rectangle<float> (0.12f * s, 0.065f * s).withCentre (c);
        const auto flare  = ui::Rectangle<float> (0.075f * s, 0.038f * s).withCentre (c.translated (0, -0.004f * s));
        const auto throat = ui::Rectangle<float> (0.03f * s, 0.016f * s).withCentre (c.translated (0, -0.007f * s));
        const float line = ui::jlimit (0.7f, 1.2f, 0.01f * s);
        g.setColour (ui::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (mouth, 0.014f * s);
        g.setColour (colour.withMultipliedAlpha (0.10f));
        g.fillRoundedRectangle (mouth.reduced (line), 0.012f * s);
        g.setColour (ui::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (flare, 0.009f * s);
        g.setColour (ui::Colours::black);
        g.fillRoundedRectangle (throat, 0.005f * s);
        g.setColour (colour.withMultipliedAlpha (0.45f));
        g.drawRoundedRectangle (flare, 0.009f * s, line * 0.7f);
        g.setColour (colour.withMultipliedAlpha (0.8f));
        g.drawRoundedRectangle (throat, 0.005f * s, line * 0.7f);
        g.setColour (colour);
        g.drawRoundedRectangle (mouth, 0.014f * s, line);
    }

    void paintPile (ui::Graphics& g) const
    {
        const auto font = ui::look().font (13.0f);
        const auto area = pileArea();
        g.setColour (ui::look().hair);
        g.fillRect (ui::Rectangle<float> ((float) area.getX(), (float) area.getBottom() - 1.0f, (float) area.getWidth(), 1.0f));
        // The back over the pile kept apart from it, as the halves are, by a hair across up to theirs (the owner, 05.11).
        g.fillRect (ui::Rectangle<float> (0.0f, (float) pileTopNow() - 3.0f, (float) leftW + 0.5f * gutter, 1.0f));

        for (const auto& [index, box] : layOut())
            paintCab (g, cabs()[(size_t) index], box, setup_, index == chosen, index == hoverCab(), 1.0f, false,
                      isHot (Part::cabHorn, -1, index));

        // In the pile at its top left, big: the box under the hand, else the chosen one — its letters' tops level with the
        // pile's top, the 2x12's (the owner, 05.11).
        const int named = hoverCab() >= 0 ? hoverCab() : chosen;
        if (named >= 0)
        {
            const auto big = ui::look().font (24.0f);
            float left = (float) area.getRight(), top = (float) area.getBottom();
            for (const auto& [index, box] : layOut())
            {
                left = ui::jmin (left, box.getX());
                top = ui::jmin (top, box.getY());
            }
            const float capGap = big.getAscent() - 0.72f * big.getHeight();   // the ascent over the capitals' tops
            g.setColour (named == chosen ? ui::look().tx : ui::look().txDim);
            ui::look().drawTracked (g, wordOf (named), { left, top - capGap, (float) leftW - left, big.getHeight() }, big, 0.06f,
                                ui::Justification::topLeft);
        }

        // Over the pile the chosen box's back: BACK, then each drawn with its word, the one it stands at in the block's
        // orange line.
        if (chosen >= 0)
        {
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, "BACK", backWordArea(), font, 0.08f, ui::Justification::centredLeft);
            for (int i = 0; i < backs(); ++i)
            {
                const int kind = backAt (i);
                const bool on = kind == backOf (chosen, setup_), over = ! on && isHot (Part::back, -1, i);
                auto cell = backCell (i);
                if (over)   // framed as a box under the hand is
                {
                    g.setColour (ui::look().violet.withAlpha (0.7f));
                    g.drawRoundedRectangle (cell.expanded (6.0f, 2.0f), 3.0f, 1.0f);
                }
                const auto word = cell.removeFromBottom ((float) backWordH);
                paintBack (g, kind, cell.withHeight ((float) backIconH).withSizeKeepingCentre (56.0f, 34.0f),
                           on ? ui::look().orange : ui::look().violet.withAlpha (over ? 0.9f : 0.6f));
                g.setColour (on ? ui::look().orange : over ? ui::look().tx : ui::look().txDim);
                ui::look().drawTracked (g, backWords[kind], word, font, 0.06f, ui::Justification::centred);
                // The box's own back, a dot before its word: where DEFAULT goes back to.
                if (kind == ownBack (chosen))
                {
                    const float w = ui::look().trackedWidth (backWords[kind], font, 0.06f);
                    g.setColour (ui::look().txDim);
                    g.fillEllipse (ui::Rectangle<float> (4.0f, 4.0f).withCentre ({ word.getCentreX() - 0.5f * w - 8.0f, word.getCentreY() }));
                }
            }
        }

        // WALL under BACK, where the catalog has it: orange on, dim off, faint with the back closed — nothing gets out there.
        if (chosen >= 0 && catalog().wallSwitch)
        {
            const bool over = isHot (Part::wall), closed = backOf (chosen, setup_) == 0;
            g.setColour (setup_.wall ? (closed ? ui::look().orange.withAlpha (0.45f) : ui::look().orange)
                                     : over ? ui::look().tx : closed ? ui::look().txFaint : ui::look().txDim);
            ui::look().drawTracked (g, "WALL", wallArea(), font, 0.08f, ui::Justification::centredLeft);
        }

        // Under it the speakers, where the catalog has them: SPEAKER, then their words, the box's own dotted as its back is.
        if (chosen >= 0 && hasSpeakers())
        {
            // SPEAKER, the cone's switch where the catalog has a variant: orange with it on, lit under the hand.
            g.setColour (setup_.cone ? ui::look().orange : isHot (Part::speakerCone) ? ui::look().tx : ui::look().txFaint);
            ui::look().drawTracked (g, "SPEAKER", speakerWordArea(), font, 0.08f, ui::Justification::centredLeft);
            for (int i = 0; i < catalog().speakers.size(); ++i)
            {
                const bool on = i == speakerOf (chosen, setup_), over = ! on && isHot (Part::speaker, -1, i);
                const auto cell = speakerCell (i);
                if (over)
                {
                    g.setColour (ui::look().violet.withAlpha (0.7f));
                    g.drawRoundedRectangle (cell.expanded (6.0f, 0.0f), 3.0f, 1.0f);
                }
                g.setColour (on ? ui::look().orange : over ? ui::look().tx : ui::look().txDim);
                ui::look().drawTracked (g, catalog().speakerWords[i], cell, font, 0.06f, ui::Justification::centred);
                if (i == ownSpeaker (chosen))
                {
                    g.setColour (ui::look().txDim);
                    g.fillEllipse (ui::Rectangle<float> (4.0f, 4.0f).withCentre ({ cell.getX() - 7.0f, cell.getCentreY() }));
                }
            }
        }

        // ...and under them the room, where the catalog has rooms: ROOM, then OFF and theirs, the one round the box lit.
        if (chosen >= 0 && hasRooms())
        {
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, "ROOM", roomWordArea(), font, 0.08f, ui::Justification::centredLeft);
            for (int i = -1; i < catalog().rooms.size(); ++i)
            {
                const bool on = i == setup_.room, over = ! on && isHot (Part::room, -1, i);
                const auto cell = roomCell (i);
                if (over)
                {
                    g.setColour (ui::look().violet.withAlpha (0.7f));
                    g.drawRoundedRectangle (cell.expanded (6.0f, 0.0f), 3.0f, 1.0f);
                }
                g.setColour (on ? ui::look().orange : over ? ui::look().tx : ui::look().txDim);
                ui::look().drawTracked (g, roomWord (i), cell, font, 0.06f, ui::Justification::centred);
            }
        }
    }

public:
    /** One box as the pile draws it, `setup` its mics and horn when it is the chosen one, framed in the block's orange
        unless `plainFrame`; its tolex `body` opaque — the block's picture draws it over its IR (paintSketch) with the IR
        showing through, in a plain frame: alone there, it has nothing to be told from. */
    static void paintCab (ui::Graphics& g, const Cab& c, ui::Rectangle<float> box, const Setup& setup, bool isChosen, bool isHover,
                          float body = 1.0f, bool plainFrame = false, bool hornHover = false)
    {
        const float s = box.getWidth() / c.w;

        // The box: the tolex, a grille's hint, its outline — the chosen one in the block's orange line.
        g.setColour (ui::look().panel2.withMultipliedAlpha (body));
        g.fillRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f);
        g.setColour (ui::look().bezel.withMultipliedAlpha (body));
        g.fillRoundedRectangle (box.reduced (ui::jmax (1.5f, 0.025f * s)), 1.5f);
        const bool orange = isChosen && ! plainFrame;
        g.setColour (orange ? ui::look().orange : isHover ? ui::look().violet.withAlpha (0.7f) : ui::look().hair2);
        g.drawRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f, orange ? 1.5f : 1.0f);

        // The port where the back the chosen one is turned to has one: none closed, at its foot on a sealed box ported.
        if (const int back = isChosen ? setup.back : -1; back < 0 || back == ownBackOf (c.name) ? c.port : back == 1)
        {
            const auto at = portOf (c);
            g.setColour (ui::Colours::black);
            const float w = ui::jmin (0.20f, 2.0f * (0.5f * c.w - std::abs (at.x)) - 0.04f);
            g.fillRoundedRectangle (ui::Rectangle<float> (w * s, ui::jmax (2.0f, 0.035f * s)).withCentre (onBox (box, s, at)), 1.0f);
        }

        const auto line = ui::look().violet.withAlpha (isChosen ? 0.85f : 0.5f);
        for (const auto& d : c.drivers)
        {
            const auto o = onBox (box, s, d);
            const float r = c.radius * s;
            g.setColour (line);
            g.drawEllipse (ui::Rectangle<float> (2 * r, 2 * r).withCentre (o), 1.0f);
            g.drawEllipse (ui::Rectangle<float> (0.5f * r, 0.5f * r).withCentre (o), 0.8f);
        }

        if (c.horn)
        {
            // Under the hand on the chosen box, where a click turns it over: brighter.
            const bool off = isChosen && ! setup.a.horn;
            const auto colour = off ? ui::look().txFaint.withAlpha (hornHover ? 0.8f : 0.4f)
                                    : ui::look().lilac.withAlpha (isChosen ? 0.9f : 0.55f);
            paintHorn (g, onBox (box, s, c.hornAt), s, hornHover && ! off ? colour.brighter (0.4f) : colour);
        }

        // The mics where they stand on the first speaker, A orange and B lilac.
        if (isChosen)
        {
            const auto o = onBox (box, s, c.drivers.front());
            const float r = c.radius * s;
            for (int m = 1; m >= 0; --m)
            {
                if (! (m == 0 ? setup.aOn : setup.bOn))
                    continue;
                const auto& sp = m == 0 ? setup.a : setup.b;
                g.setColour (m == 0 ? ui::look().orange : ui::look().lilac);
                g.fillEllipse (ui::Rectangle<float> (5, 5).withCentre (o + ui::Point<float> (sp.x * r, -sp.y * r)));
            }
        }
    }

    /** A speaker, flat as the player faces it — its surround, its cone in rings, its cap — `R` its radius; its paper
        `body` opaque (the sketch over the IR lets the IR through). */
    static void paintCone (ui::Graphics& g, ui::Point<float> o, float R, float body = 1.0f)
    {
        g.setColour (ui::look().panel2.withMultipliedAlpha (body));
        g.fillEllipse (ui::Rectangle<float> (2.12f * R, 2.12f * R).withCentre (o));
        g.setGradientFill (ui::ColourGradient (ui::look().panel.withMultipliedAlpha (body), o.x, o.y, ui::look().bezel.withMultipliedAlpha (body), o.x + R, o.y, true));
        g.fillEllipse (ui::Rectangle<float> (2 * R, 2 * R).withCentre (o));
        g.setColour (ui::look().violet.withAlpha (0.8f));
        g.drawEllipse (ui::Rectangle<float> (2 * R, 2 * R).withCentre (o), 1.2f);
        g.setColour (ui::look().violet.withAlpha (0.18f));
        for (float k : { 0.45f, 0.7f, 0.88f })
            g.drawEllipse (ui::Rectangle<float> (2 * k * R, 2 * k * R).withCentre (o), 0.8f);
        g.setColour (ui::look().panel2.brighter (0.15f));
        g.fillEllipse (ui::Rectangle<float> (0.5f * R, 0.5f * R).withCentre (o));
        g.setColour (ui::look().violet.withAlpha (0.6f));
        g.drawEllipse (ui::Rectangle<float> (0.5f * R, 0.5f * R).withCentre (o), 1.0f);
    }

    /** The mics on a speaker drawn at `o`, radius `R`, in perspective, the farther from the cone drawn last (nearest the
        eye): its shadow on the cone, then the glyph, its capsule over the spot, as tall as it is near the eye, leaning back
        toward the cap; each only while it is on. `k` the scene's size, the panel's 1. */
    static void paintMicsOn (ui::Graphics& g, const Cab& cab, const Setup& setup, ui::Point<float> o, float R, float k, int lit = -1)
    {
        for (int m : drawOrder (setup))
        {
            if (! (m == 0 ? setup.aOn : setup.bOn))
                continue;
            const auto& sp = m == 0 ? setup.a : setup.b;
            const float near = nearness (sp.distance);
            const auto gl = glyphOf (sp, o, R, k);
            const auto colour = m == lit ? colourOf (m).brighter (0.3f) : colourOf (m);

            const float sh = (6.0f + 26.0f * near) * k;
            g.setColour (ui::Colours::black.withAlpha (0.55f - 0.35f * near));
            g.fillEllipse (ui::Rectangle<float> (sh, sh * 0.6f).withCentre (gl.at + ui::Point<float> (10.0f * near, 8.0f * near) * k));
            g.setColour (colour.withAlpha (0.9f));
            g.fillEllipse (ui::Rectangle<float> (5, 5).withCentre (gl.at));
            if (m == lit)   // under the hand, or in it: ringed at its capsule
            {
                g.setColour (colour.withAlpha (0.85f));
                g.drawEllipse (ui::Rectangle<float> (16.0f, 16.0f).withCentre (gl.at), 1.2f);
            }
            paintMicGlyph (g, sp.mic.isNotEmpty() ? sp.mic : cab.mic, gl.box, colour, gl.lean, gl.at);
        }
    }

    /** The two mics in the order they are drawn: the farther from the cone last, nearest the eye, over the other. */
    static std::array<int, 2> drawOrder (const Setup& setup)
    {
        return setup.b.distance < setup.a.distance ? std::array<int, 2> { 1, 0 } : std::array<int, 2> { 0, 1 };
    }

    /** Where a mic's glyph is drawn: its capsule over the spot, as tall as it is near the eye, leaning back from the cap
        by its angle — its top away from the centre — about the spot. */
    struct Glyph { ui::Point<float> at; ui::Rectangle<float> box; float lean; };
    static Glyph glyphOf (const Spot& sp, ui::Point<float> o, float R, float k)
    {
        const auto at = o + ui::Point<float> (sp.x * R, -sp.y * R);
        const float h = (30.0f + 70.0f * nearness (sp.distance)) * k, w = h * 36.0f / 96.0f;
        const float side = at.x >= o.x ? 1.0f : -1.0f;
        return { at, ui::Rectangle<float> (w, h).withPosition (at.x - w * 0.5f, at.y - h * 0.94f),
                 side * sp.angle * ui::MathConstants<float>::pi / 180.0f };
    }

    /** THE CABINET THAT PLAYS, SMALL, in `area` (the band of the IR's picture between its words): its speaker at the
        right edge, the mics on it as the panel's scene stands them (a tall one over the band's top); the box beside it on
        the band's floor, its mics' dots on its first speaker, every box at one scale — the tallest as tall as the band — so an 8x10 stands
        taller than a 2x10. A host's cabinet block draws it over its IR, the IR showing through the tolex. */
    static void paintSketch (ui::Graphics& g, int cab, const Setup& setup, ui::Rectangle<float> area)
    {
        if (cab < 0 || cab >= (int) cabs().size())
            return;

        const auto& c = cabs()[(size_t) cab];
        const float R = ui::jmax (8.0f, ui::jmin (0.45f * area.getHeight(), 0.09f * area.getWidth() + 18.0f));
        const ui::Point<float> o (area.getRight() - R - 28.0f, area.getCentreY());   // clear of the picture's dB marks

        static const auto tallest = [] { float h = 0.0f, w = 0.0f; for (const auto& b : cabs()) { h = ui::jmax (h, b.h); w = ui::jmax (w, b.w); } return ui::Point<float> (w, h); }();
        const float right = o.x - R - 10.0f;
        const float s = ui::jmin (area.getHeight() / tallest.y, 0.3f * area.getWidth() / tallest.x);
        paintCab (g, c, ui::Rectangle<float> (c.w * s, c.h * s).withPosition (right - c.w * s, area.getBottom() - c.h * s),   // on the floor
                  setup, true, false, 0.55f, true);

        paintCone (g, o, R, 0.55f);
        paintMicsOn (g, c, setup, o, R, R / 80.0f);   // the panel's speaker is about 80 px across its radius
    }

private:

    // ---------------------------------------------------------------- the speaker
    // Its mics in two columns, A at the left in the block's orange and B at the right in lilac, B's a mirror of A's —
    // each its list of mics, its distance standing beside the list, its places and its angle; between them the speaker,
    // flat, as the player faces it, and on it the mics in perspective: the nearer the cone, the smaller (the farther
    // from the eye), a shadow on the cone that grows and fades with the distance, the glyph leaning back toward the cap
    // by the angle, its centimetres under it. BLEND right under the speaker, ALIGN and Ø
    // after it (the owner, 05.11).
    static constexpr int colW = 108, rowH = 18, edge = 6;   // edge: B's column off the panel's

    static ui::Colour colourOf (int m) { return m == 0 ? ui::look().orange : ui::look().lilac; }

    int rightX() const { return leftW + gutter; }

    ui::Rectangle<int> column (int m) const
    {
        const int h = bodyH() - blendH;   // down to BLEND's row
        return m == 0 ? ui::Rectangle<int> (rightX(), 0, colW, h) : ui::Rectangle<int> (getWidth() - edge - colW, 0, colW, h);
    }

    ui::Rectangle<int> micRow (int m, int i) const
    {
        const auto c = column (m);
        return { c.getX(), c.getY() + rowH + 4 + i * rowH, c.getWidth(), rowH };
    }

    /** A column's places under its mics, two to a line: CAP EDGE, OFF ROOM. */
    ui::Rectangle<int> placeArea (int m, int i) const
    {
        const auto c = column (m);
        const int top = micRow (m, mics().size() - 1).getBottom() + 4;
        return { c.getX() + 4 + (i % 2) * (c.getWidth() / 2), top + (i / 2) * rowH, c.getWidth() / 2 - 4, rowH };
    }

    /** A column's angle: a word and its value over a track. */
    ui::Rectangle<float> angleTrack (int m) const
    {
        const auto c = column (m);
        const int top = placeArea (m, 3).getBottom() + 6;
        return ui::Rectangle<float> ((float) c.getX() + (m == 0 ? 4.0f : 8.0f), (float) (top + rowH + 2), (float) c.getWidth() - 12.0f, 6.0f);
    }

    /** B's TIME, under its angle: B moved against A, -1 ms to +1, nought in the middle (the owner, 05.11). */
    static constexpr float maxShiftMs = 1.0f;
    ui::Rectangle<float> timeTrack() const
    {
        const auto a = angleTrack (1);
        return a.translated (0.0f, (float) rowH + 12.0f);
    }

    /** A mic's distance in its column, along its list of mics and no lower (the owner, 05.11) — over EDGE's GE in A's,
        over CAP's CA in B's, where the names leave room — its centimetres level with the column's name, far at the top. */
    float distX (int m) const
    {
        const auto font = ui::look().font (13.0f);
        const auto width = [&font] (const ui::String& word) { return ui::look().trackedWidth (word, font, 0.06f); };
        float longest = 0.0f;   // ...and clear of the longest name by a little
        for (const auto& word : micWords())
            longest = ui::jmax (longest, width (word));
        const auto c = column (m).toFloat();
        if (m == 0)
            return ui::jmax ((float) placeArea (0, 1).getX() + width ("EDGE") - 0.5f * width ("GE"), c.getX() + 20.0f + longest + 14.0f);
        return ui::jmin ((float) placeArea (1, 0).getRight() - 4.0f - width ("CAP") + 0.5f * width ("CA"), c.getRight() - 20.0f - longest - 14.0f);
    }

    ui::Rectangle<int> distArea (int m) const   // the hand's: the value over the track and the track
    {
        const int top = column (m).getY(), bottom = micRow (m, mics().size() - 1).getBottom();
        return { ui::roundToInt (distX (m)) - 12, top, 24, bottom - top };
    }

    ui::Rectangle<float> distTrack (int m) const
    {
        const float top = (float) micRow (m, 0).getY() + 4.0f, bottom = (float) micRow (m, mics().size() - 1).getBottom() - 4.0f;
        return { distX (m) - 3.0f, top, 6.0f, bottom - top };
    }

    static ui::String cmOf (float distance)   // a metre and on, in metres: "1.2 m"
    {
        const int cm = ui::roundToInt (distance * 100.0f);
        return cm >= 100 ? ui::String (cm / 100) + "." + ui::String ((cm % 100) / 10) + " m" : ui::String (cm) + " cm";
    }

    /** ...and by the mic on the speaker, under its spot — a word to read, no slider (the owner, 05.11). */
    ui::Rectangle<float> distLabel (int m) const
    {
        const auto at = spotOf (m);
        const float w = ui::look().trackedWidth (cmOf (mic (m).distance), ui::look().font (13.0f), 0.0f) + 8.0f;
        return { at.x - 0.5f * w, at.y + 8.0f, w, 16.0f };
    }

    /** Both heard mics' centimetres, each under its own — and where the two would run into each other, pushed apart,
        half the overlap each, each to its mic's side (A to the left where the two stand level), kept inside the scene
        (the owner, 05.11). */
    std::array<ui::Rectangle<float>, 2> distLabels() const
    {
        std::array<ui::Rectangle<float>, 2> r { distLabel (0), distLabel (1) };
        if (! bothHeard())
            return r;
        const bool aLeft = ! (spotOf (1).x < spotOf (0).x);
        auto& left = r[aLeft ? 0 : 1];
        auto& right = r[aLeft ? 1 : 0];
        constexpr float gap = 6.0f;
        const bool level = left.getY() < right.getBottom() && right.getY() < left.getBottom();
        if (const float overlap = left.getRight() + gap - right.getX(); level && overlap > 0.0f)
        {
            left = left.translated (-0.5f * overlap, 0.0f);
            right = right.translated (0.5f * overlap, 0.0f);
            const auto sc = scene();
            if (const float under = sc.getX() + 2.0f - left.getX(); under > 0.0f)
            {
                left = left.translated (under, 0.0f);
                right = right.translated (ui::jmax (0.0f, left.getRight() + gap - right.getX()), 0.0f);
            }
            if (const float over = right.getRight() - (sc.getRight() - 2.0f); over > 0.0f)
            {
                right = right.translated (-over, 0.0f);
                left = left.translated (-ui::jmax (0.0f, left.getRight() + gap - right.getX()), 0.0f);
            }
        }
        return r;
    }

    static constexpr int blendH = 26;   // BLEND's row, right under the speaker

    ui::Rectangle<float> scene() const
    {
        const int x0 = column (0).getRight() + 8, x1 = column (1).getX() - 8;
        return ui::Rectangle<int> (x0, 4, x1 - x0, bodyH() - 4 - blendH).toFloat();
    }

    float radius() const { const auto s = scene(); return ui::jmax (10.0f, ui::jmin (s.getWidth(), s.getHeight()) * 0.5f / 1.2f); }

    /** Under the speaker (the owner, 05.11): BLEND, A, its track as wide as the speaker's scene, B — ALIGN and Ø after. */
    ui::Rectangle<int> blendRow() const
    {
        const int y = scene().toNearestInt().getBottom() + 2;
        return { rightX(), y, getWidth() - rightX(), bodyH() - y };
    }

    ui::Rectangle<int> foot() const { return { rightX(), getHeight() - footHeight() + 4, getWidth() - rightX(), footHeight() - 4 }; }

    /** The foot: CLOSE and DEFAULT, buttons, at the right. */
    static int wordW (const char* word) { return ui::roundToInt (ui::look().trackedWidth (word, ui::look().font (13.0f), 0.06f)) + 4; }

    ui::Rectangle<int> closeArea() const
    {
        const int w = onClose != nullptr ? wordW ("CLOSE") + 14 : 0;
        return { getWidth() - w, foot().getY(), w, foot().getHeight() };
    }

    ui::Rectangle<int> defaultArea() const
    {
        if (! showDefault)
            return {};
        const int w = wordW ("DEFAULT") + 14;
        return { closeArea().getX() - (onClose != nullptr ? 6 : 0) - w, foot().getY(), w, foot().getHeight() };
    }

    ui::Rectangle<int> alignArea() const { return { column (1).getX(), blendRow().getY(), wordW ("ALIGN"), blendRow().getHeight() }; }

    ui::Rectangle<int> invertArea() const { return { alignArea().getRight() + 8, blendRow().getY(), 20, blendRow().getHeight() }; }

    ui::Rectangle<float> blendTrack() const
    {
        const auto sc = scene();
        return { sc.getX() + 18.0f, blendRow().toFloat().getCentreY() - 3.0f, sc.getWidth() - 36.0f, 6.0f };
    }

    /** FIND, under A's angle, its column's width — level with B's TIME (the owner, 07.11): orange, the panel's one call to
        act, its words on two lines where one is too narrow for them at the reading size. */
    static constexpr int findLineH = 15;   // FIND's two lines, at the reading size, a little closer than a row

    ui::Rectangle<int> findArea() const
    {
        if (onFind == nullptr)
            return {};
        // Two lines tall always, its lines close (findLineH); where the column is short (the bass's seven mics) nearer the
        // angle, never down into BLEND's row.
        const auto c = column (0);
        const int h = 2 * findLineH + 4, under = ui::roundToInt (angleTrack (0).getBottom());
        const int top = ui::jmax (under + 4, ui::jmin (under + 12, blendRow().getY() - h));
        return { c.getX() + 4, top, c.getWidth() - 8, h };
    }

    void paintFind (ui::Graphics& g) const
    {
        const auto r = findArea();
        if (r.isEmpty())
            return;
        const auto b = r.toFloat().reduced (0.5f);
        const auto font = ui::look().font (13.0f);
        const auto ink = searching || isHot (Part::find) ? ui::look().orange.brighter (0.3f) : ui::look().orange;
        g.setColour (ui::look().panel2);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (ink);
        g.drawRoundedRectangle (b, 4.0f, searching ? 1.6f : 1.2f);
        if (ui::look().trackedWidth ("FIND BEST POSITION", font, 0.06f) + 14.0f <= b.getWidth())
            ui::look().drawTracked (g, "FIND BEST POSITION", b, font, 0.06f, ui::Justification::centred);
        else
        {
            const auto lines = b.withSizeKeepingCentre (b.getWidth(), 2.0f * (float) findLineH);
            ui::look().drawTracked (g, "FIND BEST", lines.withHeight ((float) findLineH), font, 0.06f, ui::Justification::centred);
            ui::look().drawTracked (g, "POSITION", lines.withTrimmedTop ((float) findLineH), font, 0.06f, ui::Justification::centred);
        }
    }

    ui::Rectangle<float> blendWordArea() const
    {
        const float w = ui::look().trackedWidth ("BLEND", ui::look().font (13.0f), 0.06f) + 4.0f;
        const auto row = blendRow().toFloat();
        return { blendTrack().getX() - 22.0f - w, row.getY(), w, row.getHeight() };
    }

    /** The places a mic is put at with one click, as the table's positions stand them: at the cap, at the cone's edge,
        half way out and turned 35° to the cap, and as far off as it goes. Across the cone on the mic's own side. */
    struct Place { const char* word; float r, distance, angle; };
    static constexpr Place places[] { { "CAP", 0.0f, 0.05f, 0.0f }, { "EDGE", 0.8f, 0.04f, 0.0f },
                                      { "OFF", 0.5f, 0.05f, 35.0f }, { "ROOM", 0.0f, 0.30f, 0.0f } };
    /** ...ROOM as far as the catalog says (the guitar's a metre). */
    static Place placeAt (int i) { auto p = places[i]; if (i == 3) p.distance = catalog().roomM; return p; }
    void putAt (int m, const Place& p)
    {
        takeUp (m);
        auto& sp = mic (m);
        float side = sp.x > 0.0f ? 1.0f : -1.0f;
        if (std::abs (sp.x) <= 0.05f)   // on the axis: the side away from the other mic, while that one is on
            side = mic (1 - m).x > 0.0f && heard (1 - m) ? -1.0f : 1.0f;
        sp.x = side * p.r;
        sp.y = 0.0f;
        sp.distance = p.distance;
        sp.angle = p.angle;
        send();
    }

    bool standsAt (const Spot& sp, const Place& p) const
    {
        return std::abs (std::abs (sp.x) - p.r) < 0.02f && std::abs (sp.y) < 0.02f
            && std::abs (sp.distance - p.distance) < 0.004f && std::abs (sp.angle - p.angle) < 1.0f;
    }

    static float along (ui::Rectangle<float> t, float x) { return ui::jlimit (0.0f, 1.0f, (x - t.getX()) / t.getWidth()); }

    /** Where a mic stands on the cone, on the screen. */
    ui::Point<float> spotOf (int m) const
    {
        const auto o = coneCentre();
        const float R = radius();
        return o + ui::Point<float> (mic (m).x * R, -mic (m).y * R);
    }

    static float nearness (float distance)   // 0 at the cone, 1 at 30 cm
    {
        return std::log (ui::jlimit (minCm, maxCm(), distance * 100.0f) / minCm) / std::log (maxCm() / minCm);
    }

    bool heard (int m) const { return m == 0 ? setup_.aOn : setup_.bOn; }
    bool bothHeard() const { return setup_.aOn && setup_.bOn; }

    /** A dark column taken up: its mic heard again where it stood, BLEND where it was left. B taken up on top of A
        stands to A's right, not where it was only waiting — and a host may then move it where the two comb least
        (synth::quietPlaceForB). */
    void takeUp (int m)
    {
        if (heard (m))
            return;
        if (m == 0)
        {
            setup_.aOn = true;
            return;
        }
        setup_.bOn = true;
        if (std::hypot (setup_.b.x - setup_.a.x, setup_.b.y - setup_.a.y) < 0.4f)
        {
            setup_.b.x = ui::jmin (1.05f, setup_.a.x + 0.7f);
            setup_.b.y = 0.0f;
        }
    }

    /** The heard mic under `p` — its glyph, leaning as it is drawn, or its capsule — the one drawn over the other taken
        first; -1 for none. */
    int micAt (ui::Point<float> p) const
    {
        const auto o = coneCentre();
        const float R = radius();
        const auto order = drawOrder (setup_);
        for (int k = 1; k >= 0; --k)
        {
            const int m = order[(size_t) k];
            if (! heard (m))
                continue;
            const auto gl = glyphOf (mic (m), o, R, 1.0f);
            const auto d = p - gl.at;
            const float c = std::cos (gl.lean), s = std::sin (gl.lean);
            const ui::Point<float> upright (gl.at.x + d.x * c + d.y * s, gl.at.y - d.x * s + d.y * c);   // turned back
            if (gl.box.expanded (4.0f).contains (upright) || p.getDistanceFrom (gl.at) < 9.0f)
                return m;
        }
        return -1;
    }

    static void paintMicGlyph (ui::Graphics& g, const ui::String& name, ui::Rectangle<float> box, ui::Colour colour, float lean,
                               ui::Point<float> pivot)
    {
        g.drawGlyph (name, box, colour, lean, pivot);
    }

    void paintColumn (ui::Graphics& g, int m) const
    {
        const auto font = ui::look().font (13.0f);
        const auto c = column (m);
        const auto colour = colourOf (m);
        const bool live = heard (m);
        const float dim = live ? 1.0f : 0.45f;
        const auto& sp = mic (m);
        const auto& cab = cabs()[(size_t) chosen];
        const auto heardBy = sp.mic.isNotEmpty() ? sp.mic : cab.mic;

        // B's a mirror of A's: its words at the right, each mic's glyph after its name (the owner, 05.11).
        const auto side = m == 0 ? ui::Justification::centredLeft : ui::Justification::centredRight;
        g.setColour (colour.withMultipliedAlpha (dim));
        ui::look().drawTracked (g, m == 0 ? "MIC A" : "MIC B", c.toFloat().withHeight ((float) rowH), font, 0.08f, side);

        for (int i = 0; i < mics().size(); ++i)
        {
            auto r = micRow (m, i).toFloat();
            const bool lit = mics()[i] == heardBy, over = isHot (Part::micRow, m, i);
            const auto tint = (lit ? (over ? colour.brighter (0.3f) : colour) : over ? ui::look().tx : ui::look().txFaint).withMultipliedAlpha (dim);
            const auto glyph = m == 0 ? r.removeFromLeft (12.0f) : r.removeFromRight (12.0f);
            paintMicGlyph (g, mics()[i], glyph.reduced (0, 1), tint, 0.0f, {});
            g.setColour (tint);
            ui::look().drawTracked (g, micWords()[i], m == 0 ? r.withTrimmedLeft (8.0f) : r.withTrimmedRight (8.0f), font, 0.06f, side);
        }

        // Its places, in its colour where it stands.
        for (int i = 0; i < 4; ++i)
        {
            const bool over = isHot (Part::place, m, i);
            g.setColour ((standsAt (sp, placeAt (i)) ? (over ? colour.brighter (0.3f) : colour) : over ? ui::look().tx : ui::look().txDim)
                             .withMultipliedAlpha (dim));
            auto cell = placeArea (m, i).toFloat();
            ui::look().drawTracked (g, placeAt (i).word, m == 0 ? cell : cell.withTrimmedRight (4.0f), font, 0.06f, side);
        }

        // Its angle, a word and its value over a track — the value on the side away from the word.
        {
            const auto t = angleTrack (m);
            const auto label = ui::Rectangle<float> (t.getX(), t.getY() - (float) rowH - 2.0f, t.getWidth(), (float) rowH);
            g.setColour (ui::look().txFaint.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, "ANGLE", label, font, 0.06f, side);
            g.setColour (ui::look().txDim.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, ui::String (ui::roundToInt (sp.angle)) + ui::String::fromUTF8 ("\xc2\xb0"), label, font, 0.0f,
                                m == 0 ? ui::Justification::centredRight : ui::Justification::centredLeft);
            g.setColour (ui::look().hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (colour.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (ui::Rectangle<float> (6.0f, 12.0f).withCentre ({ t.getX() + sp.angle / maxAngle * t.getWidth(), t.getCentreY() }), 2.0f);
        }

        // B's TIME under its angle: the word, its milliseconds, a track with nought marked in its middle.
        if (m == 1)
        {
            const auto t = timeTrack();
            const auto label = ui::Rectangle<float> (t.getX(), t.getY() - (float) rowH - 2.0f, t.getWidth(), (float) rowH);
            g.setColour (ui::look().txFaint.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, "TIME", label, font, 0.06f, side);
            g.setColour ((std::abs (setup_.shiftMs) > 0.0f ? colour : ui::look().txDim).withMultipliedAlpha (dim));
            ui::look().drawTracked (g, (setup_.shiftMs > 0.0f ? "+" : "") + ui::String (setup_.shiftMs, 2) + " ms", label, font, 0.0f,
                                ui::Justification::centredLeft);
            g.setColour (ui::look().hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.fillRect (ui::Rectangle<float> (1.0f, 8.0f).withCentre (t.getCentre()));
            g.setColour (colour.withMultipliedAlpha (dim));
            const float at = t.getX() + (0.5f + 0.5f * setup_.shiftMs / maxShiftMs) * t.getWidth();
            g.fillRoundedRectangle (ui::Rectangle<float> (6.0f, 12.0f).withCentre ({ at, t.getCentreY() }), 2.0f);
        }

        // Its distance, beside its list: its centimetres level with the column's name, far at the top.
        {
            const auto t = distTrack (m);
            g.setColour (ui::look().txDim.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, cmOf (sp.distance), { t.getCentreX() - 24.0f, (float) c.getY(), 48.0f, (float) rowH }, font, 0.0f,
                                ui::Justification::centred);
            g.setColour (ui::look().hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (2.0f, t.getHeight()), 1.0f);
            g.setColour (colour.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (ui::Rectangle<float> (12.0f, 6.0f).withCentre ({ t.getCentreX(), t.getBottom() - nearness (sp.distance) * t.getHeight() }), 2.0f);
        }
    }

    void paintSpeaker (ui::Graphics& g) const
    {
        const auto font = ui::look().font (13.0f);
        if (chosen < 0)
        {
            // An IR of the player's own plays (or the factory's): the speaker waits for a box.
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, "CHOOSE A CABINET", ui::Rectangle<int> (rightX(), 0, getWidth() - rightX(), bodyH()).toFloat(),
                                font, 0.08f, ui::Justification::centred);
            return;
        }

        const auto& cab = cabs()[(size_t) chosen];
        paintColumn (g, 0);
        paintColumn (g, 1);

        const auto sc = scene();
        const float R = radius();
        const auto o = coneCentre();
        const float s = R / cab.radius;   // px a metre

        // The neighbours and the horn, at the same scale, cut off at the scene's edge.
        {
            ui::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (sc.toNearestInt());
            g.setColour (ui::look().bezel);
            g.fillRoundedRectangle (sc, 4.0f);
            for (size_t i = 1; i < cab.drivers.size(); ++i)
            {
                const auto d = cab.drivers[i] - cab.drivers.front();
                g.setColour (ui::look().violet.withAlpha (0.25f));
                g.drawEllipse (ui::Rectangle<float> (2 * R, 2 * R).withCentre (o + ui::Point<float> (d.x * s, -d.y * s)), 1.0f);
            }
            // The horn, big, in its corner; beside it HORN and its steps, the one it stands at in the block's orange.
            if (const auto hc = hornCorner(); ! hc.isEmpty())
            {
                paintHorn (g, hc.getCentre(), hc.getWidth() / 0.12f, setup_.a.horn ? ui::look().lilac.withAlpha (0.8f) : ui::look().txFaint.withAlpha (0.35f));
                const auto side = ui::Justification::centredRight;
                g.setColour (ui::look().txFaint);
                ui::look().drawTracked (g, "HORN", hornWordArea(), font, 0.08f, side);
                for (int i = 0; i < 4; ++i)
                {
                    const bool over = isHot (Part::horn, -1, i);
                    g.setColour (i == hornStep() ? (over ? ui::look().orange.brighter (0.3f) : ui::look().orange) : over ? ui::look().tx : ui::look().txDim);
                    ui::look().drawTracked (g, hornWords[i], hornStepArea (i), font, 0.06f, ui::Justification::centred);
                }
            }

            paintCone (g, o, R);

            // The search's map: each place it judged a dot, green where B and A add, red where they cancel.
            if (searchFade > 0.0f)
                for (const auto& d : searchDots)
                {
                    const float t = ui::jlimit (0.0f, 1.0f, -d.score / 60.0f);
                    const auto at = o + ui::Point<float> (d.x * R, -d.y * R);
                    const auto dot = ui::Rectangle<float> (12.0f, 12.0f).withCentre (at);
                    g.setColour (ui::Colour (0xff3fae6a).interpolatedWith (ui::Colour (0xffd5455a), t).withMultipliedAlpha (0.85f * searchFade));
                    g.fillEllipse (dot);
                    g.setColour (ui::Colours::black.withAlpha (0.5f * searchFade));
                    g.drawEllipse (dot, 1.0f);
                }

            paintMicsOn (g, cab, setup_, o, R, 1.0f, drag == Drag::spot ? dragMic : hot.part == Part::mic ? hot.m : -1);

            // Each heard mic's centimetres under it, the two kept apart.
            const auto labels = distLabels();
            for (int m = 0; m < 2; ++m)
                if (heard (m))
                {
                    g.setColour (colourOf (m));
                    ui::look().drawTracked (g, cmOf (mic (m).distance), labels[(size_t) m], font, 0.0f, ui::Justification::centred);
                }
        }

        // The blend: A..B on a track, ALIGN, and Ø for B — faint while one mic is heard, the blend not heard then.
        {
            const float dim = bothHeard() ? 1.0f : 0.45f;
            const auto row = blendRow().toFloat();
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, "BLEND", blendWordArea(), font, 0.06f, ui::Justification::centredRight);
            const auto t = blendTrack();
            g.setColour (ui::look().orange.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, "A", ui::Rectangle<float> (t.getX() - 18.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, ui::Justification::centred);
            g.setColour (ui::look().lilac.withMultipliedAlpha (dim));
            ui::look().drawTracked (g, "B", ui::Rectangle<float> (t.getRight() + 4.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, ui::Justification::centred);
            g.setColour (ui::look().hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (ui::look().orange.interpolatedWith (ui::look().lilac, setup_.blend).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (ui::Rectangle<float> (6.0f, 14.0f).withCentre ({ t.getX() + setup_.blend * t.getWidth(), t.getCentreY() }), 2.0f);
            const auto toggle = [this] (bool on, Part part)
            {
                const bool over = isHot (part);
                return on ? (over ? ui::look().orange.brighter (0.3f) : ui::look().orange) : over ? ui::look().tx : ui::look().txFaint;
            };
            g.setColour (toggle (bothHeard() && setup_.align, Part::align));
            ui::look().drawTracked (g, "ALIGN", alignArea().toFloat(), font, 0.06f, ui::Justification::centredLeft);
            g.setColour (toggle (bothHeard() && setup_.invert, Part::invert));
            ui::look().drawTracked (g, ui::String::fromUTF8 ("\xc3\x98"), invertArea().toFloat(), font, 0.0f, ui::Justification::centred);
            paintFind (g);
            paintButton (g, defaultArmed ? "SURE?" : "DEFAULT", defaultArea(), defaultArmed, isHot (Part::reset));
            paintButton (g, "CLOSE", closeArea(), false, isHot (Part::close));
        }
    }

    /** A button: a box round its word — DEFAULT, CLOSE, DOWNLOAD IR — orange while it asks. */
    static void paintButton (ui::Graphics& g, const char* word, ui::Rectangle<int> r, bool asking, bool over = false)
    {
        if (r.isEmpty())
            return;
        const auto b = r.toFloat().reduced (0.5f, 1.5f);
        g.setColour (ui::look().panel2);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (asking ? ui::look().orange : over ? ui::look().txDim : ui::look().txFaint);   // under the hand: its line up
        g.drawRoundedRectangle (b, 4.0f, asking ? 1.2f : 1.0f);
        g.setColour (asking ? ui::look().orange : over ? ui::look().tx : ui::look().txDim);
        ui::look().drawTracked (g, word, b, ui::look().font (13.0f), 0.06f, ui::Justification::centred);
    }

    // ---------------------------------------------------------------- the response
    // Across the panel under it all: the cabinet's response, 30 Hz to 12 kHz on a log scale, +15 to -30 dB — the one
    // heard green to red, the one kept before it a faint grey ghost; dB at the left, the frequencies under.
    std::vector<float> responseNow, responseBefore, responseComb;
    std::vector<float> responseBad;   // dB, each point: how far it falls into a pit, or its two mics cancel there — the deeper
    // +15 over the middle, -30 under it: a cabinet's response rarely climbs past +10, its roll-offs and a comb's dips
    // go to -20 and -30, and under that nothing more is heard (the owner, 05.11).
    static constexpr float responseTop = 15.0f, responseBottom = -30.0f;

    ui::Rectangle<int> responseArea() const { return { 0, bodyH(), getWidth(), responseH }; }

    ui::Rectangle<float> responsePlot() const
    {
        return responseArea().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (12.0f).withTrimmedTop (9.0f).withTrimmedBottom (20.0f);
    }

    void paintResponse (ui::Graphics& g) const
    {
        const auto font = ui::look().font (13.0f);
        const auto area = responseArea().toFloat();
        const auto p = responsePlot();
        g.setColour (ui::look().hair);
        g.fillRect (area.withHeight (1.0f));

        const auto xOf = [p] (float hz) { return p.getX() + p.getWidth() * std::log (hz / 30.0f) / std::log (12000.0f / 30.0f); };
        const auto yOf = [p] (float db) { return p.getY() + (responseTop - db) / (responseTop - responseBottom) * p.getHeight(); };

        // Under it all, with two mics heard, where they meet (the owner, 05.11): green where they add in phase, red
        // where they cancel — the stronger, the deeper, full at 6 dB.
        if ((int) responseComb.size() == responsePoints)
            for (int i = 0; i < responsePoints; ++i)
            {
                const float x0 = i == 0 ? p.getX() : 0.5f * (xOf (responseHz (i - 1)) + xOf (responseHz (i)));
                const float x1 = i == responsePoints - 1 ? p.getRight() : 0.5f * (xOf (responseHz (i)) + xOf (responseHz (i + 1)));
                const float v = responseComb[(size_t) i];
                g.setColour ((v > 0.0f ? ui::Colour (0xff3fae6a) : ui::Colour (0xffd5455a)).withAlpha (0.45f * ui::jmin (1.0f, std::abs (v) / 6.0f)));
                g.fillRect (ui::Rectangle<float> (x0, p.getY(), x1 - x0 + 0.5f, p.getHeight()));
            }

        struct Mark { float hz; const char* word; };
        static constexpr Mark marks[] { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                        { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };
        for (const auto& m : marks)
        {
            const float x = xOf (m.hz);
            g.setColour (ui::look().hair);
            g.fillRect (ui::Rectangle<float> (x, p.getY(), 1.0f, p.getHeight()));
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, m.word, { x - 20.0f, p.getBottom() + 3.0f, 40.0f, 16.0f }, font, 0.0f, ui::Justification::centred);
        }
        struct Level { float db; const char* word; };
        static constexpr Level levels[] { { 10.0f, "+10" }, { 0.0f, "0" }, { -10.0f, "-10" }, { -20.0f, "-20" }, { -30.0f, "-30" } };
        for (const auto& l : levels)
        {
            const float y = yOf (l.db);
            g.setColour (std::abs (l.db) < 1.0f ? ui::look().hair2 : ui::look().hair);
            g.fillRect (ui::Rectangle<float> (p.getX(), y, p.getWidth(), 1.0f));
            g.setColour (ui::look().txFaint);
            ui::look().drawTracked (g, l.word, { area.getX() + 2.0f, y - 8.0f, 28.0f, 16.0f }, font, 0.0f, ui::Justification::centredRight);
        }

        const auto curve = [&] (const std::vector<float>& db, ui::Colour colour, float width, int from = 0, int to = responsePoints - 1)
        {
            if ((int) db.size() != responsePoints)
                return;
            ui::Path path;
            for (int i = from; i <= to; ++i)
            {
                // Past the scale it goes out of the frame, as a dip does, not along its floor (the owner, 05.11).
                const ui::Point<float> at (xOf (responseHz (i)), yOf (ui::jlimit (3.0f * responseBottom, 3.0f * responseTop, db[(size_t) i])));
                if (i == from)
                    path.startNewSubPath (at);
                else
                    path.lineTo (at);
            }
            g.setColour (colour);
            g.strokePath (path, ui::PathStrokeType (width, ui::PathStrokeType::curved, ui::PathStrokeType::rounded));
        };
        ui::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (p.toNearestInt().expanded (0, 2));
        curve (responseBefore, ui::look().txDim.withAlpha (0.3f), 1.2f);   // barely there: a second line reads as a second mic
        // What plays, green where it is fine and red where it falls into a pit or its two mics cancel — from 3 dB, full
        // red at 6, the bounds the presets are held to (the owner, 05.11): drawn in runs of one shade, each segment as
        // red as the worse of its two ends.
        if (responseBad.size() == responseNow.size())
        {
            const auto shade = [this] (int i)
            {
                // Past 3 dB at all, a shade at least: the line reddens where FIND would move a mic (synth::MicSearch).
                const auto at = [this] (int k) { return ui::jlimit (0, 8, (int) std::ceil ((responseBad[(size_t) k] - 3.0f) / 3.0f * 8.0f)); };
                return ui::jmax (at (i), at (i + 1));
            };
            for (int from = 0; from < (int) responseNow.size() - 1;)
            {
                const int level = shade (from);
                int to = from + 1;
                while (to < (int) responseNow.size() - 1 && shade (to) == level)
                    ++to;
                curve (responseNow, ui::Colour (0xff3fae6a).interpolatedWith (ui::Colour (0xffd5455a), (float) level / 8.0f), 1.6f, from, to);
                from = to;
            }
        }

        // DOWNLOAD IR over the corner, its line pulsing slowly — the one thing the response invites a hand to do.
        if (const auto d = downloadArea(); ! d.isEmpty())
        {
            const float pulse = isHot (Part::download) ? 1.0f   // under the hand: full, still
                                                       : 0.5f + 0.5f * (float) std::sin (ui::MathConstants<double>::twoPi * 0.5 * clock);
            const auto b = d.toFloat().reduced (0.5f);
            {
                ui::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (d.expanded (2));
                g.setColour (ui::look().panel2.withAlpha (0.92f));
                g.fillRoundedRectangle (b, 4.0f);
                g.setColour (ui::look().orange.withAlpha (0.35f + 0.55f * pulse));
                g.drawRoundedRectangle (b, 4.0f, 1.3f);
                g.setColour (ui::look().orange.withAlpha (0.75f + 0.25f * pulse));
                ui::look().drawTracked (g, "DOWNLOAD IR", b, ui::look().font (13.0f), 0.06f, ui::Justification::centred);
            }
            // Its format's ▾, still — it answers the hand only.
            if (const auto f = downloadFormatArea(); ! f.isEmpty())
            {
                const auto m = f.toFloat().reduced (0.5f);
                const bool over = isHot (Part::downloadFormat);
                g.setColour (ui::look().panel2.withAlpha (0.92f));
                g.fillRoundedRectangle (m, 4.0f);
                g.setColour (ui::look().orange.withAlpha (over ? 0.9f : 0.45f));
                g.drawRoundedRectangle (m, 4.0f, 1.3f);
                ui::Path down;
                down.startNewSubPath (m.getCentreX() - 4.5f, m.getCentreY() - 2.0f);
                down.lineTo (m.getCentreX(), m.getCentreY() + 2.5f);
                down.lineTo (m.getCentreX() + 4.5f, m.getCentreY() - 2.0f);
                g.setColour (ui::look().orange.withAlpha (over ? 1.0f : 0.8f));
                g.strokePath (down, ui::PathStrokeType (1.6f, ui::PathStrokeType::curved, ui::PathStrokeType::rounded));
            }
            // ...and what the next download will be, under them.
            if (downloadNote.isNotEmpty())
            {
                const float right = (float) (downloadFormatArea().isEmpty() ? d : downloadFormatArea()).getRight();
                g.setColour (ui::look().txDim);
                ui::look().drawTracked (g, downloadNote, { right - 320.0f, (float) d.getBottom() + 4.0f, 320.0f, 16.0f },
                                        ui::look().font (13.0f), 0.0f, ui::Justification::centredRight);
            }
        }
    }

};

/** A STATE FROM ITS TEXT, as the panel can draw and the synthesis render it: none where the text is none, or where it
    names a box the table has not — one kept by an older version under a name since changed, say (0.1.3 named the boxes by
    size and family), which would render a box of no cones — or a mic the panel has not, which it has no glyph to draw
    (a hand-made or mangled text, the site found 07.11). Wherever a state comes in from outside — a run's workspace, a
    preset file, a host's session, a page — it comes through here, and a caller falls back to its default. */
inline std::optional<State> knownState (const std::string& toml)
{
    auto s = fromToml (toml);
    const auto knownMic = [] (const Mic& m) { return m.model.empty() || Panel::mics().contains (ui::String (m.model)); };
    if (s && (Panel::indexOf (ui::String (s->box)) < 0 || ! knownMic (s->a) || ! knownMic (s->b)))
        return std::nullopt;
    return s;
}

} // namespace cabir
