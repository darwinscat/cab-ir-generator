// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

// CABSYNTH — a close-miked bass cabinet, synthesized: every cone a surface of patches, each moving as a piston below its
// breakup and as a sum of seeded modes above it, summed at the mic with its true path delays; the box's low end (sealed,
// vented, cone and port apart, or open at the back, the cones' backs wrapping round), the horn on its crossover, the
// baffle's edges, the box's cavity and panel modes, the
// mic's capsule, pattern and proximity. Frequency domain, one complex sum per bin, then one inverse FFT.
//
// The cabinets are a table, "[section] key = value" with `inherit` (data/cabinets.txt): the tool renders every
// section of it to files (tools/cabgen.cpp), a host renders one cabinet with the mic wherever the player put it (renderAt).
// JUCE-free: the plugins, the app and the web page all render by it.

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "State.h"

namespace cabir::synth
{

using cd = std::complex<double>;
constexpr double pi = 3.14159265358979323846;

/** In place, n a power of two; inverse scales by 1/n. */
inline void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * pi / (double) len * (inverse ? 1 : -1);
        const cd w (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd wk (1);
            for (size_t k = 0; k < len / 2; ++k, wk *= w)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

/** The minimum-phase spectrum of a magnitude given on bins 0..n/2 (folded cepstrum). */
inline std::vector<cd> minimumPhase (const std::vector<double>& mag, size_t n)
{
    std::vector<cd> c (n);
    for (size_t k = 0; k < n; ++k)
    {
        const size_t b = k <= n / 2 ? k : n - k;
        c[k] = std::log (std::max (mag[b], 1e-9));
    }
    fft (c, true);
    for (size_t k = 1; k < n / 2; ++k) { c[k] *= 2.0; c[n - k] = 0.0; }
    fft (c, false);
    for (auto& x : c) x = std::exp (x);
    return c;
}

/** RBJ biquad's response at f (Hz), as a complex gain. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    cd at (double f, double fs) const
    {
        const cd z1 = std::polar (1.0, -2 * pi * f / fs), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
    static Biquad make (const std::string& type, double f0, double q, double db, double fs)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f0 / fs, cw = std::cos (w), sw = std::sin (w),
                     al = sw / (2 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        if (type == "hp")      { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else if (type == "lp") { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else if (type == "peak")
        { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
        else if (type == "lowshelf" || type == "highshelf")
        {
            const double s = 2 * std::sqrt (A) * al, sgn = type == "lowshelf" ? 1 : -1;
            b0 = A * ((A + 1) - sgn * (A - 1) * cw + s);
            b1 = sgn * 2 * A * ((A - 1) - sgn * (A + 1) * cw);
            b2 = A * ((A + 1) - sgn * (A - 1) * cw - s);
            a0 = (A + 1) + sgn * (A - 1) * cw + s;
            a1 = -sgn * 2 * ((A - 1) + sgn * (A + 1) * cw);
            a2 = (A + 1) + sgn * (A - 1) * cw - s;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

inline void writeWav24 (const std::string& path, const std::vector<double>& x, int fs)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) x.size(), data = n * 3;
    auto u32 = [&] (uint32_t v) { f.put ((char) v); f.put ((char) (v >> 8)); f.put ((char) (v >> 16)); f.put ((char) (v >> 24)); };
    auto u16 = [&] (uint16_t v) { f.put ((char) v); f.put ((char) (v >> 8)); };
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (1); u32 ((uint32_t) fs);
    u32 ((uint32_t) fs * 3); u16 (3); u16 (24); f.write ("data", 4); u32 (data);
    for (double v : x)
    {
        const int32_t s = (int32_t) std::lround (std::clamp (v, -1.0, 1.0 - 1.0 / 8388608.0) * 8388608.0);
        f.put ((char) s); f.put ((char) (s >> 8)); f.put ((char) (s >> 16));
    }
}

/** "[name]" opens a section; "key = value" lines fill it; '#' starts a comment. */
using Section = std::map<std::string, std::string>;
using Table = std::vector<std::pair<std::string, Section>>;

/** The table out of its text. */
inline Table parseTable (const std::string& text)
{
    Table out;
    std::istringstream f (text);
    std::string line;
    const auto trim = [] (std::string s) {
        const auto a = s.find_first_not_of (" \t\r"), b = s.find_last_not_of (" \t\r");
        return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
    };
    while (std::getline (f, line))
    {
        line = trim (line.substr (0, line.find ('#')));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { out.push_back ({ trim (line.substr (1, line.size() - 2)), {} }); continue; }
        const auto eq = line.find ('=');
        if (eq == std::string::npos || out.empty()) continue;
        out.back().second[trim (line.substr (0, eq))] = trim (line.substr (eq + 1));
    }
    return out;
}

inline Table readTable (const std::string& path)
{
    std::ifstream f (path);
    std::stringstream all;
    all << f.rdbuf();
    return parseTable (all.str());
}

inline double num (const Section& s, const std::string& k, double fallback)
{
    const auto it = s.find (k);
    return it == s.end() || it->second.empty() ? fallback : std::atof (it->second.c_str());
}

constexpr double fs = 48000.0, c0 = 343.0;

struct Xy { double x, y; };

inline std::vector<double> nums (const std::string& s)
{
    std::vector<double> v;
    std::string cur;
    for (char ch : s + ",")
    {
        if (ch == ',' || ch == '/' || ch == ' ')
        {
            if (! cur.empty()) v.push_back (std::atof (cur.c_str()));
            cur.clear();
        }
        else cur += ch;
    }
    return v;
}

/** "a/b/c, d/e/f" — tuples of three. */
inline std::vector<std::array<double, 3>> triples (const Section& s, const std::string& k)
{
    std::vector<std::array<double, 3>> out;
    const auto it = s.find (k);
    if (it == s.end() || it->second == "none") return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 2 < v.size(); i += 3) out.push_back ({ v[i], v[i + 1], v[i + 2] });
    return out;
}

inline std::vector<Xy> points (const Section& s, const std::string& k)
{
    std::vector<Xy> out;
    const auto it = s.find (k);
    if (it == s.end()) return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back ({ v[i], v[i + 1] });
    return out;
}

inline std::pair<double, double> range (const Section& s, const std::string& k, double lo, double hi)
{
    const auto it = s.find (k);
    if (it == s.end()) return { lo, hi };
    const auto dots = it->second.find ("..");
    if (dots == std::string::npos) { const double v = std::atof (it->second.c_str()); return { v, v }; }
    return { std::atof (it->second.substr (0, dots).c_str()), std::atof (it->second.substr (dots + 2).c_str()) };
}

inline std::string str (const Section& s, const std::string& k, const std::string& fallback = {})
{
    const auto it = s.find (k);
    return it == s.end() ? fallback : it->second;
}

/** A section with what it inherits laid under it, left to right, recursively. */
inline Section resolve (const std::vector<std::pair<std::string, Section>>& all, const std::string& name, int depth = 0)
{
    Section out;
    for (const auto& [n, s] : all)
        if (n == name)
        {
            const auto inh = str (s, "inherit");
            std::string cur;
            for (char ch : inh + ",")
            {
                if (ch == ',')
                {
                    const auto a = cur.find_first_not_of (' '), b = cur.find_last_not_of (' ');
                    if (a != std::string::npos && depth < 8)
                        for (auto& [k, v] : resolve (all, cur.substr (a, b - a + 1), depth + 1)) out[k] = v;
                    cur.clear();
                }
                else cur += ch;
            }
            for (auto& [k, v] : s) out[k] = v;
        }
    return out;
}

/** A unit-peak resonance at f0: the band-pass s(w0/Q) / (s^2 + s w0/Q + w0^2). */
inline cd resonance (double f, double f0, double q)
{
    const cd s (0, f / f0);
    return s / q / (s * s + s / q + 1.0);
}

inline cd highpass2 (double f, double f0, double q) { const cd s (0, f / f0); return s * s / (s * s + s / q + 1.0); }
inline cd lowpass2 (double f, double f0, double q)  { const cd s (0, f / f0); return 1.0 / (s * s + s / q + 1.0); }
inline cd lowpass1 (double f, double f0) { return 1.0 / cd (1, f / f0); }
inline cd highpass1 (double f, double f0) { const cd s (0, f / f0); return s / (1.0 + s); }

struct Mode { double f, q, residue; int radial, azimuthal; double phase, psi, eps; };

struct Cab
{
    // geometry
    std::vector<Xy> drivers;
    double radius = 0.105, depthFrac = 0.25, bw = 0.6, bh = 0.6;
    // low end
    bool vented = false;
    // Open at the back: each cone in free air but for the open box behind it, a pipe shut at the cone and open at the
    // back, `openLoss` its damping (openPipe); its back heard inverted from the back's plane, dark above `openLp`.
    bool open = false;
    double openLp = 150, openLoss = 0.48;
    // Half closed (the guitar's): open but for a part of the back, `halfClosed` of it — the cone on the box's air as much
    // as the back is closed (lowEnd), the air behind it and the opening's own a resonator that lets the back's sound out
    // under it and holds it above, and the depth's mode kept as much as the back is closed.
    bool half = false;
    double halfClosed = 0.5, halfQ = 1.2;
    // The guitar's open back as it is low down (open_back_full_below_step): under the baffle's step the box neither
    // shadows the back's sound nor leaves the front the half space alone — the back as loud there as its volume velocity
    // says, so it takes the low end the front makes. Off, the bass's: half, and blocked as at the top.
    bool openFullLow = false;
    // A wall behind the box (Spot::wall, the guitar's switch): back_wall_m behind the back's plane, back_wall_reflection of
    // the back's sound coming back off it — its image beyond the wall, heard as the back is. 0, none.
    double wallM = 0, wallRefl = 0;
    // What of the box's own modes an open back keeps (open_back_cavity_share): 1, the bass's, as closed.
    double openCavity = 1;
    // A closed box's Q against a sealed one's (box_q_factor): a guitar's closed back is no sealed box — it leaks, its
    // panels give, nothing damps it — and its hump over the cone on its air is lower than Qts sqrt(1 + alpha) says.
    // The sealed and the half closed; 1, the sealed box's — the bass's. And an open one's against the cone's free air
    // (open_q_factor): a small open combo rings round the cone's own resonance, a big open box hardly at all.
    double boxQ = 1, openQ = 1;
    // The box's echo (box_echo = gain/low Hz/high Hz): what the cone's back sends into a closed box comes off its walls
    // and out through the cone again — two depths, widths or heights round — `gain` of it each way round in the band
    // the cone lets it through — the 5-80 ms a closed cabinet keeps ringing in its mids, which close mics hear (measured, some 11 dB
    // under the whole at 1 kHz). A half closed back as much as it is closed; an open one none. 0, none — the bass's.
    // A fourth number, its ways round (box_echo = gain/low/high/paths): 3, the three axes; 7, each face's diagonal and
    // the box's own too — where a box's depth is half its width and its width its height, the three axes' trips come
    // round in step near c/(2 depth) and comb the mids (a 4x12's 480 and 950 Hz), which more ways round smear; 1, the
    // depth alone — the back the cone faces, the walls across sending it back no straighter than past it.
    double echoGain = 0, echoLow = 300, echoHigh = 3000;
    int echoPaths = 3;
    double fsd = 50, qts = 0.4, vas = 30, vb = 60, perChamber = 1, chambers = 1, fb = 45, ql = 8;
    bool portRear = false, hornAtMic = false;
    double fixedMass = 0, coilHz = 0, proxStrength = 1, proxCorner = 0, micTop = 12000, depth = 0.4;
    bool figure8 = false;   // a ribbon's: front and back alike, the back in the other polarity
    Xy port {};
    double portArea = 150, portLeakHz = 700;
    // cone
    double wooferLp = 5000, wooferQ = 0.707, decoupleHz = 1200;
    // A second section over it (woofer_lowpass_second = Hz/Q): a guitar's cone, flat to its knee and falling off a cliff
    // past it, which one second-order section cannot draw. 0, none — the bass's.
    double wooferLp2 = 0, wooferQ2 = 0.707;
    // The cap and the coil past the cone's cliff (cap_top = dB/from Hz/top Hz/Q): they go on speaking where the paper has
    // let go, the shelf measured guitar cabinets keep from 7.5 to 10 kHz some 25 dB under 1 kHz before falling off at
    // their own corner. A small piston at each cone's centre, its level against the whole cone gathered there, coming in
    // steeply from `from` — past the cone's cliff, so the two never meet where they would dig a pit — and falling off
    // from `top`. None — the bass's, and a cliff all the way down.
    double capTop = 0, capFrom = 8500, capHz = 11000, capQ = 0.707;
    // The top's tail (top_tail = dB/from Hz/ms): past the cone's breakup what it sends goes on ringing — the 5-80 ms every
    // measured cabinet keeps at 4-16 kHz, open or closed, some 28 dB under the whole — `dB` under the direct sound in
    // every band from `from` up, rising over its first 6 ms, then dying away `ms` at a time. None — the bass's.
    double tailTop = 0, tailFrom = 4000, tailMs = 12;
    // The box's tail (box_tail = dB/low Hz/high Hz/ms/open dB): what its air and panels keep ringing in the mids — the
    // 5-80 ms measured closed cabinets keep at 500 Hz-2 kHz some 11 dB under the whole, open ones some 8 dB less — `dB`
    // under the direct sound from `low` to `high`, a half closed back between the two, the same rise and fall as the
    // top's, its own noise. Not the echo's (box_echo): it rings without colouring. None — the bass's.
    double boxTail = 0, boxTailOpen = 0, boxTailLow = 400, boxTailHigh = 2000, boxTailMs = 12;
    // The paper's way past its breakup (cone_decouple_floor, the guitar's PAPER cone): what of the cone lets go keeps
    // `floor` of its motion, but scattered — each patch late by its own 0..floorJitterMs (fixed per patch, no draw) —
    // and falling off at the knee (floorHz, cone_floor_lowpass_hz, the woofer's lowpass by default). Not a piston's: it
    // does not sum in step at a far mic, nor cancel in step at a near one. 0, none — the bass's.
    double floor = 0, floorJitterMs = 0.3, floorHz = 0;
    std::vector<std::vector<Mode>> modes;   // per driver
    std::vector<double> sens;               // per driver, linear
    // box
    std::vector<std::array<double, 3>> body, cavity, panel;
    // horn
    bool horn = false;
    Xy hornXy {};
    double hornXo = 4000, hornQ = 0.707, hornDb = 0, hornLp = 17000, hornDepth = 0.04;
    std::vector<std::array<double, 3>> hornEq;
    // diffraction
    double diffDb = -24, diffLp = 4000;
    // mic
    double micHp = 35, micHpQ = 0.707, micLp = 12000, proxHz = 140, prox50 = 2.5, proxMax = 6, offLoss = 4;
    // mic_off_axis = dB/full Hz: the capsule's top lost past its pattern at 90 degrees, all of it from full Hz up
    double offAxisDb = 4, offAxisFull = 8000;
    std::vector<std::array<double, 3>> presence;
    double micD = 0.05, micOff = 0, micOffY = 0, tilt = 0;
    // Where the table's horn level (horn_at_mic_db) was taken: its section's own mic. A live mic (cabAt) moves off it,
    // and the horn's level follows the mic's real distance and angle to the horn from there.
    double hornRefD = 0.05, hornRefOff = 0, hornRefTilt = 0;
    double baffleStepDb = 0.5;
    // The floor the box stands on, floorGap under its bottom: what of a source's image under it comes back (0, no floor).
    double floorGap = 0, floorRefl = 0;
    // THE ROOM round it (Spot::room, the guitar's row; a table's "room-<name>" section): room_width_m across, room_length_m
    // from the wall behind the box to the one behind the mic, room_height_m from the floor up; the box's back room_back_m off
    // its wall, its middle room_side_m off the room's. Each wall gives back sqrt(1 - absorption) of what reaches it —
    // room_absorption up to 1 kHz, room_absorption_high from 4 kHz — the floor its floor_reflection. Early, every cone's
    // images to two bounces, the cone a piston narrowing as it lets go, the box shadowing what goes behind it; late, a tail
    // as long as Sabine's time says, its level as the room's absorption leaves it (room_tail scales it). 0 wide, none.
    double roomW = 0, roomL = 0, roomH = 0, roomBack = 1, roomSide = 0, roomAlpha = 0.3, roomAlphaHigh = 0.3, roomTail = 1;
    double lengthMs = 200, fadeMs = 30;
    double refDelay = -1;   // the time zero, s from the sound leaving: below zero, the first arrival's own
    double rate = fs;       // the IR's sample rate: the panel's and the app's 48 kHz, a pack's 44.1 or 96 too
};

inline Cab build (const Section& s)
{
    Cab c;
    c.drivers = points (s, "driver_xy");
    c.radius = num (s, "effective_radius_m", 0.105);
    c.depthFrac = num (s, "cone_depth_fraction_of_radius", 0.25);
    const auto baffle = nums (str (s, "baffle", "0.6,0.6,0.4"));
    c.bw = baffle.size() > 0 ? baffle[0] : 0.6;
    c.bh = baffle.size() > 1 ? baffle[1] : 0.6;

    c.vented = str (s, "alignment") == "ported";
    c.open = str (s, "alignment") == "open" || str (s, "alignment") == "half";
    c.half = str (s, "alignment") == "half";
    c.halfClosed = num (s, "half_back_closed_fraction", 0.5);
    c.halfQ = num (s, "half_back_Q", 1.2);
    c.openFullLow = num (s, "open_back_full_below_step", 0) > 0;
    c.wallM = num (s, "back_wall_m", 0);
    c.wallRefl = num (s, "back_wall_reflection", 0);
    c.openCavity = num (s, "open_back_cavity_share", 1);
    c.openLp = num (s, "open_back_lowpass_hz", 150);
    c.openLoss = num (s, "open_back_loss", 0.48);
    const auto ts = nums (str (s, "ts", "50,0.4,30"));
    c.fsd = ts[0]; c.qts = ts[1]; c.vas = ts[2];
    c.boxQ = num (s, "box_q_factor", 1);
    c.openQ = num (s, "open_q_factor", 1);
    if (const auto echo = nums (str (s, "box_echo")); echo.size() >= 3)
    {
        c.echoGain = echo[0];
        c.echoLow = echo[1];
        c.echoHigh = echo[2];
        c.echoPaths = echo.size() < 4 ? 3 : echo[3] >= 7 ? 7 : echo[3] < 2 ? 1 : 3;
    }
    const auto ch = nums (str (s, "chamber", "60,1,1"));
    c.vb = ch[0]; c.perChamber = ch.size() > 1 ? ch[1] : 1; c.chambers = ch.size() > 2 ? ch[2] : 1;
    c.depth = baffle.size() > 2 ? baffle[2] : 0.4;
    c.portRear = str (s, "port_face") == "rear";
    c.fixedMass = num (s, "cone_fixed_mass_fraction", 0);
    c.coilHz = num (s, "coil_corner_hz", 0);
    c.proxStrength = num (s, "proximity_strength", 1);
    c.proxCorner = num (s, "proximity_corner_hz", 0);
    c.figure8 = str (s, "mic_pattern") == "figure8";
    c.micTop = num (s, "mic_top_corner_hz", 0);
    c.hornAtMic = s.count ("horn_at_mic_db") > 0;
    c.fb = num (s, "fb_hz", 45);
    c.ql = num (s, "port_loss_Q", 8);
    const auto ports = points (s, "equivalent_port_xy");
    if (! ports.empty()) c.port = ports[0];
    c.portArea = num (s, "equivalent_port_area_cm2", 150);
    c.portLeakHz = num (s, "port_high_frequency_leakage_lowpass_hz", 700);
    if (str (s, "port_face") == "rear") c.portArea *= 0.35;   // heard round the box: weaker at the front

    c.wooferLp = num (s, "woofer_lowpass_hz", 5000);
    c.wooferQ = num (s, "woofer_envelope_Q", 0.707);
    c.floor = num (s, "cone_decouple_floor", 0);
    c.floorJitterMs = num (s, "cone_floor_jitter_ms", 0.3);
    c.floorHz = num (s, "cone_floor_lowpass_hz", 0) > 0 ? num (s, "cone_floor_lowpass_hz", 0) : c.wooferLp;
    if (const auto second = nums (str (s, "woofer_lowpass_second")); second.size() >= 2)
    {
        c.wooferLp2 = second[0];
        c.wooferQ2 = second[1];
    }
    if (const auto cap = nums (str (s, "cap_top")); cap.size() >= 4)
    {
        c.capTop = std::pow (10.0, cap[0] / 20);
        c.capFrom = cap[1];
        c.capHz = cap[2];
        c.capQ = cap[3];
    }
    if (const auto tail = nums (str (s, "top_tail")); tail.size() >= 3)
    {
        c.tailTop = std::pow (10.0, tail[0] / 20);
        c.tailFrom = tail[1];
        c.tailMs = tail[2];
    }
    if (const auto tail = nums (str (s, "box_tail")); tail.size() >= 5)
    {
        c.boxTail = std::pow (10.0, tail[0] / 20);
        c.boxTailLow = tail[1];
        c.boxTailHigh = tail[2];
        c.boxTailMs = tail[3];
        c.boxTailOpen = std::pow (10.0, tail[4] / 20);
    }
    c.decoupleHz = num (s, "cone_decouple_hz", range (s, "breakup_hz", 1200, 7000).first);
    c.body = triples (s, "body_eq");
    c.cavity = triples (s, "cavity_modes");
    if (c.cavity.empty()) c.cavity = triples (s, "cavity_modes_per_chamber");
    c.panel = triples (s, "panel_modes");

    c.horn = str (s, "horn", "present") != "absent" && ! points (s, "horn_xy").empty();
    if (c.horn) c.hornXy = points (s, "horn_xy")[0];
    c.hornXo = num (s, "horn_crossover_hz", 4000);
    c.hornQ = num (s, "horn_filter_Q", 0.707);
    c.hornDb = c.hornAtMic ? num (s, "horn_at_mic_db", -10)
                           : num (s, "horn_sensitivity_relative_one_woofer_db", 6) + num (s, "horn_attenuator_db", -6);
    c.hornLp = num (s, "horn_lowpass_hz", 17000);
    c.hornDepth = num (s, "horn_extra_acoustic_depth_m", 0.04);
    c.hornEq = triples (s, "horn_response_eq");

    c.diffDb = num (s, "diffraction_total_amplitude_db", -24);
    c.diffLp = num (s, "diffraction_lowpass_hz", 4000);

    c.micHp = num (s, "mic_highpass_hz", 35);
    c.micLp = num (s, "mic_lowpass_hz", 12000);
    c.presence = triples (s, "mic_presence_eq");
    c.proxHz = num (s, "proximity_shelf_hz", 140);
    c.prox50 = num (s, "proximity_gain_at_50mm_db", 2.5);
    c.proxMax = num (s, "proximity_max_db", 6);
    c.offLoss = num (s, "mic_extra_loss_at_6k_45deg_db", 4);
    if (const auto off = nums (str (s, "mic_off_axis")); off.size() >= 2)
    {
        c.offAxisDb = off[0];
        c.offAxisFull = off[1];
    }
    c.micD = num (s, "mic_distance_m", num (s, "mic_distance_from_cap_plane_m", 0.05));
    c.micOff = num (s, "mic_radial_offset_fraction", 0);
    c.tilt = num (s, "mic_axis_tilt_deg", 0);
    c.hornRefD = c.micD;
    c.hornRefOff = c.micOff;
    c.hornRefTilt = c.tilt;
    c.baffleStepDb = num (s, "baffle_step_depth_db", 0.5);
    c.floorGap = num (s, "floor_gap_m", 0);
    c.floorRefl = num (s, "floor_reflection", 0);
    c.roomW = num (s, "room_width_m", 0);
    c.roomL = num (s, "room_length_m", 0);
    c.roomH = num (s, "room_height_m", 0);
    c.roomBack = num (s, "room_back_m", 1);
    c.roomSide = num (s, "room_side_m", 0);
    c.roomAlpha = num (s, "room_absorption", 0.3);
    c.roomAlphaHigh = num (s, "room_absorption_high", c.roomAlpha);
    c.roomTail = num (s, "room_tail", 1);
    c.lengthMs = num (s, "length_ms", 200);
    c.fadeMs = num (s, "tail_fade_ms", 30);

    // The modes: seeded per cabinet, the same for every position (a position changes their weights, not them).
    std::mt19937 rng ((unsigned) num (s, "seed", 1));
    std::uniform_real_distribution<double> u (0, 1);
    const auto [b0, b1] = range (s, "breakup_hz", 1200, 7000);
    const auto [q0, q1] = range (s, "mode_Q", 3, 9);
    const auto [r0, r1] = range (s, "mode_residue_abs", 0.08, 0.4);
    const int count = (int) num (s, "mode_count", 16);
    const double gain = num (s, "mode_gain", 1);
    const double jitter = num (s, "mode_spacing_jitter_fraction", 0.2);
    const bool relative = s.count ("mode_level_rel_piston_db_first3") > 0;
    const auto first3 = range (s, "mode_level_rel_piston_db_first3", 4, 8), rest = range (s, "mode_level_rel_piston_db_rest", -2, 4);
    const double axisym = num (s, "mode_axisymmetric_share", 0.25), eps = num (s, "mode_asymmetry_eps", 0);
    const auto [ds0, ds1] = range (s, "driver_mode_frequency_spread_percent", -1.5, 1.5);
    const auto [dr0, dr1] = range (s, "driver_mode_residue_spread_db", -1, 1);
    const auto [dv0, dv1] = range (s, "driver_sensitivity_spread_db", -0.5, 0.5);
    std::vector<Mode> base;
    const double step = std::log (b1 / b0) / std::max (1, count - 1);
    for (int m = 0; m < count; ++m)
    {
        Mode md;
        md.f = b0 * std::exp (step * (m + jitter * (2 * u (rng) - 1)));
        md.q = q0 * std::pow (q1 / q0, u (rng));
        md.residue = gain * (r0 + (r1 - r0) * u (rng)) * (u (rng) < 0.5 ? -1 : 1);
        md.radial = (int) (u (rng) * 6.999);
        md.azimuthal = (int) (u (rng) * 3.999);
        md.phase = 2 * pi * u (rng);
        md.psi = pi * u (rng);
        md.eps = 0;
        if (relative)
        {
            // A level against the piston at its peak, the first three the loudest; now and then a dip instead.
            const auto [l0, l1] = m < 3 ? first3 : rest;
            const double lv = l0 + (l1 - l0) * u (rng);
            md.residue = std::pow (10.0, lv / 20) - 1;
            if (lv > 0 && u (rng) < 0.3) md.residue = -md.residue;
            md.azimuthal = u (rng) < axisym ? 0 : 1 + (int) (u (rng) * 2.999);
            md.eps = eps * (u (rng) < 0.5 ? -1 : 1);
        }
        base.push_back (md);
    }
    for (size_t d = 0; d < c.drivers.size(); ++d)
    {
        auto mine = base;
        for (auto& md : mine)
        {
            md.f *= 1 + 0.01 * (ds0 + (ds1 - ds0) * u (rng));
            md.residue *= std::pow (10.0, (dr0 + (dr1 - dr0) * u (rng)) / 20);
        }
        c.modes.push_back (mine);
        c.sens.push_back (std::pow (10.0, (dv0 + (dv1 - dv0) * u (rng)) / 20));
    }
    return c;
}

/** The cone's and the port's shares of the box's low end, at f: sealed, the cone alone; vented, the two apart; open,
    the cone on no air but its own — its free-air resonance, Qts. */
inline void lowEnd (const Cab& c, double f, cd& cone, cd& port)
{
    const double alpha = c.perChamber * c.vas / c.vb;
    const cd s (0, 2 * pi * f);
    if (c.open && ! c.half)
    {
        const double ws = 2 * pi * c.fsd;
        cone = s * s / (s * s + s * ws / (c.qts * c.openQ) + ws * ws);
        port = 0;
        return;
    }
    if (! c.vented)
    {
        // Half closed, a leaky box: the cone stiffened by as much of the box's air as the back holds.
        const double a = c.half ? alpha * c.halfClosed : alpha;
        const double wc = 2 * pi * c.fsd * std::sqrt (1 + a), qtc = c.qts * std::sqrt (1 + a) * c.boxQ;
        cone = s * s / (s * s + s * wc / qtc + wc * wc);
        port = 0;
        return;
    }
    const double ws = 2 * pi * c.fsd, wb = 2 * pi * c.fb, h = c.fb / c.fsd, w0 = std::sqrt (ws * wb);
    const double ql = c.ql, qt = c.qts;
    const double a1 = (ql + h * qt) / (std::sqrt (h) * ql * qt);
    const double a2 = (h + (alpha + 1 + h * h) * ql * qt) / (h * ql * qt);
    const double a3 = (h * ql + qt) / (std::sqrt (h) * ql * qt);
    const cd d = s * s * s * s + a1 * w0 * s * s * s + a2 * w0 * w0 * s * s + a3 * w0 * w0 * w0 * s + w0 * w0 * w0 * w0;
    cone = s * s * (s * s + s * wb / ql + wb * wb) / d;
    port = -s * s * (s * wb / ql + wb * wb) / d;
}

/** AN OPEN BACK'S PIPE, at f: the box behind each cone a pipe of its share of the back (bw x bh over the cones), as long
    as the box is deep plus the open end's correction (0.6 of the back's equivalent radius), shut at the cone and open at
    the back, damped by `openLoss` (radiation off a back that wide: Q about 2). The cone carries it — the driver's own
    mechanical impedance from fs, Qts and Vas against the pipe's input — so it moves less round the pipe's quarter wave,
    and the back hands out more there: `cone` what the cone's motion is multiplied by, `back` what the back hands out for
    what the cone moves. */
inline void openPipe (const Cab& c, double f, cd& cone, cd& back)
{
    constexpr double rho = 1.2;
    const double sd = pi * c.radius * c.radius, ws = 2 * pi * c.fsd, w = 2 * pi * f;
    const double cms = c.vas / 1000 / (rho * c0 * c0 * sd * sd), mms = 1 / (ws * ws * cms), rms = ws * mms / c.qts;
    const cd zm = cd (rms, w * mms - 1 / (w * cms));
    const double duct = c.bw * c.bh / (double) std::max<size_t> (1, c.drivers.size());
    const double length = c.depth + 0.6 * std::sqrt (c.bw * c.bh / pi);
    const cd kl (w / c0 * length, -c.openLoss);
    const cd zu = cd (0, 1) * (rho * c0 / duct) * std::tan (kl);
    cone = zm / (zm + sd * sd * zu);
    back = 1.0 / std::cos (kl);
}

struct Mic { double x, y, z, ax, ay, az; };

/** What the mic makes of a source at (dx,dy,dz) from it, at f: a cardioid, half pressure and half gradient — the
    gradient's 1/(jkr) its proximity, as strong as the mic is (proximity_strength), at the source's own distance — and the
    capsule's extra loss off its axis at the top (a dynamic's measured polar: 60 deg -2 dB @1k -5 @8k, 90 deg -6/-10). */
inline cd heard (const Cab& c, const Mic& m, double dx, double dy, double dz, double f)
{
    const double len = std::sqrt (dx * dx + dy * dy + dz * dz);
    const double cosT = std::clamp ((dx * m.ax + dy * m.ay + dz * m.az) / len, -1.0, 1.0);   // the axis points at the cab: in front, +1
    const double theta = std::acos (cosT) * 180 / pi;
    const double kr = 2 * pi * f / c0 * (len + 0.02);
    // Below its corner the proximity stops growing: there the capsule's own gradient path lets go.
    const cd prox = 0.55 * c.proxStrength / cd (0, kr) * (c.proxCorner > 0 ? highpass1 (f, c.proxCorner) : cd (1.0));
    if (c.figure8)   // all gradient: its proximity whole, no front-only loss at the top
        return cosT * (1.0 + prox);
    const cd pattern = 0.5 + 0.5 * cosT * (1.0 + prox);
    // Past its pattern the capsule loses its top off its axis, 4 dB at 90 degrees from 8 kHz up unless mic_off_axis says.
    const double e = (theta <= 60 ? 2.5 * theta / 60 : theta <= 90 ? 2.5 + 1.5 * (theta - 60) / 30 : 4 + 2 * (theta - 90) / 90)
                   * (c.offAxisDb == 4 ? 1.0 : c.offAxisDb / 4);
    const double top = std::clamp (std::log2 (f / 1000) / (c.offAxisFull == 8000 ? 3.0 : std::log2 (c.offAxisFull / 1000)), 0.0, 1.0);
    return pattern * std::pow (10.0, -e * top / 20);
}

/** `bin (k)` for every bin from 1 up to `bins`, spread over `threads` threads (0: up to four) in turns of the range — or one
    after the other where there are no threads (a page built without them). */
template <class F>
inline void forEachBin (size_t bins, const F& bin, size_t threadsAsked = 0)
{
#if defined(__EMSCRIPTEN__) && ! defined(__EMSCRIPTEN_PTHREADS__)
    for (size_t k = 1; k < bins; ++k)
        bin (k);
#else
    const size_t threads = threadsAsked > 0 ? threadsAsked : std::clamp<size_t> (std::thread::hardware_concurrency(), 1, 4);
    const size_t step = (bins - 1 + threads - 1) / threads;
    std::vector<std::future<void>> others;
    for (size_t t = 1; t < threads; ++t)
    {
        const size_t from = 1 + t * step, to = std::min (bins, from + step);
        if (from < to)
            others.push_back (std::async (std::launch::async, [&bin, from, to] { for (size_t k = from; k < to; ++k) bin (k); }));
    }
    for (size_t k = 1; k < std::min (bins, 1 + step); ++k)
        bin (k);
    for (auto& f : others)
        f.get();
#endif
}

/** J1, Bessel's of the first kind and order one (Numerical Recipes' rational fits, as good as a float), for a piston's beam. */
inline double besselJ1 (double x)
{
    const double ax = std::abs (x);
    if (ax < 8.0)
    {
        const double y = x * x;
        return x * (72362614232.0 + y * (-7895059235.0 + y * (242396853.1 + y * (-2972611.439 + y * (15704.48260 + y * -30.16036606)))))
             / (144725228442.0 + y * (2300535178.0 + y * (18583304.74 + y * (99447.43394 + y * (376.9991397 + y)))));
    }
    const double z = 8.0 / ax, y = z * z, xx = ax - 2.356194491;
    const double p = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4 + y * (0.2457520174e-5 + y * -0.240337019e-6)));
    const double q = 0.04687499995 + y * (-0.2002690873e-3 + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
    const double j = std::sqrt (0.636619772 / ax) * (std::cos (xx) * p - z * std::sin (xx) * q);
    return x < 0 ? -j : j;
}

