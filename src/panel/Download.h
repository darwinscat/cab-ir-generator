// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

#pragma once

// DOWNLOAD IR IN A FORMAT (the owner, 06.11): many a floor unit's IR loader resamples nothing — it wants 44.1 kHz, often
// 16 bits, and no more than so many samples. The cabinet is synthesised again at the rate (no resampling), cut to the
// length with its last quarter faded (synth::shortened) and written at the bits (synth::wavPcm) — the app's and the
// page's DOWNLOAD IR alike, the same name for the file and the same line under the button.

#include "Panel.h"

#include <string>
#include <vector>

namespace cabir
{

struct DownloadFormat
{
    int rate = 48000, bits = 24, samples = 0;   // samples: 0, the whole IR (100 ms)

    bool usual() const { return rate == 48000 && bits == 24 && samples == 0; }

    /** One of the formats offered: 44.1, 48 or 96 kHz; 24 or 16 bits; the whole IR, 2048, 1024 or 512 samples. */
    static bool offered (int rate, int bits, int samples)
    {
        return (rate == 44100 || rate == 48000 || rate == 96000) && (bits == 16 || bits == 24)
            && (samples == 0 || samples == 512 || samples == 1024 || samples == 2048);
    }
};

/** The cabinet `s` as a WAV in `f`, saying what `meta` says. */
inline std::vector<unsigned char> downloadWav (const synth::Table& table, const State& s, const DownloadFormat& f, const synth::WavMeta& meta)
{
    const auto ir = synth::PairRenderer {}.render (table, s.box, synth::pairOf (s), f.rate > 48000 ? (size_t) 1 << 15 : (size_t) 1 << 14,
                                                   (double) f.rate).mix;
    return synth::wavPcm (synth::shortened (ir, (size_t) f.samples), f.rate, f.bits, &meta);
}

/** What a download in `f` will be, the line under the button: "44.1 kHz, 16 bit, 1024 samples", "48 kHz, 24 bit, 100 ms" — plain
    words and commas (the owner, 06.11: no exotic symbols in the text). */
inline std::string formatNote (const DownloadFormat& f)
{
    return std::string (f.rate == 44100 ? "44.1" : f.rate == 96000 ? "96" : "48") + " kHz, " + std::to_string (f.bits) + " bit, "
         + (f.samples > 0 ? std::to_string (f.samples) + " samples" : std::string ("100 ms"));
}

/** A download's file name, without ".wav": the cabinet as its line says it, the dots gone — "8x10 MD421+RE20" — and the
    format after it where it is not the usual one: "8x10 MD421+RE20 44k 16bit 1024". Not yet legal for a file system; a
    host makes it so. */
inline std::string downloadName (const State& s, const DownloadFormat& f)
{
    auto name = Panel::labelOf (s).toStdString();
    for (size_t at; (at = name.find (" \xc2\xb7 ")) != std::string::npos;)
        name.replace (at, 4, " ");
    if (! f.usual())
        name += std::string (" ") + (f.rate == 44100 ? "44k" : f.rate == 96000 ? "96k" : "48k") + " " + std::to_string (f.bits) + "bit"
              + (f.samples > 0 ? " " + std::to_string (f.samples) : std::string());
    return name;
}

} // namespace cabir
