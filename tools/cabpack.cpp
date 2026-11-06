// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE IR PACKS a page offers for download, made again whenever the presets change — the same inputs, the same bytes.
//
//   cabpack <cabinets.txt> <presets folder> <typical.toml> <out folder>
//
// A folder for each preset style (rock, metal, ...) and one for the typical cabinets (typical.toml: a box, its back, its
// horn; A as DEFAULT's, B a U47 where FIND finds it quietest, every place of both rounds judged — no deadline, so a run
// again finds the same). In each, 44.1k/, 48k/ and 96k/, every cabinet synthesised at that rate: a 24-bit mono WAV as
// the app's DOWNLOAD IR writes it, and beside it its state as the app's preset — and index.tsv, a line for each, its last
// column the link that opens it on the page; and responses.svg, each one's response as the panel draws it, a click on one
// opening it there too.

#include <panel/Link.h>
#include <panel/Panel.h>
#include <synth/CabSynth.h>
#include <synth/Response.h>
#include <synth/State.h>

#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <sstream>
#include <thread>

namespace
{
    using namespace cabir;
    namespace fsys = std::filesystem;
    namespace t = felitronics::toml;

    struct Item { std::string file, style, name, text; State state; };

    std::string slurp (const fsys::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    std::string field (const t::Table& root, const char* key)
    {
        if (const auto* v = root.find (key))
            if (const auto* s = std::get_if<std::string> (&v->data))
                return *s;
        return {};
    }

    std::string lower (std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        return s;
    }

    /** A box as the list names it: the panel's size word, or a table key. Empty for neither. */
    std::string boxOf (const synth::Table& table, const std::string& word)
    {
        for (const auto& column : Panel::pile())
            for (const auto& name : column)
            {
                const int i = Panel::indexOf (name);
                if (i >= 0 && Panel::wordOf (i).toStdString() == word)
                    return name.toStdString();
            }
        const bool known = std::any_of (table.begin(), table.end(), [&word] (const auto& s) { return s.first == word + " cap"; });
        return known ? word : std::string();
    }

    /** B where it combs least with A: both rounds, every place judged, on all the machine's threads. */
    synth::Place quietest (const synth::Table& table, const State& s)
    {
        const synth::BSearch search (table, s);
        const auto judgeAll = [&search] (const std::vector<synth::Place>& all)
        {
            std::vector<synth::BSearch::Judged> judged (all.size());
            const size_t lanes = std::clamp<size_t> (std::thread::hardware_concurrency(), 1, 32);
            std::vector<std::future<void>> work;
            for (size_t lane = 0; lane < lanes; ++lane)
                work.push_back (std::async (std::launch::async, [&, lane] { for (size_t i = lane; i < all.size(); i += lanes) judged[i] = search.score (all[i]); }));
            for (auto& w : work) w.get();
            return judged;
        };
        const auto first = search.coarse();
        const auto firstJudged = judgeAll (first);
        const auto second = search.fine (first, firstJudged);
        return search.best (first, firstJudged, second, judgeAll (second));
    }

    const char* backWord (int b) { return b == 0 ? "closed" : b == 1 ? "ported" : "open"; }

    std::string hornWord (const State& s)
    {
        if (! s.horn) return "off";
        return s.hornDb < -9 ? "-12" : s.hornDb < -3 ? "-6" : "0";
    }

    std::string number (double v, int decimals)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.*f", decimals, v);
        return b;
    }

    /** The page's address, opening the generator on `s` (panel/Link.h) — as each WAV says it too. */
    std::string linkTo (const State& s) { return std::string (pageAddress) + "#" + linkOf (s); }

    std::string tsvLine (const Item& it)
    {
        const auto& s = it.state;
        const int at = Panel::indexOf (ui::String (s.box));
        const int back = s.back >= 0 ? s.back : Panel::ownBack (at);
        const auto mic = [] (const Mic& m)
        {
            if (! m.on) return std::string ("off\t\t\t\t");
            return m.model + "\t" + number (m.x, 3) + "\t" + number (m.y, 3) + "\t" + number (m.distance, 3) + "\t" + number (m.angle, 1);
        };
        return it.file + "\t" + it.style + "\t" + it.name + "\t" + Panel::wordOf (at).toStdString() + "\t" + backWord (back) + "\t"
             + hornWord (s) + "\t" + mic (s.a) + "\t" + mic (s.b) + "\t" + number (s.blend, 3) + "\t" + (s.align ? "on" : "off") + "\t"
             + (s.invert ? "inverted" : "normal") + "\t" + number (s.shiftMs, 3) + "\t" + Panel::labelOf (s).toStdString() + "\t"
             + linkTo (s) + "\n";
    }