/** A piston's beam, 2 J1(x) / x, x = ka sin(angle off its axis): 1 on the axis or below ka ~ 1, narrowing above. */
inline double pistonBeam (double x) { return std::abs (x) < 1e-6 ? 1.0 : 2 * besselJ1 (x) / x; }

inline std::vector<double> render (const Cab& c, size_t n, size_t threads = 0)
{
    if (c.drivers.empty())   // a box the table has not (cabAt of an unknown name): silence, never a cone read that is not there
        return std::vector<double> (n, 0.0);
    // A room's tail longer than the size asked for: the size doubled until it fits, the tail never wrapped round — but a
    // moving hand's render (the app's and the page's ask 2^13 then) keeps its size, the tail cut to it and faded, the one
    // kept when the hand lets go bringing it whole.
    if (c.roomW > 0 && n > ((size_t) 1 << 13))
        while ((double) n < 1.1 * c.lengthMs * c.rate / 1000)
            n *= 2;
    const size_t bins = n / 2 + 1;
    std::vector<cd> H (n, 0.0);

    // The mic, in front of the first cone, off its centre by a fraction of its radius, tilted toward +x.
    const Xy t = c.drivers.empty() ? Xy {} : c.drivers[0];
    const double tr = c.tilt * pi / 180;
    // Tilted toward the cap: off the cap, the axis leans back toward its centre; on it, toward -x.
    const double off = std::hypot (c.micOff, c.micOffY), ux = off > 1e-6 ? -c.micOff / off : -1.0, uy = off > 1e-6 ? -c.micOffY / off : 0.0;
    const Mic mic { t.x + c.micOff * c.radius, t.y + c.micOffY * c.radius, c.micD, std::sin (tr) * ux, std::sin (tr) * uy, -std::cos (tr) };

    // Each cone as patches: rings x spokes, the cone's depth behind the baffle growing toward its centre.
    constexpr int rings = 7, spokes = 16;
    struct Patch { double x, y, z, area, rho, phi; };
    std::vector<std::vector<Patch>> patches;
    for (const auto& d : c.drivers)
    {
        std::vector<Patch> ps;
        for (int r = 0; r < rings; ++r)
        {
            const double r0 = (double) r / rings, r1 = (double) (r + 1) / rings, rho = 0.5 * (r0 + r1);
            for (int k = 0; k < spokes; ++k)
            {
                const double phi = 2 * pi * (k + 0.5 * (r % 2)) / spokes;
                ps.push_back ({ d.x + rho * c.radius * std::cos (phi), d.y + rho * c.radius * std::sin (phi),
                                -c.depthFrac * c.radius * (1 - rho), (r1 * r1 - r0 * r0) / spokes, rho, phi });
            }
        }
        patches.push_back (ps);
    }

    // Each patch's path to the mic: its delay, its 1/r, the mic's pattern — and its modes' weights there.
    const auto pathTo = [&] (double x, double y, double z, double& r, double& dx, double& dy, double& dz) {
        dx = x - mic.x; dy = y - mic.y; dz = z - mic.z;
        r = std::sqrt (dx * dx + dy * dy + dz * dz);
    };

    // The floor: every cone, the port and the horn have their image under it, heard from below. Close to a cone the image
    // is far and faint; a hand's span back it combs the low mids, and the box's low end gains the floor.
    const double floorY = -c.bh / 2 - c.floorGap;
    const int images = c.floorRefl > 0 ? 2 : 1;
    const auto mirrored = [&] (int img, double y) { return img == 0 ? y : 2 * floorY - y; };

    // The four edges of the baffle, each a weak inverted source fed by the first cone, at the edge's nearest point.
    const Xy edges[4] { { -c.bw / 2, t.y }, { c.bw / 2, t.y }, { t.x, -c.bh / 2 }, { t.x, c.bh / 2 } };

    // The paper's scatter, once: each patch's own lateness, from its place alone (a hash, not the seeded draws).
    std::vector<std::vector<double>> floorTau (patches.size());
    if (c.floor > 0)
        for (size_t d = 0; d < patches.size(); ++d)
            for (size_t i = 0; i < patches[d].size(); ++i)
            {
                uint32_t h = (uint32_t) (d * 7919u + i * 104729u + 1u);
                h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
                floorTau[d].push_back ((double) h / 4294967296.0 * c.floorJitterMs * 1e-3);
            }

    // Each mode's shape at each patch, once: a position weighs a mode, it does not move it.
    std::vector<std::vector<std::vector<double>>> shape (patches.size());
    for (size_t d = 0; d < patches.size(); ++d)
        for (const auto& md : c.modes[d])
        {
            std::vector<double> w;
            for (const auto& p : patches[d])
                w.push_back (std::cos (pi * md.radial * p.rho + md.psi)
                             * (md.azimuthal == 0 ? 1.0 : std::cos (md.azimuthal * p.phi + md.phase) + md.eps));
            shape[d].push_back (w);
        }

    double minDelay = 1e9;
    for (size_t d = 0; d < patches.size(); ++d)
        for (const auto& p : patches[d])
        {
            double r, dx, dy, dz;
            pathTo (p.x, p.y, p.z, r, dx, dy, dz);
            minDelay = std::min (minDelay, r / c0);
        }
    if (c.refDelay >= 0)
        minDelay = c.refDelay;   // two mics on one clock: the later one arrives late by its path

    // The coil's inductance: half a slope (-3 dB/oct) above its corner, minimum-phase — the woofer's alone.
    std::vector<cd> coil (n, 1.0);
    if (c.coilHz > 0)
    {
        std::vector<double> mag (n / 2 + 1);
        for (size_t k = 0; k <= n / 2; ++k) { const double f = c.rate * (double) k / (double) n; mag[k] = std::pow (1 + (f / c.coilHz) * (f / c.coilHz), -0.25); }
        coil = minimumPhase (mag, n);
    }
    // The target cone's own near-field sum, as the cones' sum is scaled below: what the port and the horn stand against.
    double near = 0;
    for (const auto& p : patches[0]) { double r, dx, dy, dz; pathTo (p.x, p.y, p.z, r, dx, dy, dz); near += p.area / r; }
    // The horn's level is the table's where the table's mic stood (hornRef*): the mic there, and the target cone's near sum
    // there — so a mic moved nearer the horn hears more of it, one moved away or back from the cone hears the balance shift.
    const double refTr = c.hornRefTilt * pi / 180, refUx = c.hornRefOff > 1e-6 ? -1.0 : c.hornRefOff < -1e-6 ? 1.0 : -1.0;
    const Mic micRef { t.x + c.hornRefOff * c.radius, t.y, c.hornRefD, std::sin (refTr) * refUx, 0.0, -std::cos (refTr) };
    double nearRef = 0;
    for (const auto& p : patches[0])
    {
        const double dx = p.x - micRef.x, dy = p.y - micRef.y, dz = p.z - micRef.z;
        nearRef += p.area / std::sqrt (dx * dx + dy * dy + dz * dz);
    }
    const double hdxR = c.hornXy.x - micRef.x, hdyR = c.hornXy.y - micRef.y, hdzR = -c.hornDepth - micRef.z;
    const double hrR = std::sqrt (hdxR * hdxR + hdyR * hdyR + hdzR * hdzR);
    // The port against the cone at a cone-close mic (Keele's near field against the port's far field): n Sd / (2 pi r Delta).
    const double dc = c.micD + c.depthFrac * c.radius, delta = std::sqrt (dc * dc + c.radius * c.radius) - dc;
    std::vector<Biquad> bodyEq, presence, hornEq;
    for (auto& e : c.body) bodyEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], c.rate));
    for (auto& e : c.presence) presence.push_back (Biquad::make ("peak", e[0], e[1], e[2], c.rate));
    for (auto& e : c.hornEq) hornEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], c.rate));
    const double stepDb = c.baffleStepDb, stepHz = 115.0 / c.bw;

    // THE ROOM (Cab::roomW): each cone's images in its walls, ceiling and floor to two bounces — the floor's alone left to
    // the patches' own image above — and its back's too where the box is open, each facing as the mirror turns it.
    struct RoomImage { double x, y, z, facing; int walls, floors; };
    std::vector<std::vector<RoomImage>> roomFront, roomRear;
    std::vector<cd> tailLow, tailHigh;   // the late part: one noise, as the room keeps the low end and the top
    const double aBox = std::sqrt (c.bw * c.bh / pi);   // the box as a disc: what it shadows behind it
    if (c.roomW > 0)
    {
        const double ax = c.roomSide - c.roomW / 2, bx = c.roomSide + c.roomW / 2, ay = floorY, by = floorY + c.roomH;
        const double az = -c.depth - c.roomBack, bz = az + c.roomL;
        struct Along { double q; int low, high; };   // one axis's image: where, and how many off its low wall and its high
        const auto along = [] (double q, double a, double b) {
            return std::vector<Along> { { q, 0, 0 }, { 2 * a - q, 1, 0 }, { 2 * b - q, 0, 1 }, { q + 2 * (b - a), 1, 1 }, { q - 2 * (b - a), 1, 1 } };
        };
        const auto imagesOf = [&] (double x, double y, double z, double facing) {
            std::vector<RoomImage> out;
            for (const auto& ix : along (x, ax, bx))
                for (const auto& iy : along (y, ay, by))
                    for (const auto& iz : along (z, az, bz))
                    {
                        const int nx = ix.low + ix.high, ny = iy.low + iy.high, nz = iz.low + iz.high;
                        if (nx + ny + nz == 0 || nx + ny + nz > 2 || (nx == 0 && nz == 0 && iy.low == 1 && iy.high == 0))
                            continue;
                        out.push_back ({ ix.q, iy.q, iz.q, nz % 2 ? -facing : facing, nx + iy.high + nz, iy.low });
                    }
            return out;
        };
        for (const auto& d : c.drivers)
        {
            roomFront.push_back (imagesOf (d.x, d.y, 0, 1));
            roomRear.push_back (imagesOf (d.x, d.y, -c.depth, -1));
        }

        // Late, a tail: Sabine's time and the room's constant from its size and absorption — the reverberant against the
        // direct sound at the mic's distance (8 pi r^2 / R, the box radiating into half the space) — its noise building up
        // between the first bounce and the last of the two, then dying away; fixed per place in quarter metres, no draw.
        double rc, dxc, dyc, dzc;
        pathTo (t.x, t.y, -c.depthFrac * c.radius, rc, dxc, dyc, dzc);
        const double V = c.roomW * c.roomL * c.roomH, S = 2 * (c.roomW * c.roomL + c.roomW * c.roomH + c.roomL * c.roomH);
        const double t1 = 0.5 * std::min ({ c.roomW, c.roomL, c.roomH }) / c0, t2 = 2 * std::max ({ c.roomW, c.roomL, c.roomH }) / c0;
        const size_t whole = (size_t) std::lround (c.lengthMs * c.rate / 1000), len = std::min (n, whole);   // ...cut, a hand moving
        uint32_t seed = 2166136261u;
        for (const double v : { std::round (mic.x * 4), std::round (mic.y * 4), std::round (mic.z * 4), c.roomW, c.roomL, c.roomH })
            seed = (seed ^ (uint32_t) std::llround (v * 1000 + 1e6)) * 16777619u;
        std::vector<double> noise (whole);
        for (auto& x : noise)
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            x = (double) seed / 2147483648.0 - 1.0;
        }
        const auto tail = [&] (double absorption) {
            const double alpha = std::clamp (absorption, 0.01, 0.99), rt = 0.161 * V / (S * alpha), R = S * alpha / (1 - alpha);
            const double energy = c.roomTail * 8 * pi * rc * rc / R * std::exp (-13.8 * 0.5 * (t1 + t2) / rt);
            std::vector<cd> x (n, 0.0);
            double e = 0;   // over the whole tail, so a cut one keeps its level
            for (size_t i = 0; i < whole; ++i)
            {
                const double ti = (double) i / c.rate, rise = std::clamp ((ti - t1) / (t2 - t1), 0.0, 1.0);
                const double v = noise[i] * rise * std::exp (-6.91 * ti / rt);
                e += v * v;
                if (i < len)
                    x[i] = v;
            }
            const double g = e > 0 ? std::sqrt (energy / e) : 0.0;
            for (auto& v : x) v *= g;
            fft (x, false);
            return x;
        };
        tailLow = tail (c.roomAlpha);
        tailHigh = tail (c.roomAlphaHigh);
    }
    // The top's tail (Cab::tailTop): one noise per box, the same at every place — rising from 1 ms to 6 ms, then dying
    // away tailMs at a time — of a whole energy of one, so a bin's direct sound carries it on at tailTop of itself.
    // The box's tail (Cab::boxTail) the same way, its noise its own.
    const auto lateNoise = [&] (uint32_t salt, double decayMs)
    {
        uint32_t seed = 2166136261u;
        for (const double v : { c.bw, c.bh, c.depth, (double) c.drivers.size() })
            seed = (seed ^ (uint32_t) std::llround (v * 1000 + 1e6)) * 16777619u;
        seed ^= salt;
        std::vector<cd> x (n, 0.0);
        double e = 0;
        for (size_t i = 0; i < n; ++i)
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            const double ms = 1000.0 * (double) i / c.rate, rise = std::clamp ((ms - 1) / 5, 0.0, 1.0);
            const double v = ((double) seed / 2147483648.0 - 1.0) * rise * std::exp (-ms / decayMs);
            x[i] = v;
            e += v * v;
        }
        for (auto& v : x) v /= std::sqrt (e);
        fft (x, false);
        return x;
    };
    const std::vector<cd> topTail = c.tailTop > 0 ? lateNoise (0, c.tailMs) : std::vector<cd> {};
    const std::vector<cd> boxTail = c.boxTail > 0 ? lateNoise (0x9e3779b9u, c.boxTailMs) : std::vector<cd> {};
    const double boxTailNow = c.boxTail * (! c.open ? 1.0 : c.half ? c.halfClosed : 0.0)
                            + c.boxTailOpen * (! c.open ? 0.0 : c.half ? 1 - c.halfClosed : 1.0);
    // What a room's images bring a mic: each off its walls (sqrt(1 - absorption), 1 kHz to 4 kHz from the low to the high)
    // and the floor, a piston beaming `ka` toward the mic from where it stands — behind it, the box's shadow.
    const auto roomHeard = [&] (const std::vector<RoomImage>& ims, double ka, double f) {
        const double k = 2 * pi * f / c0, up = std::clamp (std::log2 (f / 1000) / 2, 0.0, 1.0);
        const double wall = std::sqrt (1 - std::clamp (c.roomAlpha + (c.roomAlphaHigh - c.roomAlpha) * up, 0.0, 0.99));
        const double shadow = 1 / std::sqrt (1 + (k * aBox) * (k * aBox));
        cd at = 0.0;
        for (const auto& im : ims)
        {
            double r, dx, dy, dz;
            pathTo (im.x, im.y, im.z, r, dx, dy, dz);
            const double cosT = -dz * im.facing / r;   // from the image to the mic, against the way it faces
            const double beam = cosT >= 0 ? pistonBeam (ka * std::sqrt (1 - cosT * cosT)) : pistonBeam (ka) * shadow;
            at += std::pow (c.floorRefl, im.floors) * std::pow (wall, im.walls) * beam * heard (c, mic, dx, dy, dz, f) / r
                * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
        }
        return at;
    };

    // Each bin on its own — nothing below writes what another bin reads — so the bins are spread over a few threads.
    const auto bin = [&] (size_t k)
    {
        const double f = c.rate * (double) k / (double) n;
        cd cone, port;
        lowEnd (c, f, cone, port);
        cd pipe = 0.0;   // an open back's: what it hands out for what the cone moves
        if (c.open)
        {
            cd by;
            openPipe (c, f, by, pipe);
            if (! c.half)
                cone *= by;
            else   // half closed: the pipe's hold on the cone as much as the back is open, the box's air the rest (lowEnd)
                cone *= 1.0 + (by - 1.0) * (1 - c.halfClosed);
        }

        // What the box adds to every cone's motion: its cavity and panel modes, then the voicing.
        cd box = 1.0;
        // An open back lets go of the depth's mode, the air shut between baffle and back; across, the box still rings.
        for (auto& m : c.cavity)
            if (c.open && c.openCavity != 1.0 && ! (std::abs (m[0] - c0 / (2 * c.depth)) < 0.08 * m[0]))
                // the guitar's: an open back lets the modes across go too, a half closed one half of that
                box += (c.half ? 1 - (1 - c.openCavity) * (1 - c.halfClosed) : c.openCavity) * m[2] * resonance (f, m[0], m[1]);
            else if (! (c.open && std::abs (m[0] - c0 / (2 * c.depth)) < 0.08 * m[0]))
                box += m[2] * resonance (f, m[0], m[1]);
            else if (c.half)   // half closed: as much of it as the back is
                box += c.halfClosed * m[2] * resonance (f, m[0], m[1]);
        for (auto& m : c.panel) box += m[2] * resonance (f, m[0], m[1]);
        for (auto& b : bodyEq) box *= b.at (f, c.rate);
        // The box's echo (Cab::echoGain): every two depths, through the cone again, in its band.
        // Off the back, the sides and the top and bottom alike, a third of it each way round: the box's three round
        // trips spread its echo's peaks and pits, as a box's many paths do, where one delay alone would comb it.
        // Under zero it pulls: what comes back presses on the cone's back as the cone's back sent it, against its motion —
        // a sealed box's dips where its trips come round in step (c/(2 depth), 476 Hz in a 4x12) and its rises between.
        if (const double g = c.echoGain * (! c.open ? 1.0 : c.half ? c.halfClosed : 0.0); g != 0)
        {
            const auto trip = [f] (double metres) { return std::polar (1.0, -2 * pi * f * 2 * metres / c0); };
            const double d = c.depth, w = c.bw, h = c.bh;
            const cd ways = c.echoPaths == 7 ? (trip (d) + trip (w) + trip (h) + trip (std::hypot (d, w)) + trip (std::hypot (d, h))
                                                + trip (std::hypot (w, h)) + trip (std::sqrt (d * d + w * w + h * h))) / 7.0
                                             : c.echoPaths == 1 ? trip (d) : (trip (d) + trip (w) + trip (h)) / 3.0;
            // The depth alone: its band a weight on what comes back, not a filter turning its phase — its dips stand where
            // the depth puts them.
            const cd band = highpass2 (f, c.echoLow, 0.707) * lowpass2 (f, c.echoHigh, 0.707);
            const cd round = c.echoPaths == 1 ? g * std::abs (band) * ways : g * highpass2 (f, c.echoLow, 0.707) * lowpass2 (f, c.echoHigh, 0.707) * ways;
            box *= 1.0 / (1.0 - round);
        }
        // The baffle step: the low end, radiating into the whole room, a few tenths of a dB under the top.
        box *= std::pow (10.0, -stepDb / 20 * (1 / (1 + (f / stepHz) * (f / stepHz))));

        const cd piston = lowpass2 (f, c.wooferLp, c.wooferQ) * (c.wooferLp2 > 0 ? lowpass2 (f, c.wooferLp2, c.wooferQ2) : cd (1.0));
        // The cap past the cone's knee (Cab::capTop), against a whole cone gathered at its centre.
        const cd capped = c.capTop > 0 ? c.capTop * highpass2 (f, c.capFrom, 0.707) * highpass2 (f, c.capFrom, 0.707) * lowpass2 (f, c.capHz, c.capQ)
                                       : cd (0.0);
        cd sum = 0.0;
        cd roomFed = 0.0;      // a room's: the cones' whole output, what its tail is fed
        double roomKa = 0;     // ...and how the cones beam it, their moving part a piston
        for (size_t d = 0; d < patches.size(); ++d)
        {
            // Each mode's resonance at f, once per cone.
            std::vector<cd> res (c.modes[d].size());
            for (size_t m = 0; m < res.size(); ++m) res[m] = c.modes[d][m].residue * resonance (f, c.modes[d][m].f, c.modes[d][m].q);
            // The part of the cone still moving, as an area: what lets go hands its share to the centre, which the coil
            // drives the harder for carrying less — the cone's output kept, its radiation drawn in toward the cap.
            double whole = 0, moving = 0;
            for (const auto& p : patches[d])
            {
                const double let = p.rho * f / c.decoupleHz;
                whole += p.area;
                moving += p.area / (1 + let * let);
            }
            // The coil and the cap keep their mass as the outer cone lets go (cone_fixed_mass_fraction).
            const cd held = c.fixedMass > 0 ? piston / (c.fixedMass + (1 - c.fixedMass) * moving / whole) : piston * (whole / moving);
            cd drv = 0.0;
            cd whole1 = 0.0;   // a room's: the cone's whole motion, as far away on its axis
            for (size_t pi_ = 0; pi_ < patches[d].size(); ++pi_)
            {
                const auto& p = patches[d][pi_];
                // Above its breakup the cone stops moving whole: its outer part lets go first, the centre keeps on.
                const double let = p.rho * f / c.decoupleHz;
                // The modes ride on what the cone carries there: above its breakup they are most of what it does.
                cd v = 1.0 / (1 + let * let);
                if (c.floor > 0)   // the paper's: what let go keeps a share of its motion, scattered in time
                    v += c.floor / (1 + (f / c.floorHz) * (f / c.floorHz)) * (let * let / (1 + let * let))
                       * std::polar (1.0, -2 * pi * f * floorTau[d][pi_]);
                for (size_t m = 0; m < res.size(); ++m)
                    v += res[m] * shape[d][m][pi_];
                v *= held;
                cd at = 0.0;
                for (int img = 0; img < images; ++img)
                {
                    double r, dx, dy, dz;
                    pathTo (p.x, mirrored (img, p.y), p.z, r, dx, dy, dz);
                    at += (img == 0 ? 1.0 : c.floorRefl) * heard (c, mic, dx, dy, dz, f) / r * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
                drv += v * p.area * at;
                if (c.roomW > 0)
                    whole1 += v * p.area * std::polar (1.0, 2 * pi * f * p.z / c0);
            }
            // The cap: a small piston at the cone's centre, heard as it is — not as the paper round it lets go.
            if (c.capTop > 0)
                for (int img = 0; img < images; ++img)
                {
                    double r, dx, dy, dz;
                    pathTo (c.drivers[d].x, mirrored (img, c.drivers[d].y), -c.depthFrac * c.radius, r, dx, dy, dz);
                    drv += (img == 0 ? 1.0 : c.floorRefl) * capped * heard (c, mic, dx, dy, dz, f) / r
                         * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
            sum += drv * c.sens[d] * cone * box * coil[k];
            if (c.roomW > 0)   // the room's images of this cone, its moving part beaming as a piston
            {
                const cd out = whole1 * c.sens[d] * cone * box * coil[k];
                roomKa = 2 * pi * f / c0 * c.radius * std::sqrt (moving / whole);
                sum += out * roomHeard (roomFront[d], roomKa, f);
                roomFed += out;
            }
        }
        // Normalise the cones' sum to the nearest cone's 1/r at its centre, so the port and the horn sit against it.
        double r0, dx0, dy0, dz0;
        pathTo (t.x, t.y, -c.depthFrac * c.radius, r0, dx0, dy0, dz0);
        sum *= r0 / 1.0;

        if (c.vented)
            for (int img = 0; img < images; ++img)
            {
                const double back = img == 0 ? 1.0 : c.floorRefl;
                double r, dx, dy, dz;
                pathTo (c.port.x, mirrored (img, c.port.y), 0, r, dx, dy, dz);
                if (c.fixedMass > 0)
                {
                    // Round the box when it is at the back: the path wraps over the depth and half the width, and less arrives.
                    const double wrap = c.portRear ? c.depth + 0.5 * c.bw : 0, rp = r + wrap;
                    const double ratio = c.perChamber * c.chambers * pi * c.radius * c.radius / (2 * pi * rp * delta) * (c.portRear ? 0.35 : 1);
                    sum += back * port * coil[k] * ratio * (r0 * near) * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f)
                         * std::polar (1.0, -2 * pi * f * (rp / c0 - minDelay));
                }
                else
                {
                    const double area = c.portArea / (pi * c.radius * c.radius * 1e4);   // against one cone's
                    sum += back * port * area * c.perChamber * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f) * (r0 / r)
                         * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
            }
        // An open back: each cone's back, inverted, out of the back's plane straight to the mic — a box this small does
        // not shadow a bass's wavelength, its baffle only stands in the way (B: a rigid disc of the baffle's area in the
        // flow from a source `depth` behind it, at the mic `micD` in front) — into the whole space where the front has
        // half (1/2), as the cone's volume velocity in the cones' own 1/r (r0/d), dark above openLp as the box shadows it
        // more; the floor's image too, as every source has.
        if (c.open)
        {
            const double disc = std::sqrt (c.bw * c.bh / pi), z = std::max (c.micD, 0.005);
            // Half closed: the back's sound through the box's resonator — the opening's air on the air behind the cones
            // (Helmholtz: c/2pi sqrt(A / V L), L the panel and the opening's two ends) — out under it, held above it.
            cd halfOut = 1.0;
            const double low = 1 / (1 + (f / stepHz) * (f / stepHz));   // under the baffle's step, 1
            if (c.half)
            {
                const double area = (1 - c.halfClosed) * c.bw * c.bh, volume = c.bw * c.bh * c.depth;
                const double neck = 0.015 + 1.7 * std::sqrt (area / pi);
                halfOut = lowpass2 (f, c0 / (2 * pi) * std::sqrt (area / (volume * neck)), c.halfQ);
            }
            for (size_t d = 0; d < c.drivers.size(); ++d)
                for (int img = 0; img < images; ++img)
                {
                    const double floorBy = img == 0 ? 1.0 : c.floorRefl;
                    double r, dx, dy, dz;
                    pathTo (c.drivers[d].x, mirrored (img, c.drivers[d].y), -c.depth, r, dx, dy, dz);
                    const double block = std::clamp (1 - 2 / pi * (disc - z * std::atan (disc / z)) * r / (c.depth * c.depth), 0.2, 1.0);
                    if (! c.openFullLow)
                        sum -= floorBy * cone * pipe * coil[k] * c.sens[d] * 0.5 * block * (r0 / r) * (c.half ? halfOut : lowpass1 (f, c.openLp))
                             * heard (c, mic, dx, dy, dz, f) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                    else
                    {
                        // The guitar's: whole and unshadowed under the baffle's step, half and blocked over it.
                        const auto share = [&] (double blocked) { return (0.5 + 0.5 * low) * (blocked + (1 - blocked) * low); };
                        sum -= floorBy * cone * pipe * coil[k] * c.sens[d] * share (block) * (r0 / r) * (c.half ? halfOut : lowpass1 (f, c.openLp))
                             * heard (c, mic, dx, dy, dz, f) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                        // ...and off the wall behind, where there is one: the back's image beyond it, round the box.
                        // (A room's back wall stands in for it: cabAt leaves wallM nought with a room.)
                        if (c.wallM > 0)
                        {
                            double rw, wx, wy, wz;
                            pathTo (c.drivers[d].x, mirrored (img, c.drivers[d].y), -c.depth - 2 * c.wallM, rw, wx, wy, wz);
                            const double blockW = std::clamp (1 - 2 / pi * (disc - z * std::atan (disc / z)) * rw / (c.depth * c.depth), 0.2, 1.0);
                            sum -= floorBy * c.wallRefl * cone * pipe * coil[k] * c.sens[d] * share (blockW) * (r0 / rw)
                                 * (c.half ? halfOut : lowpass1 (f, c.openLp)) * heard (c, mic, wx, wy, wz, f)
                                 * std::polar (1.0, -2 * pi * f * (rw / c0 - minDelay));
                        }
                    }
                }
            if (c.roomW > 0)   // the back's sound in the room too: out behind the box, half of it over the baffle's step
                for (size_t d = 0; d < c.drivers.size(); ++d)
                {
                    const cd back = cone * pipe * coil[k] * c.sens[d] * (c.half ? halfOut : lowpass1 (f, c.openLp))
                                  * (c.openFullLow ? 0.5 + 0.5 * low : 0.5);
                    sum -= back * r0 * roomHeard (roomRear[d], roomKa, f);
                    roomFed = std::sqrt (std::norm (roomFed) + std::norm (back));   // ...and feeds its tail beside the front
                }
        }
        if (c.horn)
        {
            cd hn = highpass2 (f, c.hornXo, c.hornQ) * lowpass2 (f, c.hornLp, 0.707) * std::pow (10.0, c.hornDb / 20);
            for (auto& b : hornEq) hn *= b.at (f, c.rate);
            for (int img = 0; img < images; ++img)
            {
                const double back = img == 0 ? 1.0 : c.floorRefl;
                double r, dx, dy, dz;
                pathTo (c.hornXy.x, mirrored (img, c.hornXy.y), -c.hornDepth, r, dx, dy, dz);
                if (c.hornAtMic)   // its level against the cone's pass band where the table's mic stood, then as this mic hears it from there
                {
                    const double there = std::abs (heard (c, micRef, hdxR, hdyR, hdzR, f)) / hrR;
                    const double here = std::abs (heard (c, mic, dx, dy, dz, f)) / r;
                    sum += back * hn * (r0 * nearRef) * (here / there) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
                else
                    sum += back * hn * heard (c, mic, dx, dy, dz, f) * (r0 / r) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
            }
        }
        // The baffle's edges: what the first cone sends along the baffle, inverted, coming back late and dark.
        {
            const double each = std::pow (10.0, c.diffDb / 20) / 4;
            for (const auto& e : edges)
            {
                const double toEdge = std::hypot (e.x - t.x, e.y - t.y);
                double r, dx, dy, dz;
                pathTo (e.x, e.y, 0, r, dx, dy, dz);
                sum -= each * cone * piston * coil[k] * lowpass2 (f, c.diffLp, 0.707) * heard (c, mic, dx, dy, dz, f)
                     * std::polar (1.0, -2 * pi * f * ((toEdge + r) / c0 - minDelay));
            }
        }

        // The top's tail (Cab::tailTop): the direct sound's own spectrum, carried on late from tailFrom up. The box's
        // (Cab::boxTail) the same in the mids, as closed as the back is, its band a weight on it and no turn of phase.
        const double direct = std::abs (sum);
        if (c.tailTop > 0)
            sum += topTail[k] * direct * c.tailTop * highpass2 (f, c.tailFrom, 0.707);
        if (c.boxTail > 0)
            sum += boxTail[k] * direct * boxTailNow * std::abs (highpass2 (f, c.boxTailLow, 0.707) * lowpass2 (f, c.boxTailHigh, 0.707));

        // The room's tail: what the cones put into the whole room — beamed (sqrt(2 R1(2ka)) / ka of the axis, 1 low down)
        // — as the mic hears a sound from everywhere (its pattern's mean over six ways), the room keeping less of the top.
        if (c.roomW > 0)
        {
            const double r1 = roomKa < 0.05 ? 0.5 * roomKa * roomKa : 1 - besselJ1 (2 * roomKa) / roomKa;
            const double beam = roomKa < 0.05 ? 1.0 : std::sqrt (2 * r1) / roomKa;
            double around = 0;
            for (const auto& u : { std::array<double, 3> { 3, 0, 0 }, { -3, 0, 0 }, { 0, 3, 0 }, { 0, -3, 0 }, { 0, 0, 3 }, { 0, 0, -3 } })
                around += std::norm (heard (c, mic, u[0], u[1], u[2], f)) / 6;
            const double up = std::clamp (std::log2 (f / 500) / 3, 0.0, 1.0);   // 500 Hz to 4 kHz, the low tail to the high
            sum += (tailLow[k] * (1 - up) + tailHigh[k] * up) * std::abs (roomFed) * beam * std::sqrt (around);
        }

        // The mic: its capsule's own response, and the proximity it hears at this distance.
        cd capsule = highpass2 (f, c.micHp, 0.707) * (c.micTop > 0 ? lowpass1 (f, c.micTop) : lowpass2 (f, c.micLp, 0.707));
        for (auto& b : presence) capsule *= b.at (f, c.rate);
        H[k] = sum * capsule;
    };
    forEachBin (bins, bin, threads);
    for (size_t k = 1; k < n / 2; ++k) H[n - k] = std::conj (H[k]);
    H[0] = 0;
    fft (H, true);

    const int len = (int) std::min ((double) n, (double) std::lround (c.lengthMs * c.rate / 1000)), fade = (int) std::lround (c.fadeMs * c.rate / 1000);
    std::vector<double> ir ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        double w = 1;
        if (i >= len - fade) w = 0.5 + 0.5 * std::cos (pi * (i - (len - fade)) / fade);
        ir[(size_t) i] = H[(size_t) i].real() * w;
    }
    return ir;
}

