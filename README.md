<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Cab IR Generator

Bass cabinet impulse responses, synthesized: a box of each size, its back closed, ported or open, a
horn switched in and out, and up to two mics placed on the speaker — kind, spot, distance, angle and
blend — rendered to a 48 kHz IR while you move them, with the response and where two mics comb drawn
as you go.

One code base for three homes:

- the cabinet panel and its miniature inside Darwin's Cat's bass amps (OrbitBass, RedGrooveBass);
- a standalone app for Windows, macOS and Linux, with a sample player to audition by ear, history,
  undo/redo and A/B/C/D;
- a live web page.

## Layout

- `src/synth/` — the synthesis, JUCE-free: a cabinet table in, an IR out.
- `data/cabinets.txt` — the cabinets, mics and positions the synthesis reads.
- `src/panel/` — the panel and the miniature, JUCE views; colours and type come from the host (`Look`).
- `tools/` — `cabgen` renders a table to files, `irlook` reads an IR's response back.

## Licence

AGPL-3.0-or-later — see [LICENSE](LICENSE). Contributions: see [CONTRIBUTING.md](CONTRIBUTING.md).
