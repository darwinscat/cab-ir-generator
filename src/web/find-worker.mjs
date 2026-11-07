// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// ONE OF FIND'S WORKERS (demo.html): the page's module, the table once, then batches of places to judge for a state —
// each place's score sent back as it is judged, for the map; the search itself stays in C++ (cabir_find_*).

import createCabir from './cabir.mjs';

let module = null;
const ready = createCabir ().then ((m) => { module = m; });

onmessage = async ({ data }) =>
{
    await ready;
    const m = module;
    if (data.table !== undefined)   // the page's catalog first (its boxes, mics, speakers and rooms), then the table
    {
        if (data.catalog !== undefined)
            m.ccall ('cabir_catalog', 'number', ['string'], [data.catalog]);
        m.ccall ('cabir_table', 'number', ['string'], [data.table]);
        return;
    }
    const { job, toml, places, first } = data;
    for (let i = 0; i < places.length / 3; ++i)
    {
        const x = places[3 * i], y = places[3 * i + 1], d = places[3 * i + 2];
        const score = m.ccall ('cabir_find_score', 'number', ['string', 'number', 'number', 'number'], [toml, x, y, d]);
        const shift = m.ccall ('cabir_find_shift', 'number', [], []);
        postMessage ({ job, index: first + i, x, y, score, shift });
    }
    postMessage ({ job, done: true });
};