/** Loudness: the IR's energy under a bass programme's spectrum (pink, 40 Hz .. 5 kHz), as a gain to a common level. */
inline double loudness (const std::vector<double>& ir, size_t n)
{
    std::vector<cd> X (n, 0.0);
    for (size_t i = 0; i < ir.size() && i < n; ++i) X[i] = ir[i];
    fft (X, false);
    double e = 0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f = fs * (double) k / (double) n;
        if (f < 40 || f > 5000) continue;
        e += std::norm (X[k]) / f;
    }
    return std::sqrt (e);
}

/** WHERE THE MIC STANDS, for a cabinet rendered live: across the first speaker in its radii from the cap (x right,
    y up), and how far in front of the cap's plane; the horn on or off where the box has one, and its attenuator; and
    the box's back, as the table has it or as the player turns it. */
struct Spot
{
    double x = 0, y = 0, distance = 0.05;
    bool horn = true;
    std::string mic;    // a "mic-<name>" section of the table laid over the cabinet's own, or empty for its own
    double angle = 0;   // degrees the mic leans back toward the cap
    double hornDb = 0;  // the horn's level against the table's, its attenuator's step: 0, -6, -12
    int back = -1;      // -1 the table's; 0 closed, 1 ported, 2 open (Back)
    std::string speaker;   // a "speaker-<name>" section laid over the cabinet's own speaker, or empty for its own
    bool wall = false;     // the wall behind the box the table places (back_wall_m), the guitar's switch: off, none
    std::string cone;      // a variant of the speaker's cone, its "speaker-<name>-<cone>" section laid over it (the guitar's
                           // "paper"), or empty for the speaker as it is
    std::string room;      // the room round the box, its "room-<name>" section (the guitar's row), or empty for none
};

