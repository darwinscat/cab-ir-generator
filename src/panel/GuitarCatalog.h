// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

#pragma once

#include "Panel.h"

namespace cabir
{

/** THE GUITAR'S CABINETS, as the panel draws them (Panel::Catalog) — the guitar's app hands it in before its first panel
    (Panel::useCatalog). Geometry from data/guitar/cabinets.txt: metres, the baffle's centre the origin, up positive. No
    horn, no port: the four twelves' box sealed, the tall 2x12 half closed, the others open at the back, the back's switch
    (CLOSED HALF OPEN PORTED) turning any of them.
    Its mics the bass's but the kick drum's and the big valve one, and the flat one made for a guitar's cabinet; its
    speakers by character, each box with its own (the table's "speaker-<name>"), any of them on any box. */
inline const Panel::Catalog& guitarCatalog()
{
    using P = ui::Point<float>;
    static const Panel::Catalog guitar {
      {
        { "4x12-closed", 0, 0.76f, 0.76f, { P (-0.185f, 0.185f), P (0.185f, 0.185f), P (-0.185f, -0.185f), P (0.185f, -0.185f) },
          0.125f, false, {}, false, {}, "sm57", false, "mid" },
        { "2x12-vertical", 0, 0.545f, 0.70f, { P (0, 0.165f), P (0, -0.165f) }, 0.125f, false, {}, false, {}, "sm57", false, "dark" },
        { "2x12-open",   0, 0.70f, 0.50f, { P (-0.17f, 0), P (0.17f, 0) }, 0.125f, false, {}, false, {}, "sm57", false, "bright" },
        { "1x12-open",   0, 0.60f, 0.48f, { P (0, -0.02f) }, 0.125f, false, {}, false, {}, "sm57", false, "dark" },
        { "4x10-open",   0, 0.62f, 0.62f, { P (-0.145f, 0.145f), P (0.145f, 0.145f), P (-0.145f, -0.145f), P (0.145f, -0.145f) },
          0.105f, false, {}, false, {}, "sm57", false, "snap" },
        { "2x10-open",   0, 0.60f, 0.46f, { P (-0.14f, 0), P (0.14f, 0) }, 0.105f, false, {}, false, {}, "sm57", false, "snap" },
        { "1x10-open",   0, 0.46f, 0.40f, { P (0, 0) }, 0.105f, false, {}, false, {}, "sm57", false, "snap" },
      },
      // The small on the big, the way a backline stands.
      { { "4x12-closed", "1x10-open" }, { "2x12-vertical" }, { "2x12-open", "2x10-open" }, { "4x10-open", "1x12-open" } },
      { "sm57", "e906", "md421", "re20", "u87", "ribbon" },
      { "SM57", "E906", "MD421", "RE20", "U87", "RIBB" },
      { "4x12-closed" },
      { "2x12-open", "1x12-open", "4x10-open", "2x10-open", "1x10-open" },
      { "mid", "bright", "dark", "scoop", "flat", "snap" },
      { "MID", "BRIGHT", "DARK", "SCOOP", "FLAT", "SNAP" },
      { "2x12-vertical" },
      { 0, 3, 2, 1 },
      true,
      "paper",
      150.0f, 1.0f };
    return guitar;
}

} // namespace cabir
