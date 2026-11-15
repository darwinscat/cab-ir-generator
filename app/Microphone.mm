// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#include "Microphone.h"

#import <AVFoundation/AVFoundation.h>

#include <utility>
#include <vector>

namespace cabir::app::microphone
{

namespace
{
    std::vector<std::function<void()>> waiting;   // message thread: everything that opens an input, until the answer
}

bool answered()
{
    return [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio] != AVAuthorizationStatusNotDetermined;
}

void ask (std::function<void()> then)
{
    if (answered() && waiting.empty())   // ...and none still in line for an answer just come: those first
    {
        then();
        return;
    }
    // macOS asks once however often it is asked; the first ask puts the question, the rest wait beside it.
    waiting.push_back (std::move (then));
    if (waiting.size() > 1)
        return;
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL)
                             {
                                 dispatch_async (dispatch_get_main_queue(), ^{
                                     auto all = std::exchange (waiting, {});
                                     for (auto& f : all)
                                         f();
                                 });
                             }];
}

} // namespace cabir::app::microphone
