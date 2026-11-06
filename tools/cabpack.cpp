// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE IR PACKS a page offers for download, made again whenever the presets change — the same inputs, the same bytes.
//
//   cabpack <cabinets.txt> <presets folder> <typical.toml> <out folder>
//   cabpack --typical <cabinets.txt> <typical.toml>
//
// A folder for each preset style (rock, metal, ...) and one for the typical cabinets, in typical.toml's order, each the
// preset --typical made of it in typical/ beside the list (a box, its back, its horn from the list; A DEFAULT's MD421
// and B a U47, spread as the presets' are, synth/PairSpread.h: A off its pit as FIND moves one mic, then B where the two
// meet without a deep comb and the sum without a pit — no deadline, so a run again finds the same; kept as presets so a
// plugin carries them as it carries any). In each, 44.1k/, 48k/ and 96k/, every cabinet synthesised at that rate: a
// 24-bit mono WAV as
// the app's DOWNLOAD IR writes it, and beside it its state as the app's preset — and index.tsv, a line for each, its last
// column the link that opens it on the page; and its response as the panel draws it, with what the line and the bands say
// — each alone in responses/<file>.svg, all of them together in responses.svg — a click on one opening it there too.
// And the same by size, size/8x10/ and so on: every cabinet of that size, of every style and the typical ones — a list,
// index.tsv saying in its last column, `folder`, where each one's files are, and the size's own responses.svg.

#include <panel/Link.h>
#include <panel/Panel.h>
#include <synth/CabSynth.h>
#include <synth/PairSpread.h>
#include <synth/Response.h>
#include <synth/State.h>

