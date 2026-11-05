# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

cmake_minimum_required(VERSION 3.22)

# Before project(): the oldest macOS the app runs on (left unset it is the build machine's own), and the
# C++ runtime inside the Windows .exe so a zip runs alone. Both slices of a macOS release, Apple silicon
# and Intel, are asked for by release.yml, so a build while we work compiles one.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the binaries run on")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(cab_ir_generator VERSION 0.1.0 LANGUAGES C CXX)

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
    add_executable(presetcheck tools/presetcheck.cpp)
    target_link_libraries(presetcheck PRIVATE cabir_synth)
endif()

# THE TESTS — no JUCE: the state's text, read and written.
option(CABIR_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
if(CABIR_TESTS)
    enable_testing()
    add_executable(cabir_state_test tests/StateTests.cpp)
    target_link_libraries(cabir_state_test PRIVATE cabir_synth)
    target_compile_definitions(cabir_state_test PRIVATE CABIR_TABLE="${CMAKE_CURRENT_SOURCE_DIR}/data/cabinets.txt")
    add_test(NAME state COMMAND cabir_state_test)
endif()

# THE APP — Cab IR Generator itself: the panel, a player through the family's convolver, the family's version window.
option(CABIR_APP "Build the standalone app" ${PROJECT_IS_TOP_LEVEL})
if(CABIR_APP)
    # JUCE, pinned as the family's plugins pin it; a local tree first.
    set(CABIR_JUCE_TAG "8.0.16-security" CACHE STRING "JUCE git tag to build against")
    set(CABIR_JUCE_DIR "$ENV{HOME}/IdeaProjects/orbit-capture/app/tests/build/_deps/juce-src"
        CACHE PATH "Local JUCE source tree (used if present); else the pin is fetched")
    if(CABIR_JUCE_DIR AND EXISTS "${CABIR_JUCE_DIR}/CMakeLists.txt")
        message(STATUS "JUCE: local tree — ${CABIR_JUCE_DIR}")
        FetchContent_Declare(juce SOURCE_DIR "${CABIR_JUCE_DIR}")
    else()
        FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${CABIR_JUCE_TAG} GIT_SHALLOW TRUE)
    endif()
    FetchContent_MakeAvailable(juce)

    # felitronics-core — the convolver the plugins play their cabinets through, on pffft.
    set(FELITRONICS_WITH_PFFFT ON CACHE BOOL "the cabinet's convolver on the pffft SIMD FFT" FORCE)
    set(CABIR_CORE_TAG "v0.61.2" CACHE STRING "felitronics-core release tag (fetched when no local checkout)")
    set(CABIR_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-core" CACHE PATH "Local felitronics-core checkout (used if present)")
    if(CABIR_CORE_DIR AND EXISTS "${CABIR_CORE_DIR}/CMakeLists.txt")
        FetchContent_Declare(felitronics_core SOURCE_DIR "${CABIR_CORE_DIR}")
        set(cabirCoreVersion "local")
    else()
        FetchContent_Declare(felitronics_core GIT_REPOSITORY https://github.com/darwinscat/felitronics-core.git GIT_TAG ${CABIR_CORE_TAG} GIT_SHALLOW TRUE)
        set(cabirCoreVersion "${CABIR_CORE_TAG}")
    endif()
    FetchContent_MakeAvailable(felitronics_core)

    # felitronics-appkit — the brand, the version window, the audio settings.
    set(CABIR_APPKIT_TAG "v0.22.0" CACHE STRING "felitronics-appkit release tag (fetched when no local checkout)")
    set(CABIR_APPKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-appkit" CACHE PATH "Local felitronics-appkit checkout (used if present)")
    if(CABIR_APPKIT_DIR AND EXISTS "${CABIR_APPKIT_DIR}/CMakeLists.txt")
        FetchContent_Declare(felitronics_appkit SOURCE_DIR "${CABIR_APPKIT_DIR}")
        set(cabirAppkitVersion "local")
    else()
        FetchContent_Declare(felitronics_appkit GIT_REPOSITORY https://github.com/darwinscat/felitronics-appkit.git GIT_TAG ${CABIR_APPKIT_TAG} GIT_SHALLOW TRUE)
        set(cabirAppkitVersion "${CABIR_APPKIT_TAG}")
    endif()
    FetchContent_MakeAvailable(felitronics_appkit)

    if(CABIR_TOML_DIR AND EXISTS "${CABIR_TOML_DIR}/CMakeLists.txt")
        set(cabirTomlVersion "local")
    else()
        set(cabirTomlVersion "${CABIR_TOML_TAG}")
    endif()
    execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE cabirGitHash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    juce_add_gui_app(CabIrGenerator
        PRODUCT_NAME  "Cab IR Generator"
        COMPANY_NAME  "Darwin's Cat"
        BUNDLE_ID     com.darwinscat.cabirgenerator
        ICON_BIG      "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo/icon.png"   # the logo's mark, drawn from mark.svg
        VERSION       ${PROJECT_VERSION})
    target_sources(CabIrGenerator PRIVATE app/Main.cpp app/MainComponent.cpp app/Engine.cpp)
    file(GLOB cabirPresets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/presets/*.toml")   # the factory's presets
    juce_add_binary_data(CabIrData SOURCES data/cabinets.txt assets/logo/mark.svg ${cabirPresets})
    target_compile_definitions(CabIrGenerator PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        CABIR_VERSION="${PROJECT_VERSION}"
        CABIR_GIT_HASH="${cabirGitHash}"
        CABIR_CORE_VERSION="${cabirCoreVersion}"
        CABIR_TOML_VERSION="${cabirTomlVersion}"
        CABIR_APPKIT_VERSION="${cabirAppkitVersion}")
    target_link_libraries(CabIrGenerator PRIVATE
        CabIrData
        cabir::panel
        felitronics::convolution
        felitronics::appkit
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()