/** THE BOX'S BACK, turned: closed, the cone on the air inside alone; ported, the port tuned as the table tunes it —
    or, on a box the table seals, tuned a little under the driver's resonance in the middle of the baffle's foot
    (portAtFoot over its floor); open, each cone in free air but for the open box's pipe behind it, its back heard
    from the back's plane. */
enum Back { closedBack = 0, portedBack = 1, openBack = 2, halfBack = 3 };
constexpr double portAtFoot = 0.045;

/** A cabinet of the table — its "<name> cap" section, the box as that section says, the speaker `spot` names laid over
    its own and its mic or the one `spot` names over that — with the mic at `spot`, aimed straight at the baffle, its back
    as `spot` turns it. */
inline Cab cabAt (const Table& table, const std::string& cab, const Spot& spot, double rate = fs)
{
    auto s = resolve (table, cab + " cap");
    if (! spot.speaker.empty())
        for (auto& [k, v] : resolve (table, "speaker-" + spot.speaker))
            s[k] = v;
    if (! spot.cone.empty())   // the speaker's cone as its variant has it: the box's own speaker's (speaker_name) or the one laid over
        for (auto& [k, v] : resolve (table, "speaker-" + (spot.speaker.empty() ? str (s, "speaker_name") : spot.speaker) + "-" + spot.cone))
            s[k] = v;
    if (! spot.room.empty())
        for (auto& [k, v] : resolve (table, "room-" + spot.room))
            s[k] = v;
    if (! spot.mic.empty())
        for (auto& [k, v] : resolve (table, "mic-" + spot.mic))
            s[k] = v;
    auto c = build (s);
    c.rate    = rate;
    c.micOff  = spot.x;
    c.micOffY = spot.y;
    c.micD    = spot.distance;
    c.tilt    = spot.angle;
    c.horn    = c.horn && spot.horn;
    if (spot.wall && c.roomW > 0)   // in a room WALL puts the box's back by the room's own wall, which then is the one
        c.roomBack = c.wallM;
    if (! spot.wall || c.roomW > 0)
        c.wallM = 0;
    c.hornDb += spot.hornDb;
    if (spot.back == closedBack || spot.back == openBack || spot.back == halfBack)
    {
        c.vented = false;
        c.open   = spot.back != closedBack;
        c.half   = spot.back == halfBack;
    }
    else if (spot.back == portedBack && ! c.vented)
    {
        c.vented   = true;
        c.open     = false;
        c.portRear = false;
        if (s.count ("equivalent_port_xy") == 0)
            c.port = { 0, -c.bh / 2 + portAtFoot };
        if (s.count ("fb_hz") == 0)
            c.fb = std::clamp (0.85 * c.fsd, 35.0, 55.0);   // a little under the driver's own resonance
    }
    return c;
}

