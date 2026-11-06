<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Cab IR Generator** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Every IR pack shows its cabinets and links them.** Each cabinet's response as the panel draws it — the line green
  where it is fine and red into a pit, and with two mics the bands where they meet, green where they add up and red where
  they cancel — under its name and what it is: its size, its back, its horn, its mics and how much of each, with a few
  words saying what the line and the bands are. One picture a cabinet in `responses/`, and all of a pack's together in
  `responses.svg`; a click on one opens it in the generator. And `index.tsv` ends in a `link` column: the same link, for
  each cabinet, to open it and change it.

### Changed
- **The typical pack's mics, spread as the presets' are.** Its A stood where DEFAULT puts it, and only B was moved — so
  a horn box's 4–5 kHz dip and a ported box's low one stayed. Now A first moves off its pit, then B goes where the two
  meet without a deep comb and the sum without a pit: 28 of its 30 cabinets stay green, against 17.

### Fixed
- **A link with a value that is no number** — `shift=nan`, `blend=inf`, a mic's place as `nan` — opens no cabinet, as
  any broken link does; it used to give the page an empty answer.

## [0.1.2] — 2026-11-06 — a WAV that says what made it, and a cabinet as a link

### Added
- **A cabinet as a link.** The page's address with the cabinet after its `#` —
  `darwinscat.com/felitronics/cab-ir-generator#cab=1&box=8x10&back=closed&horn=off&a=re20,0,0,5,0&…` — opens the live
  panel on exactly that cabinet: the box by its size, its back, its horn, both mics with their places, BLEND, ALIGN, Ø
  and TIME, short enough to read and to change by hand.
- **Every WAV says what made it**: DOWNLOAD IR, the page's download and every file of the IR packs carry, after the sound,
  the cabinet's name, Darwin's Cat, the program and its version, the link that opens the cabinet on the page — and the
  whole cabinet as its preset, so it can be opened again from the file. The sound is the same, sample for sample; a
  loader that knows nothing of the rest skips it.

## [0.1.1] — 2026-11-06 — presets where the response stays green

### Changed
- **The factory presets, respread.** The response's line turns red from a 3 dB pit, and many presets showed it. Each
  single-mic preset now stands, about where it stood, where its response has no pit: of 80, 69 are clear, and none falls
  past 6 dB. For a pair, A first moves off a pit of its own the same way, then B to where the two meet without a deep
  comb: 76 of 80 pairs are clear, the other four by a hair.

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
