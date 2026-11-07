// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// PRESETCHECK <table.txt> <preset.toml> ... [--suggest] — each preset read as the app reads it, rendered as the app renders
// it, and said in one line: its box and mics known or not, how much its two mics cancel where they meet (the worst point,
// 40 Hz-2 kHz and 2-12 kHz, in dB: 0 none, -3 a dip you hear), and its response's shape against its own 500 Hz-2 kHz middle
// at 60, 100, 250, 1k, 3k and 6k Hz. --suggest adds where FIND would put B. --tune-shift moves B in time where that combs
// less — B's place kept, -0.5 to +0.5 ms by 0.02 — and writes it into the file, only where a worst point gets at least
// 0.3 dB better and neither gets worse. No JUCE.

#include "../src/synth/CabSynth.h"
#include "../src/synth/State.h"

#include <felitronics/toml/Toml.h>

#include <cstring>
#include <fstream>
#include <sstream>

using namespace cabir;

namespace
{
constexpr size_t N = 1 << 16;

std::vector<synth::cd> spectrumOf (const std::vector<float>& x)
{
    std::vector<synth::cd> s (N, 0.0);
    for (size_t i = 0; i < x.size() && i < N; ++i) s[i] = x[i];
    synth::fft (s, false);
    return s;
}

double bandPower (const std::vector<synth::cd>& s, double hz, double ratio, const std::vector<synth::cd>* plus = nullptr)
{
    const double bin = synth::fs / (double) N;
    double e = 1e-30;
    for (auto k = (size_t) std::floor (hz / ratio / bin); k <= (size_t) std::ceil (hz * ratio / bin); ++k)
        e += std::norm (plus != nullptr ? s[k] + (*plus)[k] : s[k]);
    return e;
}

double worstComb (const std::vector<synth::cd>& a, const std::vector<synth::cd>& b, double f0, double f1)
{
    double worst = 0;
    const double ratio = std::pow (2.0, 1.0 / 24.0);
    for (int i = 0; i < 64; ++i)
    {
        const double hz = f0 * std::pow (f1 / f0, i / 63.0);
        worst = std::min (worst, 10 * std::log10 (bandPower (a, hz, ratio, &b) / (bandPower (a, hz, ratio) + bandPower (b, hz, ratio))));
    }
    return worst;
}

bool has (const synth::Table& table, const std::string& section)
{
    return std::any_of (table.begin(), table.end(), [&section] (const auto& s) { return s.first == section; });
}

std::string field (const std::string& text, const char* key)
{
    namespace t = felitronics::toml;
    auto parsed = t::parse (text);
    if (const auto* root = std::get_if<t::Table> (&parsed))
        if (const auto* v = root->find (key))
            if (const auto* s = std::get_if<std::string> (&v->data))
                return *s;
    return {};
}
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "presetcheck <table.txt> <preset.toml> ... [--suggest]\n"); return 2; }
    const auto table = synth::readTable (argv[1]);
    bool suggest = false, tune = false;
    for (int i = 2; i < argc; ++i)
    {
        suggest = suggest || std::strcmp (argv[i], "--suggest") == 0;
        tune = tune || std::strcmp (argv[i], "--tune-shift") == 0;
    }

    int bad = 0;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--suggest") == 0 || std::strcmp (argv[i], "--tune-shift") == 0) continue;
        std::ifstream in (argv[i]);
        std::stringstream ss;
        ss << in.rdbuf();
        const auto text = ss.str();
        const auto state = fromToml (text);
        const auto name = field (text, "name"), style = field (text, "style");
        if (! state) { std::printf ("BAD  %s: not a state (cabir = 1, box, values in range)\n", argv[i]); ++bad; continue; }
        std::string unknown;
        if (! has (table, state->box + " cap")) unknown += " box:" + state->box;
        for (const auto* m : { &state->a, &state->b })
            if (m->on && ! m->model.empty() && ! has (table, "mic-" + m->model)) unknown += " mic:" + m->model;
        if (! unknown.empty() || name.empty() || style.empty())
        {
            std::printf ("BAD  %s:%s%s%s\n", argv[i], unknown.c_str(), name.empty() ? " no name" : "", style.empty() ? " no style" : "");
            ++bad;
            continue;
        }

        // --tune-shift: B's time where its worst points comb least, B's place kept; written only where it pays.
        if (tune && state->a.on && state->b.on)
        {
            synth::PairRenderer renderer;
            const auto worstOf = [&] (double shiftMs)
            {
                auto s = *state;
                s.shiftMs = shiftMs;
                const auto pp = renderer.render (table, s.box, synth::pairOf (s));
                const auto a = spectrumOf (pp.a), b = spectrumOf (pp.b);
                return std::pair<double, double> { worstComb (a, b, 40, 2000), worstComb (a, b, 2000, 12000) };
            };
            const auto [lo0, hi0] = worstOf (state->shiftMs);
            double bestShift = state->shiftMs, bestLo = lo0, bestHi = hi0;
            for (int k = -25; k <= 25; ++k)
            {
                const double shift = k * 0.02;
                const auto [lo, hi] = worstOf (shift);
                const bool noWorse = lo >= lo0 - 0.05 && hi >= hi0 - 0.05, pays = lo - lo0 >= 0.3 || hi - hi0 >= 0.3;
                if (noWorse && pays && lo + hi > bestLo + bestHi + 1e-6)
                {
                    bestShift = shift;
                    bestLo = lo;
                    bestHi = hi;
                }
            }
            if (std::abs (bestShift - state->shiftMs) > 1e-9)
            {
                auto tuned = *state;
                tuned.shiftMs = bestShift;
                namespace t = felitronics::toml;
                t::Table head;
                (void) head.insert ("name", t::Value (name));
                (void) head.insert ("style", t::Value (style));
                std::ofstream (argv[i]) << t::write (head) + toToml (tuned);
                std::printf ("TUNED %-40s shift %+.2f ms: comb %5.1f / %5.1f -> %5.1f / %5.1f\n", name.c_str(), bestShift, lo0, hi0, bestLo, bestHi);
            }
            continue;
        }

        const auto parts = synth::renderPairParts (table, state->box, synth::pairOf (*state));
        const auto mix = spectrumOf (parts.mix);
        double middle = 0;
        for (double hz = 500; hz <= 2000; hz *= std::pow (2.0, 1.0 / 6.0)) middle += 10 * std::log10 (bandPower (mix, hz, std::pow (2.0, 1.0 / 12.0)));
        middle /= 13.0;
        std::printf ("%-8s %-40s %-11s", style.c_str(), name.c_str(), state->box.c_str());
        if (! parts.a.empty())
        {
            const auto a = spectrumOf (parts.a), b = spectrumOf (parts.b);
            std::printf (" comb %5.1f / %5.1f |", worstComb (a, b, 40, 2000), worstComb (a, b, 2000, 12000));
        }
        else
            std::printf (" one mic        |");
        for (double hz : { 60.0, 100.0, 250.0, 1000.0, 3000.0, 6000.0 })
            std::printf (" %5.1f", 10 * std::log10 (bandPower (mix, hz, std::pow (2.0, 1.0 / 12.0))) - middle);
        if (suggest && state->b.on)
        {
            const auto p = synth::quietPlaceForB (table, *state, (size_t) 1 << 13, {}, true);
            std::printf (" | FIND: x %.2f y %.2f %.0f cm", p.x, p.y, p.distance * 100);
        }
        std::printf ("\n");
    }
    return bad == 0 ? 0 : 1;
}
