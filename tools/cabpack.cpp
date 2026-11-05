// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

// THE IR PACKS a page offers for download, made again whenever the presets change — the same inputs, the same bytes.
//
//   cabpack <cabinets.txt> <presets folder> <typical.toml> <out folder>
//
// A folder for each preset style (rock, metal, ...) and one for the typical cabinets (typical.toml: a box, its back, its
// horn; A as DEFAULT's, B a U47 where FIND finds it quietest, every place of both rounds judged — no deadline, so a run
// again finds the same). In each, 44.1k/, 48k/ and 96k/, every cabinet synthesised at that rate: a 24-bit mono WAV as
// the app's DOWNLOAD IR writes it, and beside it its state as the app's preset — and index.tsv, a line for each.

#include <panel/Panel.h>
#include <synth/CabSynth.h>
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
             + (s.invert ? "inverted" : "normal") + "\t" + number (s.shiftMs, 3) + "\t" + Panel::labelOf (s).toStdString() + "\n";
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
    struct Rate { double hz; const char* folder; size_t n; };
    static constexpr Rate rates[] { { 44100.0, "44.1k", 1 << 14 }, { 48000.0, "48k", 1 << 14 }, { 96000.0, "96k", 1 << 15 } };
    const fsys::path out (argv[4]);
    int written = 0;
    for (const auto& [folder, items] : folders)
    {
        std::string tsv = "file\tstyle\tname\tsize\tback\thorn\ta_model\ta_x\ta_y\ta_distance_m\ta_angle_deg\t"
                          "b_model\tb_x\tb_y\tb_distance_m\tb_angle_deg\tblend\talign\tpolarity\tshift_ms\tlabel\n";
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
                const auto ir = synth::PairRenderer {}.render (table, it.state.box, synth::pairOf (it.state), r.n, r.hz).mix;
                const auto wav = synth::wav24 (ir, (int) r.hz);
                std::ofstream (dir / (it.file + ".wav"), std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
                std::ofstream (dir / (it.file + ".toml"), std::ios::binary) << it.text;
                ++written;
            }
        }
        std::printf ("%-8s %3zu cabinets x 3 rates\n", folder.c_str(), items.size());
    }
    std::printf ("%d IRs written, %d bad\n", written, bad);
    return bad == 0 ? 0 : 1;
}
