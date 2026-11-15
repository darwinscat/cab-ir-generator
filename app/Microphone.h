// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <functional>

namespace cabir::app::microphone
{

/** THE MICROPHONE QUESTION, on the Mac: JUCE 9's CoreAudio holds an input's opening (AudioDeviceCreateIOProcIDWithBlock)
    until the player has answered macOS's question, the message thread stopped, the window not yet shown on a first run
    or frozen under the question later. So no input opens before the answer: asked through AVFoundation, which waits for
    nothing, and the input opened once it comes, whatever it is (a refused microphone reads silence, as on JUCE 8). Off
    the Mac, and in a plugin (the host's track, no device of its own), nothing is asked. */
#if defined(__APPLE__) && ! CABIR_PLUGIN
bool answered();                       // the player has said yes or no, so an input opens without waiting
void ask (std::function<void()> then); // `then` on the message thread once answered: at once, if it was
#else
inline bool answered() { return true; }
inline void ask (std::function<void()> then) { then(); }
#endif

} // namespace cabir::app::microphone