/** WHERE THE PANEL'S LINE REDDENS for a box of the table: its response this far under its neighbours, dB — the table's
    pit_red_db (the guitar's 7: its speakers' own dips run deeper than a bass's, the owner, 07.11 and 08.11), else the bass's 3.
    The panel's own is its catalog's (Panel::Catalog::pitRedDb), the same number. */
inline double pitRedOf (const Table& table, const std::string& cab)
{
    return num (resolve (table, cab + " cap"), "pit_red_db", 3.0);
}

/** WHERE A PAIR'S COMB REDDENS FROM combTopHz UP, dB: the table's comb_top_red_db — the guitar's 6, up there a comb a
    tone of its own, the second mic's phase colour (the owner, 07.11) — else 3, as under it, where a comb thins the low
    notes. The panel's line, FIND and the presets' tools judge a comb by it. */
constexpr double combTopHz = 2000.0;
inline double combTopRedOf (const Table& table, const std::string& cab)
{
    return num (resolve (table, cab + " cap"), "comb_top_red_db", 3.0);
}

/** When the first sound reaches the mic: the nearest patch of the nearest cone, s. */
inline double firstArrival (const Cab& c)
{
    if (c.drivers.empty()) return 0;
    const Xy t = c.drivers[0];
    const double mx = t.x + c.micOff * c.radius, my = t.y + c.micOffY * c.radius;
    double best = 1e9;
    for (const auto& d : c.drivers)
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 16; ++k)
            {
                const double rho = (r + 0.5) / 7, phi = 2 * pi * (k + 0.5 * (r % 2)) / 16;
                const double x = d.x + rho * c.radius * std::cos (phi), y = d.y + rho * c.radius * std::sin (phi);
                const double z = -c.depthFrac * c.radius * (1 - rho);
                best = std::min (best, std::sqrt ((x - mx) * (x - mx) + (y - my) * (y - my) + (z - c.micD) * (z - c.micD)) / c0);
            }
    return best;
}

