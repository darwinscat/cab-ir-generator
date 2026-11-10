// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE GENERATOR FOR A PAGE: the synthesis behind a C ABI, built to WebAssembly (one module to a worker). Text in — the
// cabinet table, a state as TOML — numbers out, in buffers the module owns until the next call of the same kind:
//
//   cabir_catalog (name)               whose panel and cabinets: "bass" (the first) or "guitar" — its boxes, mics,
//                                      speakers and rooms, and what its WAVs say made them; 1 for a name it knows, else
//                                      0 and the catalog kept. Before the table, and in every worker too
//   cabir_table (text)                 the cabinets, from cabinets.txt's text (the guitar's, data/guitar/cabinets.txt,
//                                      under its catalog); the number of boxes, 0 for none
//   cabir_render (toml, live)          a state rendered: the IR's length, 0 for a state that does not read or names a
//                                      box the table lacks. live renders
//                                      at half the size, as the app does while a hand is on a control
//     cabir_mix / cabir_a / cabir_b    the IR, 48 kHz mono float, and each mic's part of it (empty with one mic on)
//     cabir_response / cabir_comb      the band's curves, cabir_points() of them (the comb empty with one mic on)
//     cabir_gain (rate)                the level the app plays it at: felitronics-core's CabConvolver, reference-unity,
//                                      times the EQ's change of loudness
//     cabir_wav / cabir_wav_size       the IR as the app downloads it: a 24-bit WAV
//   cabir_render_wav (toml, rate, bits, samples)   DOWNLOAD IR in a format (panel/Download.h): the WAV at cabir_wav() —
//                                      44100|48000|96000, 16|24, 0 the whole IR|512|1024|2048 — its size, 0 for none
//   cabir_download_name (toml, rate, bits, samples)   ...its file's name, no .wav: "8x10 MD421+RE20 44k 16bit 1024"
//   cabir_download_note (rate, bits, samples, toml)   ...the line under the button: "44.1 kHz, 16 bit, 1024 samples";
//                                      the whole IR as long as the state's (a room makes it longer) — toml null, 100 ms
//   cabir_whole_ms (toml)              how long the state's whole IR is, ms — the format menu's FULL — 100 without a room,
//                                      a guitar ROOM's 150, 250 or 400; 0 for a state that does not read
//   cabir_find (toml, thorough)        B's quiet place against A: x, y, distance, shift (ms) at cabir_found() — or, with
//                                      one mic heard, that one's place without a pit about where it is (shift 0)
//     cabir_tried / cabir_tried_count  the places tried on the way, x, y, score each, for the search's map
//
// FIND SPREAD OVER WORKERS — the same search, its steps called apart (synth::BSearch); the page only hands places out:
//   cabir_find_coarse (toml)           the first round's places: their count, x, y, distance each at cabir_find_places()
//   cabir_find_score (toml, x, y, d)   one place judged, in any worker: its score, its shift (ms) at cabir_find_shift()
//   cabir_find_fine (judged, count)    the first round's scores and shifts (double pairs, in its order) in; the second
//                                      round's places out, as coarse gives them — best first; a page stops at 2 s;
//                                      none with one mic heard, whose search is one round (synth::MicSearch)
//   cabir_with_b (toml, x, y, d, shift)   the state with B put where FIND found: its TOML (until the next call)
//   cabir_with_found (toml, x, y, d, shift)   ...B with both mics heard, the one mic with one: what FIND found, put
//   cabir_state_from_link (link)         the cabinet a link carries (panel/Link.h: what follows the page's `#`, a query, or
//                                        a whole address) as its canonical TOML; null where it names no cabinet
//   cabir_link_of (toml)                 ...and the part after `#` that carries a state (the page adds its address)
//   cabir_b_on (toml)                    1 while the state hears B — B just taken up is when the app searches on its own
//   cabir_find_best (judged, count)    the second round's pairs in (-1e30 for one not reached; none without thorough):
//                                      the place at cabir_found()
//
// Floats are read from the module's heap (HEAPF32 at the pointer / 4), lengths from the calls that say them.

