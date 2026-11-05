// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE GENERATOR FOR A PAGE: the synthesis behind a C ABI, built to WebAssembly (one module to a worker). Text in — the
// cabinet table, a state as TOML — numbers out, in buffers the module owns until the next call of the same kind:
//
//   cabir_table (text)                 the cabinets, from cabinets.txt's text; the number of boxes, 0 for none
//   cabir_render (toml, live)          a state rendered: the IR's length, 0 for a state that does not read. live renders
//                                      at half the size, as the app does while a hand is on a control
//     cabir_mix / cabir_a / cabir_b    the IR, 48 kHz mono float, and each mic's part of it (empty with one mic on)
//     cabir_response / cabir_comb      the band's curves, cabir_points() of them (the comb empty with one mic on)
//     cabir_gain (rate)                the level the app plays it at: felitronics-core's CabConvolver, reference-unity
//     cabir_wav / cabir_wav_size       the IR as the app downloads it: a 24-bit WAV
//   cabir_find (toml, thorough)        B's quiet place against A: x, y, distance, shift (ms) at cabir_found()
//     cabir_tried / cabir_tried_count  the places tried on the way, x, y, score each, for the search's map
//
// FIND SPREAD OVER WORKERS — the same search, its steps called apart (synth::BSearch); the page only hands places out:
//   cabir_find_coarse (toml)           the first round's places: their count, x, y, distance each at cabir_find_places()
//   cabir_find_score (toml, x, y, d)   one place judged, in any worker: its score, its shift (ms) at cabir_find_shift()
//   cabir_find_fine (judged, count)    the first round's scores and shifts (double pairs, in its order) in; the second
//                                      round's places out, as coarse gives them — best first; a page stops at 2 s
//   cabir_find_best (judged, count)    the second round's pairs in (-1e30 for one not reached; none without thorough):
//                                      the place at cabir_found()
//
// Floats are read from the module's heap (HEAPF32 at the pointer / 4), lengths from the calls that say them.

#include <felitronics/convolution/CabConvolver.h>
#include <synth/CabSynth.h>
#include <synth/Response.h>
#include <synth/State.h>

#include <memory>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
 #include <emscripten/emscripten.h>
 #define CABIR_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
 #define CABIR_EXPORT extern "C"
#endif

namespace
{
    using namespace cabir;

    struct Session
    {
        synth::Table table;
        synth::PairRenderer renderer;   // keeps the mic that did not move, as the app's does
        synth::PairParts ir;
        std::vector<float> response, comb;
        std::vector<unsigned char> wav;
        float found[4] {};
        std::vector<float> tried;

        std::string findToml;                       // the state the search below is of
        std::unique_ptr<synth::BSearch> search;
        std::vector<synth::Place> first, second;
        std::vector<synth::BSearch::Judged> firstJudged;
        std::vector<float> places;
        double shift = 0;
    };

    Session& session()
    {
        static Session s;
        return s;
    }

    const float* dataOf (const std::vector<float>& v) { return v.empty() ? nullptr : v.data(); }

    /** The search of this state, made once and kept while the state is the same. */
    synth::BSearch* searchOf (const char* toml)
    {
        auto& s = session();
        const std::string text = toml != nullptr ? toml : "";
        if (s.search == nullptr || s.findToml != text)
        {
            const auto state = fromToml (text);
            if (! state || s.table.empty())
                return nullptr;
            s.search = std::make_unique<synth::BSearch> (s.table, *state);
            s.findToml = text;
        }
        return s.search.get();
    }

    std::vector<synth::BSearch::Judged> judgedOf (const double* pairs, int count)
    {
        std::vector<synth::BSearch::Judged> out ((size_t) std::max (0, count));
        for (size_t i = 0; i < out.size() && pairs != nullptr; ++i)
            out[i] = { pairs[2 * i], pairs[2 * i + 1] };
        return out;
    }

    void put (std::vector<float>& to, const std::vector<synth::Place>& places)
    {
        to.clear();
        for (const auto& p : places)
            to.insert (to.end(), { (float) p.x, (float) p.y, (float) p.distance });
    }
}

CABIR_EXPORT int cabir_table (const char* text)
{
    auto& s = session();
    s.table = synth::parseTable (text != nullptr ? std::string (text) : std::string());
    s.renderer = {};
    return (int) synth::cabinets (s.table).size();
}

