# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

cmake_minimum_required(VERSION 3.22)
project(cab_ir_generator VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# felitronics-toml — the state's text (src/synth/State.h): a local checkout beside this one first, else the release.
set(CABIR_TOML_TAG "v0.3.0" CACHE STRING "felitronics-toml release tag (fetched when no local checkout)")
set(CABIR_TOML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-toml"
    CACHE PATH "Local felitronics-toml checkout (used if present); else the pinned release is fetched")
if(NOT TARGET felitronics::toml)
    if(CABIR_TOML_DIR AND EXISTS "${CABIR_TOML_DIR}/CMakeLists.txt")
        message(STATUS "felitronics-toml: local checkout — ${CABIR_TOML_DIR}")
        FetchContent_Declare(felitronics_toml SOURCE_DIR "${CABIR_TOML_DIR}")
    else()
        message(STATUS "felitronics-toml: fetching ${CABIR_TOML_TAG} from GitHub")
        FetchContent_Declare(felitronics_toml
            GIT_REPOSITORY https://github.com/darwinscat/felitronics-toml.git
            GIT_TAG        ${CABIR_TOML_TAG}
            GIT_SHALLOW    TRUE)
    endif()
    FetchContent_MakeAvailable(felitronics_toml)
endif()

# THE SYNTHESIS — JUCE-free and header-only: a cabinet table in, an IR out (src/synth/CabSynth.h), and the state the
# generator makes, as TOML (src/synth/State.h).
add_library(cabir_synth INTERFACE)
add_library(cabir::synth ALIAS cabir_synth)
target_include_directories(cabir_synth INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(cabir_synth INTERFACE cxx_std_20)
target_sources(cabir_synth INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/synth/State.cpp")
target_link_libraries(cabir_synth INTERFACE felitronics::toml)

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

# THE TESTS — no JUCE: the state's text, read and written.
option(CABIR_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
if(CABIR_TESTS)
    enable_testing()
    add_executable(cabir_state_test tests/StateTests.cpp)
    target_link_libraries(cabir_state_test PRIVATE cabir_synth)
    add_test(NAME state COMMAND cabir_state_test)
endif()