inline std::vector<float> peakAt (const std::vector<double>& ir, double to)
{
    double peak = 1e-12;
    for (double v : ir) peak = std::max (peak, std::abs (v));
    std::vector<float> out (ir.size());
    for (size_t i = 0; i < ir.size(); ++i) out[i] = (float) (ir[i] * to / peak);
    return out;
}

/** The cabinet with the mic at `spot`: the IR at 48 kHz, its peak at -6 dBFS (a player levels each IR itself). Any
    thread; a tenth of a second or so at the default FFT. */
inline std::vector<float> renderAt (const Table& table, const std::string& cab, const Spot& spot, size_t n = 1 << 14)
{
    return peakAt (render (cabAt (table, cab, spot), n), 0.5);
}

/** TWO MICS on one cabinet, as an engineer puts them up: each at its own spot, both on one clock — the farther heard
    late by its path, so the two comb where they meet — unless `align` slides the later one onto the first; each brought
    to the same level in the middle (500 Hz-2 kHz) before `blend` mixes them, the second turned over by `invertB`. The
    horn and the back are A's. Blend 0 is A alone. */
struct Pair
{
    Spot a, b;
    double blend = 0;
    bool align = false, invertB = false;
    double shiftB = 0;   // seconds B is moved against A, later positive
};

