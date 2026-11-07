// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PAGE'S MODULE CHECKED AGAINST THE APP'S BUILD. Built twice — native, and to WebAssembly run by node — it renders
// states through the page's own C ABI (src/web/CabirWeb.cpp) and writes what came out; the two files are then compared.
// The two builds do not agree to the bit — their libm differ, and so does what the compiler fuses — so the check is a
// bound: the IRs within -120 dB of their peak, the band's curves within 0.01 dB, the level within 0.001 dB.
//
//   irdump <cabinets.txt> <out.bin> <state.toml>...     render each state, write the IR, the curves and the level
//   irdump --compare <a.bin> <b.bin>                     the worst of each over all the states; fails past a bound

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C"
{
    int cabir_table (const char*);
    int cabir_render (const char*, int);
    const float* cabir_mix();
    int cabir_points();
    const float* cabir_response();
    const float* cabir_comb();
    float cabir_gain (double);
}

namespace
{
    struct Record
    {
        std::string name;
        std::vector<float> mix, response, comb;
        float gain = 1;
    };

    void putU32 (std::ostream& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.put ((char) (v >> (8 * i))); }
    void putFloats (std::ostream& o, const std::vector<float>& v)
    {
        putU32 (o, (uint32_t) v.size());
        for (float f : v)
        {
            uint32_t u;
            std::memcpy (&u, &f, 4);
            putU32 (o, u);
        }
    }
    bool getU32 (std::istream& in, uint32_t& v)
    {
        unsigned char b[4];
        if (! in.read ((char*) b, 4)) return false;
        v = (uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24;
        return true;
    }
    bool getFloats (std::istream& in, std::vector<float>& v)
    {
        uint32_t n = 0;
        if (! getU32 (in, n) || n > (1u << 24)) return false;
        v.resize (n);
        for (auto& f : v)
        {
            uint32_t u;
            if (! getU32 (in, u)) return false;
            std::memcpy (&f, &u, 4);
        }
        return true;
    }

    std::vector<Record> read (const char* path)
    {
        std::ifstream in (path, std::ios::binary);
        std::vector<Record> all;
        for (;;)
        {
            Record r;
            uint32_t n = 0;
            if (! getU32 (in, n) || n > 4096) break;
            r.name.resize (n);
            in.read (r.name.data(), n);
            std::vector<float> gain;
            if (! getFloats (in, r.mix) || ! getFloats (in, r.response) || ! getFloats (in, r.comb) || ! getFloats (in, gain) || gain.size() != 1)
                break;
            r.gain = gain[0];
            all.push_back (std::move (r));
        }
        return all;
    }

    int compare (const char* pathA, const char* pathB)
    {
        const auto a = read (pathA), b = read (pathB);
        if (a.empty() || a.size() != b.size())
        {
            std::printf ("FAIL: %zu states against %zu\n", a.size(), b.size());
            return 1;
        }
        double worstIr = -400, worstCurve = 0, worstGain = 0;
        std::string atIr, atCurve, atGain;
        int bad = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto& x = a[i];
            const auto& y = b[i];
            if (x.name != y.name || x.mix.size() != y.mix.size() || x.response.size() != y.response.size() || x.comb.size() != y.comb.size())
            {
                std::printf ("FAIL: %s — not the same shape\n", x.name.c_str());
                ++bad;
                continue;
            }
            double peak = 0, diff = 0;
            for (size_t k = 0; k < x.mix.size(); ++k)
            {
                peak = std::max (peak, (double) std::fabs (x.mix[k]));
                diff = std::max (diff, (double) std::fabs (x.mix[k] - y.mix[k]));
            }
            const double ir = diff > 0 ? 20.0 * std::log10 (diff / std::max (peak, 1e-30)) : -400.0;
            if (ir > worstIr) { worstIr = ir; atIr = x.name; }
            for (size_t k = 0; k < x.response.size(); ++k)
                if (const double d = std::fabs (x.response[k] - y.response[k]); d > worstCurve) { worstCurve = d; atCurve = x.name; }
            for (size_t k = 0; k < x.comb.size(); ++k)
                if (const double d = std::fabs (x.comb[k] - y.comb[k]); d > worstCurve) { worstCurve = d; atCurve = x.name; }
            const double g = std::fabs (20.0 * std::log10 ((double) x.gain / (double) y.gain));
            if (g > worstGain) { worstGain = g; atGain = x.name; }
        }
        std::printf ("%zu states\n", a.size());
        std::printf ("IR      worst %8.1f dB against its peak   (bound -120)   %s\n", worstIr, atIr.c_str());
        std::printf ("curves  worst %8.5f dB                  (bound 0.01)   %s\n", worstCurve, atCurve.c_str());
        std::printf ("level   worst %8.5f dB                  (bound 0.001)  %s\n", worstGain, atGain.c_str());
        const bool ok = bad == 0 && worstIr <= -120.0 && worstCurve <= 0.01 && worstGain <= 0.001;
        std::printf ("%s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }

    std::string text (const char* path)
    {
        std::ifstream in (path, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }
}

int main (int argc, char** argv)
{
    if (argc == 4 && std::strcmp (argv[1], "--compare") == 0)
        return compare (argv[2], argv[3]);
    if (argc < 4)
    {
        std::printf ("irdump <cabinets.txt> <out.bin> <state.toml>...\nirdump --compare <a.bin> <b.bin>\n");
        return 2;
    }
    if (cabir_table (text (argv[1]).c_str()) == 0)
    {
        std::printf ("no cabinets in %s\n", argv[1]);
        return 1;
    }
    std::ofstream out (argv[2], std::ios::binary);
    int bad = 0;
    for (int i = 3; i < argc; ++i)
    {
        const auto toml = text (argv[i]);
        const int n = cabir_render (toml.c_str(), 0);
        if (n <= 0)
        {
            std::printf ("BAD  %s: not a state\n", argv[i]);
            ++bad;
            continue;
        }
        std::string name = argv[i];
        name = name.substr (name.find_last_of ("/\\") + 1);
        const int points = cabir_points();
        const float* comb = cabir_comb();
        putU32 (out, (uint32_t) name.size());
        out.write (name.data(), (std::streamsize) name.size());
        putFloats (out, std::vector<float> (cabir_mix(), cabir_mix() + n));
        putFloats (out, std::vector<float> (cabir_response(), cabir_response() + points));
        putFloats (out, comb != nullptr ? std::vector<float> (comb, comb + points) : std::vector<float>());
        putFloats (out, { cabir_gain (48000.0) });
    }
    std::printf ("%d states written, %d bad\n", argc - 3 - bad, bad);
    return bad == 0 ? 0 : 1;
}