#include <felitronics/toml/Toml.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <optional>
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

    /** A typical cabinet's mics as the presets' were spread (synth/PairSpread.h, tools/respread): A first, heard alone,
        off its pit about where it stands if it has one — as FIND moves one mic — then B across the speaker where the two
        meet without a deep comb and the sum without a pit, at its best TIME; at the preset's precision, so the WAV is
        what its preset says. */
    State placed (const synth::Table& table, State s)
    {
        auto alone = s;
        alone.b.on = false;
        if (synth::green::judgeOf (table, alone).second > synth::green::pitDb)
            s.a = synth::green::smoothed (table, alone).a;
        s = synth::green::withB (s, synth::green::spreadB (table, s).found);
        if (const auto kept = fromToml (toToml (s)))
            s = *kept;
        return s;
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

    /** A response as the panel draws it (Panel::paintResponse): the curve, how bad each point is — the deeper of the pit
        it falls into and its two mics cancelling there — and, with two mics heard, where they meet (synth::combOf). */
    struct Curve { std::vector<float> db, bad, comb; };

    Curve curveOf (const synth::PairParts& parts, double rate)
    {
        Curve c;
        c.db = synth::responseOf (synth::as24Bit (parts.mix), rate);
        c.bad = synth::pitsOf (c.db);
        if (! parts.a.empty())
        {
            c.comb = synth::combOf (parts.a, parts.b, rate);
            for (size_t i = 0; i < c.bad.size() && i < c.comb.size(); ++i)
                c.bad[i] = std::max (c.bad[i], -c.comb[i]);
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

    /** What a cabinet is, in a line: its size, its back, its horn where the box has one, its mics — with two, how much of
        each: "8x10 · CLOSED · HORN -6 dB · SM57 65% + U87 35%". */
    std::string saidOf (const State& s)
    {
        const int at = Panel::indexOf (ui::String (s.box));
        if (at < 0)
            return s.box;
        const auto& cab = Panel::cabs()[(size_t) at];
        const std::string dot = " \xc2\xb7 ";
        std::string out = Panel::wordOf (at).toStdString() + dot + Panel::backWords[s.back >= 0 ? s.back : Panel::ownBack (at)];
        if (cab.horn)
            out += dot + (s.horn ? "HORN " + number (std::round (s.hornDb), 0) + " dB" : std::string ("NO HORN"));
        const auto mic = [&cab] (const Mic& m) { return (m.model.empty() ? cab.mic : ui::String (m.model)).toUpperCase().toStdString(); };
        if (s.a.on && s.b.on)
        {
            const int b = (int) std::lround (s.blend * 100.0);
            out += dot + mic (s.a) + " " + std::to_string (100 - b) + "% + " + mic (s.b) + " " + std::to_string (b) + "%";
        }
        else
            out += dot + mic (s.a.on ? s.a : s.b);
        return out;
    }

    const std::string svgFont = "font-family=\"Helvetica Neue, Helvetica, Arial, sans-serif\"";
    std::string svgNumber (double v) { return number (v, 1); }

    /** One cabinet's chart as the panel draws its response, `w` x `h` from its top left: its name, what it is (saidOf),
        and on 30 Hz to 12 kHz, +15 to -30 dB, the two mics' comb as bands under the line — green where they add, red where
        they cancel, the deeper the stronger, full at 6 dB — and the line, green where it is fine, red from 3 dB into a pit
        or where the mics cancel, full at 6. `id` keeps its clip apart from the others' in one file. */
    std::string chartSvg (const Item& it, const Curve& c, float w, float h, float nameSize, const std::string& id)
    {
        const float px = 52.0f, py = 44.0f + nameSize, pw = w - px - 22.0f, ph = h - py - 34.0f;
        constexpr float top = 15.0f, bottom = -30.0f;
        const auto f = svgNumber;
        const auto xOf = [px, pw] (double hz) { return px + pw * std::log (hz / 30.0) / std::log (12000.0 / 30.0); };
        const auto yOf = [py, ph] (double db) { return py + (top - db) / (top - bottom) * ph; };
        const std::string faint = "fill=\"#6f6b86\" font-size=\"13\" " + svgFont;

        std::string out = "<rect x=\"8\" y=\"6\" width=\"" + f (w - 16) + "\" height=\"" + f (h - 12)
                        + "\" rx=\"6\" fill=\"#15151f\" stroke=\"#9778ff\" stroke-opacity=\"0.24\"/>\n";
        out += "<text x=\"20\" y=\"" + f (14 + nameSize) + "\" fill=\"#ece9f6\" font-size=\"" + f (nameSize)
             + "\" font-weight=\"600\" letter-spacing=\"0.6\" " + svgFont + ">" + escaped (it.name) + "</text>\n";
        out += "<text x=\"20\" y=\"" + f (32 + nameSize) + "\" fill=\"#a6a2bd\" font-size=\"13\" " + svgFont + ">" + escaped (saidOf (it.state))
             + "</text>\n";

        // Under it all, where the two mics meet — in runs of one shade, a tenth of the full colour a step.
        if ((int) c.comb.size() == synth::responsePoints)
        {
            const auto stepOf = [&c] (int i)
            {
                const float v = c.comb[(size_t) i];
                const int step = (int) std::lround (std::min (1.0f, std::abs (v) / 6.0f) * 10.0f);
                return v > 0.0f ? step : -step;
            };
            const auto edge = [&xOf, px, pw] (int i)
            {
                if (i <= 0) return (double) px;
                if (i >= synth::responsePoints) return (double) (px + pw);
                return 0.5 * (xOf (synth::responseHz (i - 1)) + xOf (synth::responseHz (i)));
            };
            for (int from = 0; from < synth::responsePoints;)
            {
                const int step = stepOf (from);
                int to = from + 1;
                while (to < synth::responsePoints && stepOf (to) == step)
                    ++to;
                if (step != 0)
                    out += "<rect x=\"" + f (edge (from)) + "\" y=\"" + f (py) + "\" width=\"" + f (edge (to) - edge (from) + 0.5) + "\" height=\""
                         + f (ph) + "\" fill=\"" + (step > 0 ? "#3fae6a" : "#d5455a") + "\" fill-opacity=\"" + number (0.045 * std::abs (step), 3) + "\"/>\n";
                from = to;
            }
        }

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
            out += "<clipPath id=\"plot" + id + "\"><rect x=\"" + f (px) + "\" y=\"" + f (py - 2) + "\" width=\"" + f (pw)
                 + "\" height=\"" + f (ph + 4) + "\"/></clipPath><g clip-path=\"url(#plot" + id + ")\" fill=\"none\" "
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
        return out;
    }

    /** What the line and the bands say, from (x, y) down, a swatch before each — a sentence a line where the page is wide,
        in two where it is not; the bands' only where there are any (`bands`). Its height. */
    float legendSvg (std::string& out, float x, float y, bool wide, bool bands)
    {
        const auto f = svgNumber;
        const std::string words = "fill=\"#a6a2bd\" font-size=\"13\" " + svgFont;
        float at = y;
        const auto said = [&] (const std::vector<const char*>& lines)
        {
            std::string all;
            for (const auto* line : lines)
                all += (all.empty() ? "" : " ") + std::string (line);
            for (const auto& line : wide ? std::vector<std::string> { all } : std::vector<std::string> (lines.begin(), lines.end()))
            {
                out += "<text x=\"" + f (x + 32) + "\" y=\"" + f (at) + "\" " + words + ">" + line + "</text>\n";
                at += 20.0f;
            }
        };
        out += "<line x1=\"" + f (x) + "\" y1=\"" + f (at - 4) + "\" x2=\"" + f (x + 12) + "\" y2=\"" + f (at - 4)
             + "\" stroke=\"#3fae6a\" stroke-width=\"2\"/><line x1=\"" + f (x + 12) + "\" y1=\"" + f (at - 4) + "\" x2=\"" + f (x + 24)
             + "\" y2=\"" + f (at - 4) + "\" stroke=\"#d5455a\" stroke-width=\"2\"/>\n";
        said ({ "The line: the cabinet's response at 48 kHz, levelled to its own 100 Hz to 5 kHz.",
                bands ? "Green is fine; red falls 3 dB or more into a pit, or the two mics cancel there."
                      : "Green is fine; red falls 3 dB or more into a pit." });
        if (bands)
        {
            out += "<rect x=\"" + f (x) + "\" y=\"" + f (at - 10) + "\" width=\"12\" height=\"12\" fill=\"#3fae6a\" fill-opacity=\"0.45\"/><rect x=\""
                 + f (x + 12) + "\" y=\"" + f (at - 10) + "\" width=\"12\" height=\"12\" fill=\"#d5455a\" fill-opacity=\"0.45\"/>\n";
            said ({ "The bands, with two mics: where they meet. Green, they add up; red, they cancel.",
                    "The stronger the colour, the deeper, full at 6 dB." });
        }
        return at - y;
    }

    std::string svgOpen (float w, float h)
    {
        const auto f = svgNumber;
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + f (w) + "\" height=\"" + f (h) + "\" viewBox=\"0 0 " + f (w) + " " + f (h)
             + "\">\n<rect width=\"100%\" height=\"100%\" fill=\"#111119\"/>\n";
    }

    /** RESPONSES.SVG: every cabinet of a folder (chartSvg), four across, under its title — "ROCK · 20 BASS CABINETS from
        Darwin's Cat" (the owner, 06.11), the style upper-cased or the size as it is written — what made it, and what the
        line and the bands say, a click on one opening it on the page. Nothing read under 13 px. */
    std::string responsesSvg (const std::string& title, const std::vector<Item>& items, const std::vector<Curve>& curves)
    {
        constexpr int columns = 4;
        constexpr float cellW = 384.0f, cellH = 216.0f, head = 112.0f;
        const int rows = ((int) items.size() + columns - 1) / columns;
        const auto f = svgNumber;
        std::string out = svgOpen (columns * cellW, head + (float) rows * cellH + 8.0f);
        out += "<text x=\"20\" y=\"32\" fill=\"#ece9f6\" font-size=\"20\" font-weight=\"600\" letter-spacing=\"1.5\" " + svgFont + ">"
             + escaped (title) + " \xc2\xb7 " + std::to_string (items.size()) + " BASS CABINETS from Darwin's Cat</text>\n";
        out += "<text x=\"20\" y=\"56\" fill=\"#a6a2bd\" font-size=\"13\" " + svgFont + ">Made with Cab IR Generator " + CABIR_VERSION
             + ". A click on one opens it in the generator.</text>\n";
        legendSvg (out, 20, 80, true, true);
        for (size_t i = 0; i < items.size() && i < curves.size(); ++i)
        {
            const float x0 = (float) (i % columns) * cellW, y0 = head + (float) (i / columns) * cellH;
            out += "<a href=\"" + escaped (linkTo (items[i].state)) + "\" target=\"_blank\"><g transform=\"translate(" + f (x0) + " " + f (y0) + ")\">\n"
                 + chartSvg (items[i], curves[i], cellW, cellH, 15.0f, std::to_string (i)) + "</g></a>\n";
        }
        return out + "</svg>\n";
    }

    /** RESPONSES/<file>.SVG: one cabinet alone, large — what it is, its chart, and what the line and the bands say — the
        whole of it a link to it on the page. */
    std::string responseSvg (const Item& it, const Curve& c)
    {
        constexpr float w = 760.0f, chartH = 380.0f;
        std::string legend;
        const float tall = legendSvg (legend, 20, chartH + 18, false, ! c.comb.empty());
        return svgOpen (w, chartH + tall + 14) + "<a href=\"" + escaped (linkTo (it.state)) + "\" target=\"_blank\">\n"
             + chartSvg (it, c, w, chartH, 18.0f, "0") + legend + "</a>\n</svg>\n";
    }
}

namespace
{
    /** A preset file as an Item — the style's name for its folder, the file's stem for its files — or none. */
    std::optional<Item> presetAt (const fsys::path& p)
    {
        Item it;
        it.text = slurp (p);
        auto parsed = t::parse (it.text);
        const auto state = fromToml (it.text);
        const auto* root = std::get_if<t::Table> (&parsed);
        if (! state || root == nullptr || Panel::indexOf (ui::String (state->box)) < 0)
            return std::nullopt;
        it.state = *state;
        it.file = p.stem().string();
        it.name = field (*root, "name");
        it.style = field (*root, "style");
        return it;
    }

    /** The typical list (typical.toml), each cabinet's id and, where it has one, its own name. */
    template <typename Parsed>
    const t::Tables* typicalList (const Parsed& parsed)
    {
        const auto* root = std::get_if<t::Table> (&parsed);
        const auto* list = root != nullptr ? root->find ("cab") : nullptr;
        return list != nullptr ? std::get_if<t::Tables> (&list->data) : nullptr;
    }

    /** CABPACK --TYPICAL: the typical cabinets made presets, each beside the list in typical/<id>.toml — a box, its back
        and its horn from the list, its mics spread as the presets' are (placed) — so the packs and a plugin read them as
        they read any preset, and only this searches. */
    int writeTypical (const synth::Table& table, const fsys::path& list)
    {
        const auto parsed = t::parse (slurp (list));
        const auto* cabs = typicalList (parsed);
        if (cabs == nullptr)
        {
            std::fprintf (stderr, "BAD  %s: no [[cab]]\n", list.string().c_str());
            return 1;
        }
        int bad = 0;
        struct Typical { std::string id, name; State state; };
        std::vector<Typical> typicals;
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
            typicals.push_back ({ id, field (c, "name"), s });
        }
        // A cabinet a lane, on a quarter of the machine's threads — each render spreads its bins and its two mics over
        // threads itself (as tools/respread does).
        {
            std::vector<std::future<void>> work;
            const size_t lanes = std::max<size_t> (1, std::thread::hardware_concurrency() / 4);
            for (size_t lane = 0; lane < lanes; ++lane)
                work.push_back (std::async (std::launch::async, [&, lane]
                {
                    for (size_t i = lane; i < typicals.size(); i += lanes)
                        typicals[i].state = placed (table, typicals[i].state);
                }));
            for (auto& w : work) w.get();
        }
        const auto folder = fsys::path (list).replace_extension();
        fsys::create_directories (folder);
        for (const auto& [id, ownName, s] : typicals)
        {
            // A name of its own, or its size, back and horn said — the app's line alone names neither its own back nor the
            // horn, and two of the list would read alike in a preset menu.
            std::string said = Panel::wordOf (Panel::indexOf (ui::String (s.box))).toStdString() + " " + ui::String (backWord (s.back >= 0 ? s.back : Panel::ownBack (Panel::indexOf (ui::String (s.box))))).toUpperCase().toStdString()
                             + (s.horn ? " HORN " + hornWord (s) : std::string (" NO HORN")) + " \xc2\xb7 MD421+U47";
            t::Table head;
            (void) head.insert ("name", t::Value (ownName.empty() ? said : ownName));
            (void) head.insert ("style", t::Value (std::string ("Typical")));
            std::ofstream (folder / (id + ".toml"), std::ios::binary) << t::write (head) + toToml (s);
            std::printf ("typical %-20s A at %+.2f %+.2f %4.1f cm, B at %+.2f %+.2f %4.1f cm, shift %+.2f ms, blend %.2f\n", id.c_str(),
                         s.a.x, s.a.y, s.a.distance * 100, s.b.x, s.b.y, s.b.distance * 100, s.shiftMs, s.blend);
        }
        return bad == 0 ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    if (argc == 4 && std::string (argv[1]) == "--typical")
        return writeTypical (synth::readTable (argv[2]), argv[3]);
    if (argc != 5)
    {
        std::fprintf (stderr, "cabpack <cabinets.txt> <presets folder> <typical.toml> <out folder>\n"
                              "cabpack --typical <cabinets.txt> <typical.toml>     (the typical cabinets as presets, typical/<id>.toml)\n");
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
        if (auto it = presetAt (p))
            folders[lower (it->style)].push_back (std::move (*it));
        else
        {
            std::fprintf (stderr, "BAD  %s\n", p.string().c_str());
            ++bad;
        }
    }

    // The typical cabinets, in the list's order, each as cabpack --typical made it a preset.
    {
        const auto parsed = t::parse (slurp (argv[3]));
        const auto* cabs = typicalList (parsed);
        if (cabs == nullptr)
        {
            std::fprintf (stderr, "BAD  %s: no [[cab]]\n", argv[3]);
            return 1;
        }
        const auto folder = fsys::path (argv[3]).replace_extension();
        for (const auto& c : *cabs)
        {
            const auto p = folder / (field (c, "id") + ".toml");
            if (auto it = presetAt (p); it && lower (it->style) == "typical")
                folders["typical"].push_back (std::move (*it));
            else
            {
                std::fprintf (stderr, "BAD  %s: none, or not a typical one (cabpack --typical makes them)\n", p.string().c_str());
                ++bad;
            }
        }
    }

    // Every cabinet at every rate, a folder for each style.
    struct Rate { double hz; const char* folder; size_t n; bool drawn; };   // drawn: the rate responses.svg is drawn at
    static constexpr Rate rates[] { { 44100.0, "44.1k", 1 << 14, false }, { 48000.0, "48k", 1 << 14, true }, { 96000.0, "96k", 1 << 15, false } };
    const fsys::path out (argv[4]);
    int written = 0;
    // What every folder holds beside its rates: index.tsv, responses.svg and responses/ — and how many of its cabinets the
    // panel's line stays green on, under 3 dB everywhere, each one it reddens on named. A size's (`homes`: the folder each
    // of its cabinets lives in) holds no files of its own: its index.tsv says where each one's are, in a last column,
    // `folder`, and only its responses.svg is its own.
    const auto finish = [&out] (const std::string& folder, const std::string& title, const std::vector<Item>& items,
                                const std::vector<Curve>& curves, const std::vector<std::string>* homes)
    {
        fsys::create_directories (out / folder);
        std::string tsv = "file\tstyle\tname\tsize\tback\thorn\ta_model\ta_x\ta_y\ta_distance_m\ta_angle_deg\t"
                          "b_model\tb_x\tb_y\tb_distance_m\tb_angle_deg\tblend\talign\tpolarity\tshift_ms\tlabel\tlink";
        tsv += homes != nullptr ? "\tfolder\n" : "\n";
        for (size_t i = 0; i < items.size(); ++i)
        {
            auto line = tsvLine (items[i]);
            if (homes != nullptr)
                line.insert (line.size() - 1, "\t" + (*homes)[i]);
            tsv += line;
        }
        std::ofstream (out / folder / "index.tsv", std::ios::binary) << tsv;
        std::ofstream (out / folder / "responses.svg", std::ios::binary) << responsesSvg (title, items, curves);
        if (homes == nullptr)
            fsys::create_directories (out / folder / "responses");
        int clear = 0;
        std::string red;
        for (size_t i = 0; i < items.size() && i < curves.size(); ++i)
        {
            if (homes == nullptr)
                std::ofstream (out / folder / "responses" / (items[i].file + ".svg"), std::ios::binary) << responseSvg (items[i], curves[i]);
            const float worst = curves[i].bad.empty() ? 0.0f : *std::max_element (curves[i].bad.begin(), curves[i].bad.end());
            if (worst <= 3.0f)
                ++clear;
            else
                red += "         red " + items[i].file + "  " + number (worst, 1) + " dB\n";
        }
        std::printf ("%-10s %3zu cabinets x 3 rates, %d clear\n%s", folder.c_str(), items.size(), clear, red.c_str());
    };
    struct Made { const Item* item; std::string folder; Curve curve; };   // each cabinet as it was made, in the folders' order
    std::vector<Made> made;
    for (const auto& [folder, items] : folders)
    {
        std::vector<Curve> curves;
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
        finish (folder, ui::String (items.empty() ? folder : items.front().style).toUpperCase().toStdString(), items, curves, nullptr);
        for (size_t i = 0; i < items.size() && i < curves.size(); ++i)
            made.push_back ({ &items[i], folder, curves[i] });
    }

    // ...and the same again by size (the owner, 06.11: a bassist with one 8x10 wants every 8x10), size/8x10/ and so on —
    // every style's of that size and the typical ones, in the folders' order: a list of them, each one's files where they
    // live (the owner, 06.11: one store, the sizes pointing into it — a page's zip gathers them).
    std::map<std::string, std::vector<const Made*>> sizes;
    for (const auto& m : made)
        sizes[Panel::wordOf (Panel::indexOf (ui::String (m.item->state.box))).toStdString()].push_back (&m);
    for (const auto& [size, list] : sizes)
    {
        const auto folder = "size/" + size;
        std::vector<Item> items;
        std::vector<Curve> curves;
        std::vector<std::string> homes;
        for (const auto* m : list)
        {
            items.push_back (*m->item);
            curves.push_back (m->curve);
            homes.push_back (m->folder);
        }
        finish (folder, size, items, curves, &homes);
    }
    std::printf ("%d IRs written, %d bad\n", written, bad);
    return bad == 0 ? 0 : 1;
}