/** `ir` moved later by `seconds` — a fraction of a sample as well as whole ones, through the spectrum, its length kept. */
inline std::vector<double> delayed (const std::vector<double>& ir, double seconds, double rate = fs)
{
    if (std::abs (seconds) < 1e-12 || ir.empty())
        return ir;
    size_t m = 1;
    while (m < 2 * ir.size()) m <<= 1;
    std::vector<cd> x (m, 0.0);
    for (size_t i = 0; i < ir.size(); ++i) x[i] = ir[i];
    fft (x, false);
    for (size_t k = 0; k <= m / 2; ++k)
    {
        x[k] *= std::polar (1.0, -2 * pi * rate * (double) k / (double) m * seconds);
        if (k > 0 && k < m / 2) x[m - k] = std::conj (x[k]);
    }
    x[m / 2] = x[m / 2].real();
    fft (x, true);
    std::vector<double> out (ir.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] = x[i].real();
    return out;
}

/** The pair, and each mic's part of it at the mix's own level — `a` + `b` is `mix` — for a picture of where the two
    meet in phase and where they cancel; a pair with one mic heard (blend 0) has no parts. */
struct PairParts { std::vector<float> mix, a, b; };


/** A PAIR RENDERED AGAIN AND AGAIN, as a hand moves one mic: each mic's own IR kept by what made it — the box, the spot,
    the clock it stands on, the size it was rendered at — so the mic that did not move is not rendered again; and the two
    rendered side by side, on two threads, when both did. One renderer to one thread: it keeps what it last made. */
class PairRenderer
{
public:
    PairParts render (const Table& table, const std::string& cab, const Pair& p, size_t n = 1 << 14, double rate = fs)
    {
        if (p.blend <= 0)
        {
            auto ca = cabAt (table, cab, p.a, rate);
            return { peakAt (irOf (lastA, keyOf (cab, p.a, ca.refDelay, n, rate), ca, n), 0.5), {}, {} };
        }

        auto bSpot = p.b;
        bSpot.horn = p.a.horn;
        bSpot.hornDb = p.a.hornDb;
        bSpot.back = p.a.back;
        bSpot.speaker = p.a.speaker;
        bSpot.wall = p.a.wall;
        bSpot.cone = p.a.cone;
        bSpot.room = p.a.room;
        auto ca = cabAt (table, cab, p.a, rate), cb = cabAt (table, cab, bSpot, rate);
        const double da = firstArrival (ca), db = firstArrival (cb), first = std::min (da, db);
        ca.refDelay = p.align ? da : first;
        cb.refDelay = p.align ? db : first;
        const auto ka = keyOf (cab, p.a, ca.refDelay, n, rate), kb = keyOf (cab, bSpot, cb.refDelay, n, rate);

#if defined(__EMSCRIPTEN__) && ! defined(__EMSCRIPTEN_PTHREADS__)
        const bool together = false;   // a page with no threads renders one after the other
#else
        const bool together = ka != lastA.key && kb != lastB.key;
#endif
        if (together)
        {
            auto bLater = std::async (std::launch::async, [&cb, n] { return cabir::synth::render (cb, n); });
            lastA = { ka, cabir::synth::render (ca, n) };
            lastB = { kb, bLater.get() };
        }
        const auto& ia0 = irOf (lastA, ka, ca, n);
        const auto& ib0 = irOf (lastB, kb, cb, n);
        // B moved in time against A: B later, or — moved earlier — A later instead, so neither's start is cut off.
        const auto ia = p.shiftB < 0 ? delayed (ia0, -p.shiftB, rate) : ia0;
        const auto ib = p.shiftB > 0 ? delayed (ib0, p.shiftB, rate) : ib0;

        const auto middle = [n, rate] (const std::vector<double>& ir) {
            std::vector<cd> x (n, 0.0);
            for (size_t i = 0; i < ir.size() && i < n; ++i) x[i] = ir[i];
            fft (x, false);
            double e = 1e-30;
            for (size_t k = (size_t) (500.0 * (double) n / rate); k <= (size_t) (2000.0 * (double) n / rate); ++k) e += std::norm (x[k]);
            return std::sqrt (e);
        };
        const double ga = (1 - p.blend) / middle (ia), gb = p.blend / middle (ib) * (p.invertB ? -1 : 1);
        const size_t len = std::min (ia.size(), ib.size());
        double peak = 1e-12;
        for (size_t i = 0; i < len; ++i) peak = std::max (peak, std::abs (ga * ia[i] + gb * ib[i]));
        const double to = 0.5 / peak;   // the mix's peak at -6 dBFS, as peakAt puts it
        PairParts out { std::vector<float> (len), std::vector<float> (len), std::vector<float> (len) };
        for (size_t i = 0; i < len; ++i)
        {
            out.a[i] = (float) (ga * ia[i] * to);
            out.b[i] = (float) (gb * ib[i] * to);
            out.mix[i] = (float) ((ga * ia[i] + gb * ib[i]) * to);
        }
        return out;
    }

private:
    struct Kept { std::string key; std::vector<double> ir; };
    Kept lastA, lastB;

    static std::string keyOf (const std::string& cab, const Spot& s, double refDelay, size_t n, double rate)
    {
        char text[256];
        std::snprintf (text, sizeof text, "|%.6f|%.6f|%.6f|%d|%.4f|%.4f|%d|%.12g|%zu|%.0f|", s.x, s.y, s.distance, s.horn ? 1 : 0,
                       s.angle, s.hornDb, s.back, refDelay, n, rate);
        return cab + "|" + s.mic + "|" + s.speaker + (s.wall ? "|wall|" : "||") + s.cone + "|" + s.room + text;
    }

    static const std::vector<double>& irOf (Kept& kept, const std::string& key, const Cab& c, size_t n)
    {
        if (kept.key != key)
            kept = { key, cabir::synth::render (c, n) };
        return kept.ir;
    }
};

inline PairParts renderPairParts (const Table& table, const std::string& cab, const Pair& p, size_t n = 1 << 14, double rate = fs)
{
    return PairRenderer{}.render (table, cab, p, n, rate);
}

inline std::vector<float> renderPair (const Table& table, const std::string& cab, const Pair& p, size_t n = 1 << 14)
{
    return renderPairParts (table, cab, p, n).mix;
}

/** WHAT THE SYNTHESIS RENDERS of a state: its two mics on one clock, the horn and the back A's — or, with one mic on,
    that one alone. */
inline Pair pairOf (const State& s)
{
    const auto spot = [&s] (const cabir::Mic& m)   // the state's mic, not the synthesis's own Mic
    {
        Spot p;
        p.x = m.x;
        p.y = m.y;
        p.distance = m.distance;
        p.horn = s.horn;
        p.mic = m.model;
        p.angle = m.angle;
        p.hornDb = s.hornDb;
        p.back = s.back;
        p.speaker = s.speaker;
        p.wall = s.wall;
        p.cone = s.cone;
        p.room = s.room;
        return p;
    };
    Pair pair { spot (s.a), spot (s.b), s.blend, s.align, s.invert, s.shiftMs / 1000.0 };
    if (! s.b.on)
        pair.blend = 0;            // A alone: the blend not heard
    else if (! s.a.on)
    {
        pair.a = spot (s.b);       // B alone, as one mic is rendered
        pair.blend = 0;
    }
    return pair;
}

/** WHERE B STANDS QUIETEST BESIDE A: 96 places over the whole first speaker — eight across, three up and down, four
    distances, 2 to 12 cm — each heard as the pair hears it (A's horn and back, the same clock unless aligned, the blend,
    B turned over or not), and the one whose meeting with A cancels least from 40 Hz to 12 kHz: at each point the mix
    against the two powers apart, the dips summed, the top at nearly full weight. A place right on A is passed over: two
    of one mic there are one mic. `thorough` (FIND) looks again round the four best, finer — nine places about each, four
    distances about its own — about two seconds in all. Searched on as many threads as the machine has, a render each;
    with `align`, A is rendered once. `tried`, when given, hears of each place as it is judged — its score, 0 for no dip,
    the more negative the more it cancels — from whichever thread judged it: a picture of the search, drawn as it goes.
    Each place is judged at its best shift of B in time, a twentieth of a millisecond at a time within half of one — a
    turn of B's spectrum, no render — and the place carries it. */
struct Place { double x = 0, y = 0, distance = 0.05, shiftMs = 0; };

/** THE SEARCH IN ITS STEPS, for whoever spreads it: the places of the first round (coarse), one place judged (score) —
    nothing a place's judging reads is written by another's, so they go to any thread or worker in any order — the
    places of the second round about the four best of the first (fine), and the one kept (best). quietPlaceForB below
    runs them on this machine's threads; a page runs them on its workers, through the same calls. */
class BSearch
{
public:
    struct Judged { double score = -1e30, shiftMs = 0; };   // -1e30: not judged

    BSearch (const Table& t, const State& s, size_t size = 1 << 13)
        : table (t), state (s), n (size), topSlack (combTopRedOf (t, s.box) - 3.0)
    {
        both = s;
        both.a.on = both.b.on = true;
        pair = pairOf (both);
        ca = cabAt (table, s.box, pair.a);
        da = firstArrival (ca);
        if (both.align)
        {
            ca.refDelay = da;
            iaAligned = render (ca, n);
        }
    }

    /** The first round: eight across, three up and down, four distances — those not right on A. */
    std::vector<Place> coarse() const
    {
        std::vector<Place> out;
        for (const double x : { -0.9, -0.65, -0.4, -0.15, 0.15, 0.4, 0.65, 0.9 })
            for (const double y : { -0.35, 0.0, 0.35 })
                for (const double distance : { 0.02, 0.04, 0.07, 0.12 })
                    if (const Place p { x, y, distance }; ! onA (p))
                        out.push_back (p);
        return out;
    }

    /** The second round, about the four best of the first (`judged` in coarse()'s order): nine places about each, four
        distances about its own — the best's first, so a deadline cuts the least likely. */
    std::vector<Place> fine (const std::vector<Place>& first, const std::vector<Judged>& judged) const
    {
        const auto order = ranked (first, judged);
        std::vector<Place> out;
        for (size_t seed = 0; seed < std::min<size_t> (4, order.size()); ++seed)
        {
            const auto& at = first[order[seed]];
            for (const double dx : { -0.08, 0.0, 0.08 })
                for (const double dy : { -0.08, 0.0, 0.08 })
                    for (const double k : { 0.75, 0.9, 1.1, 1.3 })
                    {
                        const Place p { std::clamp (at.x + dx, -1.05, 1.05), std::clamp (at.y + dy, -1.05, 1.05),
                                        std::clamp (at.distance * k, 0.01, 0.30) };
                        if (std::hypot (p.x, p.y) <= 1.05 && ! onA (p))
                            out.push_back (p);
                    }
        }
        return out;
    }