#include <felitronics/convolution/CabConvolver.h>
#include <synth/CabSynth.h>
#include <panel/Download.h>
#include <panel/GuitarCatalog.h>
#include <panel/Link.h>
#include <synth/MicSearch.h>
#include <synth/Response.h>
#include <synth/State.h>

#include <algorithm>
#include <memory>
#include <optional>
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
        std::unique_ptr<synth::BSearch> search;      // both mics heard
        std::unique_ptr<synth::MicSearch> micSearch; // ...or one
        std::vector<synth::Place> first, second;
        std::vector<synth::BSearch::Judged> firstJudged;
        std::vector<float> places;
        double shift = 0;
        std::string withB;
        std::string text;   // a download's name or its format's line, until the next call
        bool guitar = false;   // the catalog cabir_catalog chose
    };

    Session& session()
    {
        static Session s;
        return s;
    }

    const float* dataOf (const std::vector<float>& v) { return v.empty() ? nullptr : v.data(); }

    /** What a WAV of `s` says of itself, as the app's DOWNLOAD IR writes it: its line, the link that opens it, its TOML. */
    synth::WavMeta metaOf (const State& s)
    {
        synth::WavMeta meta;
        meta.name = Panel::labelOf (s).toStdString();
        const bool guitar = session().guitar;
        meta.software = std::string (guitar ? "Orbit Guitar Cab Builder " : "Orbit Bass Cab Builder ") + CABIR_VERSION;
        meta.comment = std::string (guitar ? guitarPageAddress : pageAddress) + "#" + linkOf (s);
        meta.preset = "name = \"" + meta.name + "\"\n\n" + toToml (s);
        return meta;
    }

    /** A state that reads, of a box the table has — one naming a box it lacks is not a state to render. */
    std::optional<State> stateOf (const char* toml)
    {
        auto state = knownState (toml != nullptr ? toml : "");
        if (! state)
            return std::nullopt;
        const auto boxes = synth::cabinets (session().table);
        if (std::find (boxes.begin(), boxes.end(), state->box) == boxes.end())
            return std::nullopt;
        return state;
    }

    /** THE SEARCH OF THIS STATE, made once and kept while the state is the same: B's with both mics heard, the one
        mic's with one — the same steps either way. */
    struct Search
    {
        const synth::BSearch* pair = nullptr;
        const synth::MicSearch* one = nullptr;

        std::vector<synth::Place> coarse() const { return pair != nullptr ? pair->coarse() : one->coarse(); }
        synth::BSearch::Judged score (const synth::Place& p) const { return pair != nullptr ? pair->score (p) : one->score (p); }
        std::vector<synth::Place> fine (const std::vector<synth::Place>& first, const std::vector<synth::BSearch::Judged>& judged) const
        { return pair != nullptr ? pair->fine (first, judged) : one->fine (first, judged); }
        synth::Place best (const std::vector<synth::Place>& first, const std::vector<synth::BSearch::Judged>& firstJudged,
                           const std::vector<synth::Place>& second, const std::vector<synth::BSearch::Judged>& secondJudged) const
        { return pair != nullptr ? pair->best (first, firstJudged, second, secondJudged) : one->best (first, firstJudged); }
    };

    std::optional<Search> searchOf (const char* toml)
    {
        auto& s = session();
        const std::string text = toml != nullptr ? toml : "";
        if ((s.search == nullptr && s.micSearch == nullptr) || s.findToml != text)
        {
            const auto state = stateOf (toml);
            if (! state)
                return std::nullopt;
            s.search.reset();
            s.micSearch.reset();
            if (state->a.on && state->b.on)
                s.search = std::make_unique<synth::BSearch> (s.table, *state);
            else
                s.micSearch = std::make_unique<synth::MicSearch> (s.table, *state);
            s.findToml = text;
        }
        return Search { s.search.get(), s.micSearch.get() };
    }

    /** The search last made, as it stands — none before the first. */
    std::optional<Search> lastSearch()
    {
        const auto& s = session();
        if (s.search == nullptr && s.micSearch == nullptr)
            return std::nullopt;
        return Search { s.search.get(), s.micSearch.get() };
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

CABIR_EXPORT int cabir_catalog (const char* name)
{
    const std::string n = name != nullptr ? name : "";
    if (n != "bass" && n != "guitar")
        return 0;
    auto& s = session();
    s.guitar = n == "guitar";
    Panel::useCatalog (s.guitar ? guitarCatalog() : Panel::bassCatalog());
    s.renderer = {};
    return 1;
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
    const auto state = stateOf (toml);
    if (! state)
        return 0;
    // The sizes the app renders at: half while a hand moves a control, full once it lets go.
    s.ir = s.renderer.render (s.table, state->box, synth::pairOf (*state), live != 0 ? (size_t) 1 << 13 : (size_t) 1 << 14);
    s.response = synth::responseOf (synth::as24Bit (s.ir.mix), 48000.0, state->eq);
    s.comb = s.ir.a.empty() ? std::vector<float>() : synth::combOf (s.ir.a, s.ir.b);
    const auto meta = metaOf (*state);
    s.wav = synth::wav24 (s.ir.mix, 48000, &meta);
    return (int) s.ir.mix.size();
}

CABIR_EXPORT int cabir_render_wav (const char* toml, int rate, int bits, int samples)
{
    auto& s = session();
    const auto state = stateOf (toml);
    if (! state || ! DownloadFormat::offered (rate, bits, samples))
        return 0;
    s.wav = downloadWav (s.table, *state, { rate, bits, samples }, metaOf (*state));
    return (int) s.wav.size();
}

CABIR_EXPORT const char* cabir_download_name (const char* toml, int rate, int bits, int samples)
{
    auto& s = session();
    const auto state = stateOf (toml);
    if (! state || ! DownloadFormat::offered (rate, bits, samples))
        return nullptr;
    s.text = downloadName (*state, { rate, bits, samples });
    return s.text.c_str();
}

CABIR_EXPORT const char* cabir_download_note (int rate, int bits, int samples, const char* toml)
{
    auto& s = session();
    if (! DownloadFormat::offered (rate, bits, samples))
        return nullptr;
    const auto state = toml != nullptr ? stateOf (toml) : std::nullopt;
    s.text = state ? formatNote ({ rate, bits, samples }, wholeMs (s.table, *state)) : formatNote ({ rate, bits, samples });
    return s.text.c_str();
}

CABIR_EXPORT int cabir_whole_ms (const char* toml)
{
    const auto state = stateOf (toml);
    return state ? wholeMs (session().table, *state) : 0;
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
    // levelled to reference-unity; times what the EQ did to its loudness, which that levelling takes back and the app puts
    // in again (PairParts::levelMix). A page that plays at 48 kHz plays the IR as it is, times this.
    const auto& ir = session().ir;
    if (ir.mix.empty())
        return 1.0f;
    auto convolver = std::make_unique<felitronics::convolution::CabConvolver>();
    if (! convolver->prepare (rate, 128, 1, 1.0, true))
        return ir.levelMix;
    const float* planes[] { ir.mix.data() };
    convolver->loadIR (planes, 1, (int) ir.mix.size(), 48000.0);
    return convolver->irNormalizationGain() * ir.levelMix;
}

CABIR_EXPORT const unsigned char* cabir_wav() { return session().wav.empty() ? nullptr : session().wav.data(); }
CABIR_EXPORT int cabir_wav_size()             { return (int) session().wav.size(); }

CABIR_EXPORT int cabir_find (const char* toml, int thorough)
{
    auto& s = session();
    const auto state = stateOf (toml);
    if (! state)
        return 0;
    s.tried.clear();
    const auto tried = [&s] (const synth::Place& p, double score) { s.tried.insert (s.tried.end(), { (float) p.x, (float) p.y, (float) score }); };
    const auto place = state->a.on && state->b.on ? synth::quietPlaceForB (s.table, *state, (size_t) 1 << 13, tried, thorough != 0)
                                                  : synth::smoothPlaceForMic (s.table, *state, (size_t) 1 << 14, tried);
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
    const auto search = searchOf (toml);
    if (! search)
        return 0;
    s.first = search->coarse();
    s.second.clear();
    put (s.places, s.first);
    return (int) s.first.size();
}

CABIR_EXPORT const float* cabir_find_places() { return dataOf (session().places); }

CABIR_EXPORT double cabir_find_score (const char* toml, double x, double y, double distance)
{
    const auto search = searchOf (toml);
    if (! search)
        return -1e30;
    const auto judged = search->score ({ x, y, distance });
    session().shift = judged.shiftMs;
    return judged.score;
}

CABIR_EXPORT double cabir_find_shift() { return session().shift; }

CABIR_EXPORT int cabir_find_fine (const double* judged, int count)
{
    auto& s = session();
    const auto search = lastSearch();
    if (! search || (size_t) count != s.first.size())
        return 0;
    s.firstJudged = judgedOf (judged, count);
    s.second = search->fine (s.first, s.firstJudged);
    put (s.places, s.second);
    return (int) s.second.size();
}

CABIR_EXPORT int cabir_find_best (const double* judged, int count)
{
    auto& s = session();
    const auto search = lastSearch();
    if (! search || s.firstJudged.size() != s.first.size())
        return 0;
    const auto place = search->best (s.first, s.firstJudged, s.second, judgedOf (judged, count));
    s.found[0] = (float) place.x;
    s.found[1] = (float) place.y;
    s.found[2] = (float) place.distance;
    s.found[3] = (float) place.shiftMs;
    return 1;
}

CABIR_EXPORT const char* cabir_with_b (const char* toml, double x, double y, double distance, double shiftMs)
{
    auto& s = session();
    auto state = knownState (toml != nullptr ? toml : "");
    if (! state)
        return nullptr;
    state->b.x = x;
    state->b.y = y;
    state->b.distance = distance;
    state->shiftMs = shiftMs;
    s.withB = toToml (*state);
    return s.withB.c_str();
}

CABIR_EXPORT const char* cabir_with_found (const char* toml, double x, double y, double distance, double shiftMs)
{
    auto& s = session();
    auto state = knownState (toml != nullptr ? toml : "");
    if (! state)
        return nullptr;
    const bool pair = state->a.on && state->b.on;
    auto& m = pair || ! state->a.on ? state->b : state->a;
    m.x = x;
    m.y = y;
    m.distance = distance;
    if (pair)
        state->shiftMs = shiftMs;
    s.withB = toToml (*state);
    return s.withB.c_str();
}

CABIR_EXPORT const char* cabir_state_from_link (const char* link)
{
    // The cabinet a link carries (panel/Link.h), as its canonical TOML — or null where it names no cabinet.
    auto& s = session();
    const auto state = fromLink (link != nullptr ? link : "");
    if (! state)
        return nullptr;
    s.withB = toToml (*state);
    return s.withB.empty() ? nullptr : s.withB.c_str();   // never an empty text as a cabinet
}

CABIR_EXPORT const char* cabir_link_of (const char* toml)
{
    // ...and the part after `#` that carries a state: the page and a WAV put the page's address before it.
    auto& s = session();
    const auto state = knownState (toml != nullptr ? toml : "");
    if (! state)
        return nullptr;
    s.withB = linkOf (*state);
    return s.withB.c_str();
}

CABIR_EXPORT int cabir_b_on (const char* toml)
{
    const auto state = knownState (toml != nullptr ? toml : "");
    return state && state->b.on ? 1 : 0;
}