    /** A response as the panel draws it (Panel::paintResponse): the curve, and how bad each point is — the deeper of the
        pit it falls into and its two mics cancelling there. */
    struct Curve { std::vector<float> db, bad; };

    Curve curveOf (const synth::PairParts& parts, double rate)
    {
        Curve c;
        c.db = synth::responseOf (synth::as24Bit (parts.mix), rate);
        c.bad = synth::pitsOf (c.db);
        if (! parts.a.empty())
        {
            const auto comb = synth::combOf (parts.a, parts.b, rate);
            for (size_t i = 0; i < c.bad.size() && i < comb.size(); ++i)
                c.bad[i] = std::max (c.bad[i], -comb[i]);
        }
        return c;
    }

    std::string escaped (const std::string& s)
    {
        std::string out;
        for (const char c : s)
            out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string (1, c);
        return out;
    }

    /** The panel's green, reddening to its red by `t` 0..1, as the line's shades are. */
    std::string shadeOf (float t)
    {
        const auto mix = [t] (int a, int b) { return (int) std::lround (a + (b - a) * t); };
        char b[8];
        std::snprintf (b, sizeof b, "#%02x%02x%02x", mix (0x3f, 0xd5), mix (0xae, 0x45), mix (0x6a, 0x5a));
        return b;
    }

