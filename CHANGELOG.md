<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Orbit Bass Cab Builder** and **Orbit Guitar Cab Builder** (Cab IR Generator up to
0.1.5) are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Removed
- **The guitar's Clean style**: its twenty presets were jazz and country, far from the rock the builder is for. A clean
  sound for rock stays in Blues and Indie, on the same open 1x12 and 2x12. 140 presets in seven styles, and the TOP 12.
- **The guitar's presets cut to forty**: four styles of ten, Blues, Rock (Classic Rock and Hard Rock in one), Metal (Metal
  and Modern Metal in one) and Punk; Indie gone. The TOP 12 stay, each one of the forty.
- **The bass's presets cut to forty**, the guitar's four styles: Rock, Metal and Punk ten each from the factory's, the
  twelve OrbitAmp plays among them (Modern's three in Metal); Funk, Jazz, Modern, Motown and Reggae gone.

### Added
- **The bass's Blues**: ten of the old blues rigs, a 1x15 flip-top, 2x15s, a tweed 4x10 with an open back, small 1x12,
  2x12 and 2x10 combos, no horn, vintage mics (U47, ribbon, RE20), each off its pits.

## [0.2.0] — 2026-11-08 — two builders, their plugins, and a window in 16:9

### Added
- **Two downloads a platform, one for each builder**, each with its app and its plugins:
  OrbitBassCabBuilder-0.2.0-macOS.pkg and OrbitGuitarCabBuilder-0.2.0-macOS.pkg (the app, VST3, AU and CLAP; signed and
  notarized, Apple silicon and Intel), -Windows-x64.exe and -Windows-arm64.exe (the app, VST3 and CLAP), and
  -Linux-x86_64.tar.gz and -Linux-arm64.tar.gz (the same, with how to install them).
- **The guitar's TOP 12**: a dozen of rock, metal and punk, bright and dense, most of them on the 4x12 — a preset folder
  of its own.
- **A "?" on the response**: the hand on it opens what a comb is, why it matters on this instrument and how the builder
  treats it; a click keeps it open, a tap does on a touch screen.
- **BYPASS A, B, A+B** in the strip: one mic taken out, or both.
- **The builder's name across the response**, BASS CAB or GUITAR CAB, pale, brighter under the hand — the two are never
  taken one for the other.
- **Orbit Guitar Cab Builder**, a second app from the same code, on the guitar's own table: seven boxes (4x12 closed,
  a tall half-closed 2x12, open 2x12, 1x12, 4x10, 2x10 and 1x10), six mics with the E906 among them, a row of six
  speakers under the back (MID, BRIGHT, DARK, SCOOP, FLAT, SNAP) and a paper cone for each (SPEAKER clicked), a
  half-closed back (HALF) and a wall behind an open box (WALL).
  Its own name, icon, settings, presets folder and page, darwinscat.com/felitronics/orbit-guitar-cab-builder. The bass's
  app sounds exactly as it did.
- **The guitar's presets**: 160 in eight styles, Blues, Classic Rock, Hard Rock, Metal, Modern Metal, Punk, Indie and
  Clean, twenty each, every box, speaker and back among them; and 17 typical cabinets, one per construction.
- **The plugins**, VST3, AU and CLAP, for both: the same window in your DAW, the track played through the cabinet,
  mono or stereo, with no latency. The DAW's project keeps A/B/C/D with their histories and the window's size, and the
  cabinet plays with the window closed. HEAR, FIND, the presets (the app's own, from the same folder) and DOWNLOAD IR
  are there; the sample player is the app's alone.
- **The page's module plays the guitar too**: cabir_catalog ("guitar") turns its panel, its links and what its WAVs
  say to the guitar's; cabir_download_note takes the state, so its line says how long the state's IR is; the build
  puts both tables beside the module (cabinets.txt, cabinets-guitar.txt), and demo.html?guitar shows it.
- **INPUT in the apps**: an audio interface played live through the cabinet, no DAW needed. It is the first item of
  the sample menu, silent until PLAY (Space), so a laptop's own mic by its speakers never howls; AUDIO picks the input
  device and its channel, the first one open heard. macOS asks for the microphone the first time it is picked.

### Changed
- **The window in 16:9**, 960 x 540: the boxes at the left, the speaker and the two mics' columns at the right, the
  response across under them. The backs are pictures, a word under the one the hand is on; CAP, EDGE or OFF under the
  hand shows the dot where it would put its mic; a mic's distance and angle stay out while the hand goes to them.
- **On the guitar a comb from 2 kHz up is a tone, not a fault**: the line turns red there only past 6 dB, below 2 kHz
  past 3 dB as before, and FIND BEST POSITION judges the same. Some metal, modern metal and hard rock presets keep a
  light comb in the top on purpose. The bass is unchanged.
- **The guitar's line reddens with a pit from 5 dB**: its speakers' own dips run deeper than a bass's.
- **The names: Orbit Bass Cab Builder** (Cab IR Generator until now) **and Orbit Guitar Cab Builder** — the apps, their
  windows, the version window, the WAVs they write and the IR packs. A first run under the new name copies the
  workspace, the settings and your own presets from where the old name kept them; nothing is moved or deleted.
- **The page's new address**, darwinscat.com/felitronics/orbit-bass-cab-builder: the app's links, DOWNLOAD IR's WAVs
  (their comment, the link that opens the cabinet live), the page's own downloads and the IR packs' WAVs and
  index.html all name it. The old addresses lead there, the cabinet in the link kept, so the WAVs already out still open.

### Fixed
- A click on a mic, or on its distance and angle, no longer reaches the horn's steps under it.
- A link or a state naming a mic the builder has not opens the default cabinet instead of an empty one.

## [0.1.5] — 2026-11-06 — DOWNLOAD IR in the format a loader takes

### Added
- **DOWNLOAD IR in a format.** A ▾ beside the button: 44.1, 48 or 96 kHz; 24 or 16 bits; the whole IR or 2048, 1024
  or 512 samples — for a floor unit that resamples nothing and takes no longer an IR. The cabinet is synthesised again at
  that rate, cut with its last quarter faded, and the line under the button says what the next download will be; the
  choice is kept. The usual one, 48 kHz and 24 bits whole, is the very file it always was. The page can offer the same.

### Changed
- **The IR packs' pictures** are titled «ROCK · 20 BASS CABINETS from Darwin's Cat», what made them on the line under.
- **The packs' WAVs name their program without a version**, so a release that changes no sound leaves every one of them
  the same file.

## [0.1.4] — 2026-11-06 — no crash on an older version's cabinet, the IR packs by size

### Fixed
- **0.1.3 crashed on its start** where 0.1.2 had been used: the cabinets it kept were named by the boxes' old names,
  which 0.1.3 no longer knows. A cabinet of a box it does not know — kept by an older version, in a preset file, from a
  page — is now set aside for the default one, and nothing renders a box it does not know.

### Added
- **The IR packs by size too.** Beside a pack for each style, one for each box size — `size/8x10/`, `size/4x10/` and so
  on — listing every cabinet of that size, of every style and the typical ones, with the same `index.tsv` columns and a
  last one saying which style's folder holds its files, and its own picture. A player with one 8x10 takes all the 8x10s.

## [0.1.3] — 2026-11-06 — packs that show their cabinets, boxes named by their size

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
- **Every box is named by its size and its family** — `4x10-classic`, `8x8-round`, `2x15-light` — and by no maker's
  model, in a preset, in a WAV's preset or in a link. The sound is the same, sample for sample. A preset written by 0.1.2
  or earlier keeps the old name and does not open in this one.

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