    /** The place kept: the first round's best, unless the second found one better (its unjudged passed over). */
    Place best (const std::vector<Place>& first, const std::vector<Judged>& firstJudged,
                const std::vector<Place>& second = {}, const std::vector<Judged>& secondJudged = {}) const
    {
        const auto order = ranked (first, firstJudged);
        if (order.empty())
            return { pair.b.x, pair.b.y, pair.b.distance };
        Place kept = first[order.front()];
        kept.shiftMs = firstJudged[order.front()].shiftMs;
        double keptScore = firstJudged[order.front()].score;
        for (size_t i = 0; i < second.size() && i < secondJudged.size(); ++i)
            if (secondJudged[i].score > keptScore + 1e-9)
            {
                kept = second[i];
                kept.shiftMs = secondJudged[i].shiftMs;
                keptScore = secondJudged[i].score;
            }
        return kept;
    }

    /** One place judged, as the pair hears it, at its best shift of B in time. */
    Judged score (const Place& place) const
    {
        auto spot = pair.b;
        spot.x = place.x;
        spot.y = place.y;
        spot.distance = place.distance;
        auto cb = cabAt (table, state.box, spot);
        auto aHere = ca;
        const double db = firstArrival (cb), first = std::min (da, db);
        cb.refDelay = both.align ? db : first;
        aHere.refDelay = both.align ? da : first;
        const auto ia = both.align ? iaAligned : render (aHere, n, 1);
        const auto ib = render (cb, n, 1);   // one thread a render: the places themselves are spread over the threads
        const auto a = spectrumOf (ia, (1 - both.blend) / middle (ia));
        const auto b = spectrumOf (ib, both.blend / middle (ib) * (both.invert ? -1 : 1));
        const double ratio = std::pow (2.0, 1.0 / 24.0), bin = fs / (double) spectrum;
        double apart[72], hzOf[72];
        for (int i = 0; i < 72; ++i)
        {
            hzOf[i] = 40.0 * std::pow (12000.0 / 40.0, i / 71.0);
            apart[i] = power (a, hzOf[i], ratio) + power (b, hzOf[i], ratio);
        }
        double bestSum = -1e30, bestShift = 0;
        for (int step = -10; step <= 10; ++step)
        {
            const double tau = step * 0.05e-3;
            double sum = 0;
            for (int i = 0; i < 72; ++i)
            {
                const auto lo = (size_t) std::max (1.0, std::floor (hzOf[i] / ratio / bin)), hi = (size_t) std::ceil (hzOf[i] * ratio / bin);
                double e = 1e-30;
                for (size_t k = lo; k <= hi; ++k)
                    e += std::norm (a[k] + b[k] * std::polar (1.0, -2 * pi * (double) k * bin * tau));
                // From combTopHz up a comb counts only past what the table lets it cancel there (combTopRedOf).
                const double cancel = 10 * std::log10 (e / apart[i]);
                sum += hzOf[i] <= combTopHz ? std::min (cancel, 0.0) : std::min (cancel + topSlack, 0.0) * 0.8;
            }
            if (sum > bestSum + 1e-9 || (std::abs (sum - bestSum) < 1e-9 && std::abs (tau) < std::abs (bestShift)))
            {
                bestSum = sum;
                bestShift = tau;
            }
        }
        return { bestSum, bestShift * 1000.0 };
    }

private:
    static constexpr size_t spectrum = 1 << 15;   // padded past the IR: a 24th of an octave at 40 Hz still spans bins

    const Table& table;
    State state, both;
    size_t n;
    double topSlack;   // dB a comb may cancel from combTopHz up past the 3 under it: the guitar's 3, the bass's none
    Pair pair;
    Cab ca;
    double da = 0;
    std::vector<double> iaAligned;   // A rendered once, when the pair is aligned

    bool onA (const Place& p) const   // two of one mic there are one mic
    { return std::hypot (p.x - pair.a.x, p.y - pair.a.y) < 0.1 && std::abs (p.distance - pair.a.distance) < 0.01; }

    /** The judged, best first. Places that score the same — often several with no dip at all — nearest where B stands
        first, so B moves no further than it must; then nearest its distance; then in their order. The same on every
        build, whatever its sort. */
    std::vector<size_t> ranked (const std::vector<Place>& places, const std::vector<Judged>& judged) const
    {
        std::vector<size_t> order (std::min (places.size(), judged.size()));
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        const auto across = [this, &places] (size_t i) { return std::hypot (places[i].x - pair.b.x, places[i].y - pair.b.y); };
        const auto deep = [this, &places] (size_t i) { return std::abs (places[i].distance - pair.b.distance); };
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
        {
            if (judged[a].score > judged[b].score) return true;
            if (judged[a].score < judged[b].score) return false;
            const double na = across (a), nb = across (b);
            if (na < nb) return true;
            if (na > nb) return false;
            return deep (a) < deep (b);
        });
        return order;
    }

    static std::vector<cd> spectrumOf (const std::vector<double>& ir, double gain)
    {
        std::vector<cd> x (spectrum, 0.0);
        for (size_t i = 0; i < ir.size() && i < spectrum; ++i) x[i] = gain * ir[i];
        fft (x, false);
        return x;
    }

    double middle (const std::vector<double>& ir) const
    {
        std::vector<cd> x (n, 0.0);
        for (size_t i = 0; i < ir.size() && i < n; ++i) x[i] = ir[i];
        fft (x, false);
        double e = 1e-30;
        for (size_t k = (size_t) (500.0 * (double) n / fs); k <= (size_t) (2000.0 * (double) n / fs); ++k) e += std::norm (x[k]);
        return std::sqrt (e);
    }

    static double power (const std::vector<cd>& x, double hz, double ratio)
    {
        const double bin = fs / (double) spectrum;
        const auto lo = (size_t) std::max (1.0, std::floor (hz / ratio / bin)), hi = (size_t) std::ceil (hz * ratio / bin);
        double e = 0;
        for (size_t k = lo; k <= hi; ++k) e += std::norm (x[k]);
        return e + 1e-30;
    }
};

inline Place quietPlaceForB (const Table& table, const State& s, size_t n = 1 << 13,
                             const std::function<void (const Place&, double score)>& tried = {}, bool thorough = false)
{
    const auto started = std::chrono::steady_clock::now();
    const BSearch search (table, s, n);

    // Every place judged, the first first — or, given a deadline, those reached before it.
    using Clock = std::chrono::steady_clock;
    const auto judgeAll = [&search, &tried] (const std::vector<Place>& all, Clock::time_point deadline)
    {
        std::vector<BSearch::Judged> judged (all.size());
        const auto judge = [&] (size_t i)
        {
            if (Clock::now() >= deadline)
                return;
            judged[i] = search.score (all[i]);
            if (tried)
            {
                auto p = all[i];
                p.shiftMs = judged[i].shiftMs;
                tried (p, judged[i].score);
            }
        };
#if defined(__EMSCRIPTEN__) && ! defined(__EMSCRIPTEN_PTHREADS__)
        for (size_t i = 0; i < all.size(); ++i)
            judge (i);
#else
        const size_t lanes = std::clamp<size_t> (std::thread::hardware_concurrency(), 1, 32);
        std::vector<std::future<void>> others;
        for (size_t lane = 1; lane < lanes; ++lane)
            others.push_back (std::async (std::launch::async, [&all, &judge, lane, lanes] { for (size_t i = lane; i < all.size(); i += lanes) judge (i); }));
        for (size_t i = 0; i < all.size(); i += lanes)
            judge (i);
        for (auto& f : others)
            f.get();
#endif
        return judged;
    };

    const auto first = search.coarse();
    const auto firstJudged = judgeAll (first, Clock::time_point::max());
    if (! thorough)
        return search.best (first, firstJudged);
    const auto second = search.fine (first, firstJudged);
    const auto secondJudged = judgeAll (second, started + std::chrono::seconds (2));   // what is reached in two seconds
    return search.best (first, firstJudged, second, secondJudged);
}

/** The cabinets the table holds, by the names their cap sections give them, in the table's order. */
inline std::vector<std::string> cabinets (const Table& table)
{
    std::vector<std::string> out;
    for (const auto& [name, s] : table)
        if (name.size() > 4 && name.compare (name.size() - 4, 4, " cap") == 0)
            out.push_back (name.substr (0, name.size() - 4));
    return out;
}

/** WHAT A WAV SAYS OF ITSELF beside its sound — after the sound, so a loader that looks for the sound right after the
    header finds it there, and one that knows no more skips the rest: a LIST/INFO chunk with its name (INAM), who made it
    (IART, Darwin's Cat), with what (ISFT) and a comment (ICMT: the link that carries the cabinet), and the cabinet itself
    as its preset's TOML in a chunk of our own, `cbir` — so the app, or OrbitBass's CUSTOM, opens the cabinet a file was
    made of (the owner, 06.11). Empty strings are left out. */
struct WavMeta
{
    std::string name, artist = "Darwin's Cat", software, comment, preset;
};

/** A mono WAV of `x` at `bits` (24, or 16 for a loader that takes no more — the owner, 06.11), as the bytes a file of it
    would hold — and what `meta` says, when given. Rounded, not dithered: an IR's own floor is far under 16 bits'. */
inline std::vector<unsigned char> wavPcm (const std::vector<float>& x, int rate, int bits, const WavMeta* meta = nullptr)
{
    std::vector<unsigned char> b;
    const auto u32 = [&b] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((unsigned char) (v >> (8 * i))); };
    const auto u16 = [&b] (uint16_t v) { b.push_back ((unsigned char) v); b.push_back ((unsigned char) (v >> 8)); };
    const auto tag = [&b] (const char* t) { b.insert (b.end(), t, t + 4); };
    const auto put32 = [&b] (size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + (size_t) i] = (unsigned char) (v >> (8 * i)); };
    const auto pad = [&b] (size_t length) { if (length % 2 != 0) b.push_back (0); };   // a chunk ends on an even byte

    const uint32_t bytes = bits == 16 ? 2 : 3;
    const double full = bits == 16 ? 32768.0 : 8388608.0;
    const uint32_t data = (uint32_t) x.size() * bytes;
    tag ("RIFF"); u32 (0); tag ("WAVE"); tag ("fmt "); u32 (16); u16 (1); u16 (1); u32 ((uint32_t) rate);
    u32 ((uint32_t) rate * bytes); u16 ((uint16_t) bytes); u16 ((uint16_t) (8 * bytes)); tag ("data"); u32 (data);
    for (float v : x)
    {
        const auto s = (int32_t) std::lround (std::clamp ((double) v, -1.0, 1.0 - 1.0 / full) * full);
        for (uint32_t k = 0; k < bytes; ++k)
            b.push_back ((unsigned char) (s >> (8 * k)));
    }
    pad (data);

    if (meta != nullptr)
    {
        // A text chunk: its words and a nought after them, the chunk's length counting both.
        const auto text = [&] (const char* id, const std::string& words)
        {
            if (words.empty())
                return;
            tag (id);
            u32 ((uint32_t) words.size() + 1);
            b.insert (b.end(), words.begin(), words.end());
            b.push_back (0);
            pad (words.size() + 1);
        };
        const size_t list = b.size();
        tag ("LIST"); u32 (0); tag ("INFO");
        text ("INAM", meta->name);
        text ("IART", meta->artist);
        text ("ISFT", meta->software);
        text ("ICMT", meta->comment);
        if (b.size() - list == 12)
            b.resize (list);            // nothing said: no LIST at all
        else
            put32 (list + 4, (uint32_t) (b.size() - list - 8));
        text ("cbir", meta->preset);
    }
    put32 (4, (uint32_t) (b.size() - 8));
    return b;
}

/** ...24-bit, as the app's DOWNLOAD IR and the packs write it. */
inline std::vector<unsigned char> wav24 (const std::vector<float>& x, int rate, const WavMeta* meta = nullptr)
{
    return wavPcm (x, rate, 24, meta);
}

/** An IR cut to `samples` (0: as it is) for a loader that takes no longer one (the owner, 06.11) — its last quarter faded
    out along a raised cosine, so the cut is no click. */
inline std::vector<float> shortened (std::vector<float> x, size_t samples)
{
    if (samples == 0 || samples >= x.size())
        return x;
    x.resize (samples);
    const size_t fade = std::max<size_t> (1, samples / 4);
    for (size_t i = 0; i < fade; ++i)
        x[samples - fade + i] *= (float) (0.5 + 0.5 * std::cos (pi * (double) (i + 1) / (double) fade));
    return x;
}

} // namespace cabir::synth
