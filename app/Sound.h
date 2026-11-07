// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include <juce_events/juce_events.h>

#include <string>
#include <vector>

namespace cabir::app
{

/** WHAT THE WINDOW PLAYS ITS CABINET THROUGH: the app's player (Engine), or a plugin's processor (Plugin.h). */
class Sound
{
public:
    virtual ~Sound() = default;

    /** The cabinet: its IR, mono, at `rate`. Message thread. */
    virtual void setIr (std::vector<float> ir, double rate) = 0;

    /** A load the convolver turned away mid-fade, offered again. Message thread, often. */
    virtual void flush() = 0;

    /** BYPASS: the signal heard as it is, the cabinet out, faded either way. Any thread. */
    virtual void setBypass (bool on) = 0;
};

/** A PLUGIN'S SESSION: the window's in a plugin, where the track plays and the host keeps the project. Its workspace —
    A/B/C/D, their histories, the one that plays — is kept here, and the state that plays beside it as TOML, so the plugin
    plays its cabinet with no window open. A change message is sent when the host hands in a project of its own. */
class Session : public Sound, public juce::ChangeBroadcaster
{
public:
    /** The workspace as the window last kept it (CompareHistory's tree, XML); empty for none. Message thread. */
    virtual juce::String workspace() const = 0;
    /** ...kept, with the state that plays (its TOML). Message thread. */
    virtual void keepWorkspace (const juce::String& xml, const std::string& playing) = 0;
    /** The format it runs as, for the version window: VST3, AU, CLAP. */
    virtual juce::String format() const = 0;
};

} // namespace cabir::app
