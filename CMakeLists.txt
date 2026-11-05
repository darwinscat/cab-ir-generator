# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

cmake_minimum_required(VERSION 3.22)
project(cab_ir_generator VERSION 0.1.0 LANGUAGES CXX)

# THE SYNTHESIS — JUCE-free and header-only: a cabinet table in, an IR out (src/synth/CabSynth.h).
add_library(cabir_synth INTERFACE)
add_library(cabir::synth ALIAS cabir_synth)
target_include_directories(cabir_synth INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(cabir_synth INTERFACE cxx_std_20)

# THE CABINETS the synthesis reads: a host embeds the file the way it embeds its other data.
set(CABIR_CABINETS "${CMAKE_CURRENT_SOURCE_DIR}/data/cabinets.txt" CACHE INTERNAL "The cabinet table the synthesis reads")

# THE PANEL — JUCE views (src/panel): the cabinets, the mics on the speaker, the response, and the miniature a host's
# cabinet block draws. The host supplies the JUCE modules it links (juce_gui_basics, juce_audio_formats); the panel never links JUCE
# itself, and draws in the host's colours and type (Look).
add_library(cabir_panel INTERFACE)
add_library(cabir::panel ALIAS cabir_panel)
target_sources(cabir_panel INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/panel/Look.cpp"
                                     "${CMAKE_CURRENT_SOURCE_DIR}/src/panel/Response.cpp")
target_include_directories(cabir_panel INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cabir_panel INTERFACE cabir_synth)

# THE TOOLS — no JUCE: cabgen renders a table to files, irlook reads an IR's response back.
option(CABIR_TOOLS "Build cabgen and irlook" ${PROJECT_IS_TOP_LEVEL})
if(CABIR_TOOLS)
    add_executable(cabgen tools/cabgen.cpp)
    target_link_libraries(cabgen PRIVATE cabir_synth)
    add_executable(irlook tools/irlook.cpp)
    target_link_libraries(irlook PRIVATE cabir_synth)
endif()
