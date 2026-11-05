<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Cab IR Generator** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.1.0] — 2026-11-05 — the first release

Bass cabinet IRs, synthesized while you place the mics: a standalone app for macOS, Windows and Linux,
and the same panel for the web page.

### Added
- **Ten boxes**, drawn to scale and stacked as a backline stands them: 1x15, 1x12, 8x10, 4x10, 2x10,
  6x10, 2x12, 2x15, 4x6 and 8x8.
- **The back, closed, ported or open**, each drawn cut through from the side. The box's own is marked
  with a dot; a back you turn stays turned from box to box, and a sealed box turned ported gets a port.
- **The horn's attenuator**: OFF, −12, −6, 0.
- **Two mics, A and B**, each one of MD421, SM57, RE20, D112, U87, U47 and a ribbon — dragged on the
  speaker, taken by any part of the mic, or put at CAP, EDGE, OFF or ROOM with one click; distance 1 to
  30 cm, angle up to 60°. A mic turns off with a click on its lit name and comes back with a click in
  its dark column.
- **BLEND, ALIGN, Ø and TIME** for the pair. A double click sends TIME back to nought and BLEND back to
  the middle.
- **FIND.** With two mics, B moves to where the two comb least; with one, that mic moves, about where it
  stands, to where its response has no pit. Every place tried is drawn on the speaker as it goes.
- **The response**, 30 Hz to 12 kHz, following the hand: green where it is fine, red where it falls into a
  pit or the two mics cancel, with the band of where they meet under it and the response before your
  last change as a faint grey line.
- **DOWNLOAD IR**: the cabinet that plays as a 24-bit mono WAV at 48 kHz.
- **160 presets** in eight styles — Funk, Jazz, Metal, Modern, Motown, Punk, Reggae and Rock — with
  the two mics of a pair standing apart, where they meet without a deep comb.
- **A player to listen by**: riffs that come with the app, or a sample of your own dropped on the window,
  played through the cabinet as a plugin of the family plays it. Space plays and stops; the arrow keys
  step through the presets.
- **HEAR**: A+B, A or B alone, or BYPASS — what you listen through, nothing of the preset changed.
- **Undo, redo and A/B/C/D**: four cabinets kept side by side; right-click a register to copy the one
  playing into it.
- **Under the hand**, whatever a click works is lit: a box or a back framed, a word brighter, a mic on
  the speaker ringed.
- **The page's module**: the synthesis and the same panel in WebAssembly, FIND spread over the page's
  workers.
- **IR packs** for the page: every preset and the typical cabinets, at 44.1, 48 and 96 kHz.

### Dependencies
- `felitronics-core` **v0.61.2**, `felitronics-appkit` **v0.22.0**, `felitronics-toml` **v0.3.0**,
  JUCE **8.0.16**.
