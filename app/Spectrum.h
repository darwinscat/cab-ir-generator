// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <felitronics/analysis/MultiResSpectrumPane.h>
#include <felitronics/analysis/RollingSpectrumTap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace cabir::app
{

/** WHAT PLAYS, AS THE EQ SEES IT (the owner, 09.11): the cabinet's output, fed on the audio thread into a rolling tap —
    only while a window asks — and read on the message thread. */
class SpectrumFeed
{
public:
    /** The rate the audio runs at, from its prepare; the ring started again. Audio side. */
    void prepare (double rate) noexcept
    {
        rateNow = rate;
        hop = std::max (256, (int) std::lround (rate / 30.0));   // a frame about thirty times a second
        tap->reset();
    }

    /** A block of what plays, a channel or two (folded to one). Audio thread; nothing while no window asks. */
    void feed (const float* left, const float* right, int n) noexcept
    {
        if (! wanted.load (std::memory_order_relaxed))
            return;
        for (int i = 0; i < n; ++i)
            tap->push (right != nullptr ? 0.5f * (left[i] + right[i]) : left[i]);
        tap->publishIfDue (felitronics::analysis::RollingSpectrumTap::kMaxOrder, hop);
    }

    /** A window's ask: fed or not. Any thread. */
    void want (bool on) noexcept { wanted.store (on, std::memory_order_relaxed); }

    /** The latest frame, its order and the hop it came after, into `frame` (kMaxSize floats); false for none new. */
    bool pull (float* frame, int& order, int& hopSamples) noexcept { return tap->tryPull (frame, order, hopSamples); }
    double rate() const noexcept { return rateNow.load(); }

private:
    std::unique_ptr<felitronics::analysis::RollingSpectrumTap> tap = std::make_unique<felitronics::analysis::RollingSpectrumTap>();
    std::atomic<bool> wanted { false };
    std::atomic<double> rateNow { 48000.0 };
    int hop = 1600;
};

/** ...AND HOW IT IS DRAWN: tabbyEQ's pane — several FFT lengths at once, the lows from 16384 samples, so it is true down
    there — read in 1/24-octave bands at `hz`, tilted +4.5 dB an octave about 1 kHz as tabbyEQ tilts it, so a guitar's
    falling top reads level. Message thread. */
class SpectrumView
{
public:
    explicit SpectrumView (std::vector<float> hz) : at (std::move (hz)) {}

    /** A tick: the feed's newest frame taken in, else the last held and, starved, faded. The columns, dBFS. */
    std::vector<float> tick (SpectrumFeed& feed)
    {
        int order = 0, hopSamples = 0;
        if (feed.pull (frame.data(), order, hopSamples) && order == pane->frameOrder())
        {
            std::copy (frame.begin(), frame.begin() + (1 << order), pane->frameInput());
            pane->coverSamples = hopSamples;
            pane->ingest (order);
        }
        else
            pane->starve();
        std::vector<float> db (at.size());
        for (size_t i = 0; i < at.size(); ++i)
            db[i] = (float) pane->readDb (at[i], feed.rate(), 4.5, 1000.0);
        return db;
    }

    void reset() { pane->reset(); }

private:
    std::vector<float> at;
    std::unique_ptr<felitronics::analysis::MultiResSpectrumPane> pane = std::make_unique<felitronics::analysis::MultiResSpectrumPane>();
    std::vector<float> frame = std::vector<float> ((size_t) felitronics::analysis::RollingSpectrumTap::kMaxSize);
};

} // namespace cabir::app