    /** RESPONSES.SVG: every cabinet of a folder, its response as the panel draws it — 30 Hz to 12 kHz on a log scale,
        +15 to -30 dB, green where it is fine, red from 3 dB into a pit or where the mics cancel, full at 6 — four across,
        each under its name and its mics, a click on one opening it on the page. Nothing read under 13 px. */
    std::string responsesSvg (const std::vector<Item>& items, const std::vector<Curve>& curves)
    {
        constexpr int columns = 4;
        constexpr float cellW = 360.0f, cellH = 216.0f, head = 72.0f;
        constexpr float px = 52.0f, py = 62.0f, pw = cellW - px - 22.0f, ph = cellH - py - 34.0f;   // the plot, in a cell
        constexpr float top = 15.0f, bottom = -30.0f;
        const int rows = ((int) items.size() + columns - 1) / columns;
        const auto f = [] (double v) { return number (v, 1); };
        const auto xOf = [] (double hz) { return px + pw * std::log (hz / 30.0) / std::log (12000.0 / 30.0); };
        const auto yOf = [] (double db) { return py + (top - db) / (top - bottom) * ph; };
        const std::string font = "font-family=\"Helvetica Neue, Helvetica, Arial, sans-serif\"";
        const std::string faint = "fill=\"#6f6b86\" font-size=\"13\" " + font;

        std::string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + f (columns * cellW) + "\" height=\""
                        + f (head + rows * cellH + 8.0f) + "\" viewBox=\"0 0 " + f (columns * cellW) + " " + f (head + rows * cellH + 8.0f) + "\">\n"
                        + "<rect width=\"100%\" height=\"100%\" fill=\"#111119\"/>\n";
        out += "<text x=\"20\" y=\"32\" fill=\"#ece9f6\" font-size=\"20\" font-weight=\"600\" letter-spacing=\"1.5\" " + font + ">"
             + escaped (ui::String (items.empty() ? std::string() : items.front().style).toUpperCase().toStdString()) + " \xc2\xb7 "
             + std::to_string (items.size()) + " CABINETS</text>\n";
        out += "<text x=\"20\" y=\"56\" fill=\"#a6a2bd\" font-size=\"13\" " + font + ">Cab IR Generator " + CABIR_VERSION
             + ": each one's response at 48 kHz, levelled to its own 100 Hz to 5 kHz. Green is fine; red falls 3 dB or more into "
               "a pit, or its two mics cancel there. A click opens one in the generator.</text>\n";

        for (size_t i = 0; i < items.size() && i < curves.size(); ++i)
        {
            const auto& it = items[i];
            const auto& c = curves[i];
            const float x0 = (float) (i % columns) * cellW, y0 = head + (float) (i / columns) * cellH;
            out += "<a href=\"" + escaped (linkTo (it.state)) + "\" target=\"_blank\"><g transform=\"translate(" + f (x0) + " " + f (y0) + ")\">\n";
            out += "<rect x=\"8\" y=\"6\" width=\"" + f (cellW - 16) + "\" height=\"" + f (cellH - 12)
                 + "\" rx=\"6\" fill=\"#15151f\" stroke=\"#9778ff\" stroke-opacity=\"0.24\"/>\n";
            out += "<text x=\"20\" y=\"28\" fill=\"#ece9f6\" font-size=\"15\" font-weight=\"600\" letter-spacing=\"0.6\" " + font + ">"
                 + escaped (it.name) + "</text>\n";
            out += "<text x=\"20\" y=\"46\" " + faint + ">" + escaped (Panel::labelOf (it.state).toStdString()) + "</text>\n";

            static constexpr std::pair<double, const char*> marks[] { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                                       { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
            for (const auto& [hz, word] : marks)
                out += "<rect x=\"" + f (xOf (hz)) + "\" y=\"" + f (py) + "\" width=\"1\" height=\"" + f (ph)
                     + "\" fill=\"#9778ff\" fill-opacity=\"0.12\"/><text x=\"" + f (xOf (hz)) + "\" y=\"" + f (py + ph + 17)
                     + "\" text-anchor=\"middle\" " + faint + ">" + word + "</text>\n";
            static constexpr std::pair<double, const char*> levels[] { { 10, "+10" }, { 0, "0" }, { -10, "-10" }, { -20, "-20" }, { -30, "-30" } };
            for (const auto& [db, word] : levels)
                out += "<rect x=\"" + f (px) + "\" y=\"" + f (yOf (db)) + "\" width=\"" + f (pw) + "\" height=\"1\" fill=\"#9778ff\" fill-opacity=\""
                     + (std::abs (db) < 1.0 ? "0.24" : "0.12") + "\"/><text x=\"" + f (px - 6) + "\" y=\"" + f (yOf (db) + 4.5)
                     + "\" text-anchor=\"end\" " + faint + ">" + word + "</text>\n";

            // The line in runs of one shade, each segment as red as the worse of its two ends — the panel's own.
            if ((int) c.db.size() == synth::responsePoints && c.bad.size() == c.db.size())
            {
                out += "<clipPath id=\"plot" + std::to_string (i) + "\"><rect x=\"" + f (px) + "\" y=\"" + f (py - 2) + "\" width=\"" + f (pw)
                     + "\" height=\"" + f (ph + 4) + "\"/></clipPath><g clip-path=\"url(#plot" + std::to_string (i) + ")\" fill=\"none\" "
                       "stroke-width=\"1.6\" stroke-linejoin=\"round\" stroke-linecap=\"round\">\n";
                const auto at = [&c] (int k) { return std::clamp ((int) std::ceil ((c.bad[(size_t) k] - 3.0f) / 3.0f * 8.0f), 0, 8); };
                const auto shade = [&at] (int k) { return std::max (at (k), at (k + 1)); };
                for (int from = 0; from < synth::responsePoints - 1;)
                {
                    const int level = shade (from);
                    int to = from + 1;
                    while (to < synth::responsePoints - 1 && shade (to) == level)
                        ++to;
                    out += "<polyline stroke=\"" + shadeOf ((float) level / 8.0f) + "\" points=\"";
                    for (int k = from; k <= to; ++k)
                        out += (k == from ? "" : " ") + f (xOf (synth::responseHz (k))) + ","
                             + f (yOf (std::clamp ((double) c.db[(size_t) k], 3.0 * bottom, 3.0 * top)));
                    out += "\"/>\n";
                    from = to;
                }
                out += "</g>\n";
            }
            out += "</g></a>\n";
        }
        return out + "</svg>\n";
    }
}

int main (int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf (stderr, "cabpack <cabinets.txt> <presets folder> <typical.toml> <out folder>\n");
        return 2;
    }
    const auto table = synth::readTable (argv[1]);
    std::map<std::string, std::vector<Item>> folders;   // by folder name, each in file order

    // The factory presets, a folder for each style.
    std::vector<fsys::path> presets;
    for (const auto& e : fsys::directory_iterator (argv[2]))
        if (e.path().extension() == ".toml")
            presets.push_back (e.path());
    std::sort (presets.begin(), presets.end());
    int bad = 0;
    for (const auto& p : presets)
    {
        Item it;
        it.text = slurp (p);
        auto parsed = t::parse (it.text);
        const auto state = fromToml (it.text);
        const auto* root = std::get_if<t::Table> (&parsed);
        if (! state || root == nullptr || Panel::indexOf (ui::String (state->box)) < 0)
        {
            std::fprintf (stderr, "BAD  %s\n", p.string().c_str());
            ++bad;
            continue;
        }
        it.state = *state;
        it.file = p.stem().string();
        it.name = field (*root, "name");
        it.style = field (*root, "style");
        folders[lower (it.style)].push_back (std::move (it));
    }

    // The typical cabinets: A as DEFAULT's, B where FIND finds it quietest.
    {
        auto parsed = t::parse (slurp (argv[3]));
        const auto* root = std::get_if<t::Table> (&parsed);
        const auto* list = root != nullptr ? root->find ("cab") : nullptr;
        const auto* cabs = list != nullptr ? std::get_if<t::Tables> (&list->data) : nullptr;
        if (cabs == nullptr)
        {
            std::fprintf (stderr, "BAD  %s: no [[cab]]\n", argv[3]);
            return 1;
        }
        for (const auto& c : *cabs)
        {
            const auto id = field (c, "id"), back = field (c, "back"), horn = field (c, "horn");
            const auto box = boxOf (table, field (c, "box"));
            const std::map<std::string, int> backs { { "own", -1 }, { "closed", 0 }, { "ported", 1 }, { "open", 2 } };
            const std::map<std::string, double> horns { { "-12", -12.0 }, { "-6", -6.0 }, { "0", 0.0 } };
            if (id.empty() || box.empty() || backs.count (back) == 0 || (horn != "off" && horns.count (horn) == 0))
            {
                std::fprintf (stderr, "BAD  typical %s\n", id.c_str());
                ++bad;
                continue;
            }
            State s;
            s.box = box;
            s.back = backs.at (back);
            s.horn = horn != "off";
            s.hornDb = s.horn ? horns.at (horn) : 0.0;
            s.a = { true, "md421", -0.3, 0.0, 0.05, 0.0 };
            s.b = { true, "u47", -0.65, 0.0, 0.12, 0.0 };
            s.blend = 0.35;
            s.align = true;
            const auto place = quietest (table, s);
            s.b.x = place.x;
            s.b.y = place.y;
            s.b.distance = place.distance;
            s.shiftMs = place.shiftMs;

            Item it;
            it.state = s;
            it.file = id;
            it.style = "Typical";
            // A name of its own, or its size, back and horn said — the app's line alone names neither its own back nor the
            // horn, and two of the list would read alike in a preset menu.
            std::string said = Panel::wordOf (Panel::indexOf (ui::String (s.box))).toStdString() + " " + ui::String (backWord (s.back >= 0 ? s.back : Panel::ownBack (Panel::indexOf (ui::String (s.box))))).toUpperCase().toStdString()
                             + (s.horn ? " HORN " + hornWord (s) : std::string (" NO HORN")) + " \xc2\xb7 MD421+U47";
            it.name = field (c, "name").empty() ? said : field (c, "name");
            t::Table head;
            (void) head.insert ("name", t::Value (it.name));
            (void) head.insert ("style", t::Value (it.style));
            it.text = t::write (head) + toToml (s);
            std::printf ("typical %-20s B at %+.2f %+.2f %4.1f cm, shift %+.2f ms\n", id.c_str(), place.x, place.y, place.distance * 100, place.shiftMs);
            folders["typical"].push_back (std::move (it));
        }
    }

    // Every cabinet at every rate.
    struct Rate { double hz; const char* folder; size_t n; bool drawn; };   // drawn: the rate responses.svg is drawn at
    static constexpr Rate rates[] { { 44100.0, "44.1k", 1 << 14, false }, { 48000.0, "48k", 1 << 14, true }, { 96000.0, "96k", 1 << 15, false } };
    const fsys::path out (argv[4]);
    int written = 0;
    for (const auto& [folder, items] : folders)
    {
        std::string tsv = "file\tstyle\tname\tsize\tback\thorn\ta_model\ta_x\ta_y\ta_distance_m\ta_angle_deg\t"
                          "b_model\tb_x\tb_y\tb_distance_m\tb_angle_deg\tblend\talign\tpolarity\tshift_ms\tlabel\tlink\n";
        std::vector<Curve> curves;
        for (const auto& it : items)
            tsv += tsvLine (it);
        fsys::create_directories (out / folder);
        std::ofstream (out / folder / "index.tsv", std::ios::binary) << tsv;

        for (const auto& r : rates)
        {
            const auto dir = out / folder / r.folder;
            fsys::create_directories (dir);
            for (const auto& it : items)   // one at a time: each render spreads its bins and its two mics over threads itself
            {
                const auto parts = synth::PairRenderer {}.render (table, it.state.box, synth::pairOf (it.state), r.n, r.hz);
                const auto& ir = parts.mix;
                if (r.drawn)
                    curves.push_back (curveOf (parts, r.hz));
                // What each WAV says of itself (synth::WavMeta): its name, the link that opens it on the page, its preset.
                synth::WavMeta meta;
                meta.name = it.name;
                meta.software = std::string ("Cab IR Generator ") + CABIR_VERSION;
                meta.comment = linkTo (it.state);
                meta.preset = it.text;
                const auto wav = synth::wav24 (ir, (int) r.hz, &meta);
                std::ofstream (dir / (it.file + ".wav"), std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
                std::ofstream (dir / (it.file + ".toml"), std::ios::binary) << it.text;
                ++written;
            }
        }
        std::ofstream (out / folder / "responses.svg", std::ios::binary) << responsesSvg (items, curves);
        std::printf ("%-8s %3zu cabinets x 3 rates\n", folder.c_str(), items.size());
    }
    std::printf ("%d IRs written, %d bad\n", written, bad);
    return bad == 0 ? 0 : 1;
}