CABIR_EXPORT int cabir_render (const char* toml, int live)
{
    auto& s = session();
    const auto state = fromToml (toml != nullptr ? toml : "");
    if (! state || s.table.empty())
        return 0;
    // The sizes the app renders at: half while a hand moves a control, full once it lets go.
    s.ir = s.renderer.render (s.table, state->box, synth::pairOf (*state), live != 0 ? (size_t) 1 << 13 : (size_t) 1 << 14);
    s.response = synth::responseOf (synth::as24Bit (s.ir.mix), 48000.0);
    s.comb = s.ir.a.empty() ? std::vector<float>() : synth::combOf (s.ir.a, s.ir.b);
    s.wav = synth::wav24 (s.ir.mix, 48000);
    return (int) s.ir.mix.size();
}

CABIR_EXPORT const float* cabir_mix()    { return dataOf (session().ir.mix); }
CABIR_EXPORT const float* cabir_a()      { return dataOf (session().ir.a); }
CABIR_EXPORT const float* cabir_b()      { return dataOf (session().ir.b); }
CABIR_EXPORT int cabir_parts_length()    { return (int) session().ir.a.size(); }

CABIR_EXPORT int cabir_points()                 { return synth::responsePoints; }
CABIR_EXPORT float cabir_point_hz (int i)       { return synth::responseHz (i); }
CABIR_EXPORT const float* cabir_response()      { return dataOf (session().response); }
CABIR_EXPORT const float* cabir_comb()          { return dataOf (session().comb); }

CABIR_EXPORT float cabir_gain (double rate)
{
    // The convolver the app plays through, asked what it does to this IR at `rate`: resampled there if it must be, then
    // levelled to reference-unity. A page that plays at 48 kHz plays the IR as it is, times this.
    const auto& mix = session().ir.mix;
    if (mix.empty())
        return 1.0f;
    auto convolver = std::make_unique<felitronics::convolution::CabConvolver>();
    if (! convolver->prepare (rate, 128, 1, 1.0, true))
        return 1.0f;
    const float* planes[] { mix.data() };
    convolver->loadIR (planes, 1, (int) mix.size(), 48000.0);
    return convolver->irNormalizationGain();
}

CABIR_EXPORT const unsigned char* cabir_wav() { return session().wav.empty() ? nullptr : session().wav.data(); }
CABIR_EXPORT int cabir_wav_size()             { return (int) session().wav.size(); }

CABIR_EXPORT int cabir_find (const char* toml, int thorough)
{
    auto& s = session();
    const auto state = fromToml (toml != nullptr ? toml : "");
    if (! state || s.table.empty())
        return 0;
    s.tried.clear();
    const auto place = synth::quietPlaceForB (s.table, *state, (size_t) 1 << 13,
                                              [&s] (const synth::Place& p, double score)
                                              {
                                                  s.tried.insert (s.tried.end(), { (float) p.x, (float) p.y, (float) score });
                                              },
                                              thorough != 0);
    s.found[0] = (float) place.x;
    s.found[1] = (float) place.y;
    s.found[2] = (float) place.distance;
    s.found[3] = (float) place.shiftMs;
    return 1;
}

CABIR_EXPORT const float* cabir_found()      { return session().found; }
CABIR_EXPORT const float* cabir_tried()      { return dataOf (session().tried); }
CABIR_EXPORT int cabir_tried_count()         { return (int) session().tried.size() / 3; }

CABIR_EXPORT int cabir_find_coarse (const char* toml)
{
    auto& s = session();
    auto* search = searchOf (toml);
    if (search == nullptr)
        return 0;
    s.first = search->coarse();
    s.second.clear();
    put (s.places, s.first);
    return (int) s.first.size();
}

CABIR_EXPORT const float* cabir_find_places() { return dataOf (session().places); }

CABIR_EXPORT double cabir_find_score (const char* toml, double x, double y, double distance)
{
    auto* search = searchOf (toml);
    if (search == nullptr)
        return -1e30;
    const auto judged = search->score ({ x, y, distance });
    session().shift = judged.shiftMs;
    return judged.score;
}

CABIR_EXPORT double cabir_find_shift() { return session().shift; }

CABIR_EXPORT int cabir_find_fine (const double* judged, int count)
{
    auto& s = session();
    if (s.search == nullptr || (size_t) count != s.first.size())
        return 0;
    s.firstJudged = judgedOf (judged, count);
    s.second = s.search->fine (s.first, s.firstJudged);
    put (s.places, s.second);
    return (int) s.second.size();
}

CABIR_EXPORT int cabir_find_best (const double* judged, int count)
{
    auto& s = session();
    if (s.search == nullptr || s.firstJudged.size() != s.first.size())
        return 0;
    const auto place = s.search->best (s.first, s.firstJudged, s.second, judgedOf (judged, count));
    s.found[0] = (float) place.x;
    s.found[1] = (float) place.y;
    s.found[2] = (float) place.distance;
    s.found[3] = (float) place.shiftMs;
    return 1;
}
