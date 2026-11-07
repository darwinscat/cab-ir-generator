; SPDX-License-Identifier: AGPL-3.0-or-later
; Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.
;
; THE WINDOWS INSTALLER (Inno Setup 6) — one for each instrument and architecture: the app, its VST3 and its CLAP.
; Unsigned for now: SmartScreen warns ("Windows protected your PC", More info, Run anyway) until code signing comes.
;
;   ISCC.exe /DInstrument=Bass /DArch=x64 /DAppVersion=0.2.0 /DSourceDir=<staged files> installer\cabbuilder.iss
;
; SourceDir holds what release.yml stages: "Orbit <Instrument> Cab Builder.exe", VST3\<its .vst3 folder>,
; CLAP\<its .clap file>, LICENSE. Out comes Orbit<Instrument>CabBuilder-<version>-Windows-<arch>.exe in installer\output.

#ifndef Instrument
  #define Instrument "Bass"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir names the staged files
#endif

#define AppName   "Orbit " + Instrument + " Cab Builder"
#define FileStem  "Orbit" + Instrument + "CabBuilder"

; Each instrument's own AppId, the same on both architectures: what Windows knows the product by, for upgrades and
; for uninstalling. Never change one once released.
#if Instrument == "Bass"
  #define AppGuid "{{DC0415B6-F840-45F7-A61E-E969DA9226DC}"
  #define AppURL  "https://darwinscat.com/felitronics/orbit-bass-cab-builder"
#elif Instrument == "Guitar"
  #define AppGuid "{{98EE6DED-EE32-4166-873E-961B0E300517}"
  #define AppURL  "https://darwinscat.com/felitronics/orbit-guitar-cab-builder"
#else
  #error Instrument is Bass or Guitar
#endif

[Setup]
AppId={#AppGuid}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Darwin's Cat
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename={#FileStem}-{#AppVersion}-Windows-{#Arch}
LicenseFile={#SourceDir}\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppName}.exe
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#elif Arch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
  #error Arch is x64 or arm64
#endif
; The plugins go to Program Files\Common Files, where every host looks: that takes an administrator.
PrivilegesRequired=admin

[Components]
Name: "app";  Description: "The app";     Types: full custom
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom

[Files]
Source: "{#SourceDir}\{#AppName}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; A VST3 is a folder: its whole contents into the shared VST3 place.
Source: "{#SourceDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
; A CLAP is one file on Windows.
Source: "{#SourceDir}\CLAP\{#AppName}.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: app

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"
Type: files;          Name: "{commoncf64}\CLAP\{#AppName}.clap"
