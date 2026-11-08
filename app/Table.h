// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <BinaryData.h>
#include <synth/CabSynth.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cabir::app
{
/** THE TABLE THE APP PLAYS: the cabinets' text with the options switched off left out (a listening build's SETUP; every
    other build has them all on). One table for each choice, kept for the run, so a worker still reading the last one
    reads on. */
struct PlayedTable
{
    static PlayedTable& get()
    {
        static PlayedTable t;
        return t;
    }

    const synth::Table& table()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        std::string key;
        for (const auto& o : off) key += o + "\n";
        auto& t = tables[key];
        if (t == nullptr)
            t = std::make_unique<synth::Table> (synth::parseTable (text(), off));
        return *t;
    }

    std::vector<std::string> optionsOff()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return off;
    }

    void setOptionsOff (std::vector<std::string> now)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        off = std::move (now);
    }

    /** The table's options, name and label, in its order. */
    static std::vector<std::pair<std::string, std::string>> options() { return synth::optionsOf (text()); }

private:
    static std::string text() { return std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize); }

    std::mutex mutex;
    std::vector<std::string> off;
    std::map<std::string, std::unique_ptr<synth::Table>> tables;
};

inline const synth::Table& playedTable() { return PlayedTable::get().table(); }
}
