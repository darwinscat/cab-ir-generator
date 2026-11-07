# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Cab IR Generator — see LICENSE.

cmake_minimum_required(VERSION 3.22)

# Before project(): the oldest macOS the app runs on (left unset it is the build machine's own), and the
# C++ runtime inside the Windows .exe so a zip runs alone. Both slices of a macOS release, Apple silicon
# and Intel, are asked for by release.yml, so a build while we work compiles one.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the binaries run on")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(cab_ir_generator VERSION 0.1.5 LANGUAGES C CXX)

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
target_compile_definitions(cabir_synth INTERFACE CABIR_VERSION="${PROJECT_VERSION}")   # what a WAV says made it (synth::WavMeta)
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

# A page's build (emscripten) makes the page's module alone: no JUCE there, and the tools are the native build's.
if(EMSCRIPTEN)
    set(cabirNative OFF)
else()
    set(cabirNative ${PROJECT_IS_TOP_LEVEL})
endif()

# THE TOOLS — no JUCE: cabgen renders a table to files, irlook reads an IR's response back.
option(CABIR_TOOLS "Build cabgen and irlook" ${cabirNative})
if(CABIR_TOOLS)
    add_executable(cabgen tools/cabgen.cpp)
    target_link_libraries(cabgen PRIVATE cabir_synth)
    add_executable(irlook tools/irlook.cpp)
    target_link_libraries(irlook PRIVATE cabir_synth)
    add_executable(presetcheck tools/presetcheck.cpp)
    target_link_libraries(presetcheck PRIVATE cabir_synth)
    add_executable(cabpack tools/cabpack.cpp)   # the IR packs a page offers (tools/cabpack.sh)
    target_link_libraries(cabpack PRIVATE cabir_synth)
    add_executable(respread tools/respread.cpp)   # the presets' pairs spread apart, judged as the panel draws them
    target_link_libraries(respread PRIVATE cabir_synth)
endif()

# THE TESTS — no JUCE: the state's text, read and written.
option(CABIR_TESTS "Build the tests" ${cabirNative})
if(CABIR_TESTS)
    enable_testing()
    add_executable(cabir_state_test tests/StateTests.cpp)
    target_link_libraries(cabir_state_test PRIVATE cabir_synth)
    target_compile_definitions(cabir_state_test PRIVATE CABIR_TABLE="${CMAKE_CURRENT_SOURCE_DIR}/data/cabinets.txt")
    add_test(NAME state COMMAND cabir_state_test)
endif()

# THE APPS — Orbit Bass Cab Builder and Orbit Guitar Cab Builder: the panel, a player through the family's convolver, the
# family's version window.
option(CABIR_APP "Build the standalone app" ${cabirNative})
option(CABIR_WEB "Build the page's module (src/web) and irdump, the check of it against the native build" ${PROJECT_IS_TOP_LEVEL})

# felitronics-core — the convolver the plugins play their cabinets through, on pffft; the page's module asks it only
# the level it plays an IR at, on core's own FFT.
if((CABIR_APP OR CABIR_WEB) AND NOT TARGET felitronics::convolution)
    if(EMSCRIPTEN)
        set(FELITRONICS_WITH_PFFFT OFF CACHE BOOL "the page's module on core's own FFT" FORCE)
    else()
        set(FELITRONICS_WITH_PFFFT ON CACHE BOOL "the cabinet's convolver on the pffft SIMD FFT" FORCE)
    endif()
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
endif()

# THE PAGE'S MODULE — the synthesis behind a C ABI (src/web/CabirWeb.cpp). Built native too, under irdump, which renders
# the factory presets through it: the same call from both builds, written to a file, the two files compared.
if(CABIR_WEB)
    add_library(cabir_web_abi STATIC src/web/CabirWeb.cpp)
    target_link_libraries(cabir_web_abi PUBLIC cabir_synth felitronics::convolution)
    add_executable(irdump tools/irdump.cpp)
    target_link_libraries(irdump PRIVATE cabir_web_abi)
    if(EMSCRIPTEN)
        target_link_options(irdump PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node)
        add_executable(cabir_web src/web/CabirWeb.cpp src/web/PanelWeb.cpp)
        target_link_libraries(cabir_web PRIVATE cabir_synth felitronics::convolution)
        set_target_properties(cabir_web PROPERTIES OUTPUT_NAME cabir SUFFIX ".mjs")
        target_link_options(cabir_web PRIVATE
            -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createCabir -sENVIRONMENT=web,worker,node
            -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0
            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32','HEAPF64','HEAPU8','HEAPU32','UTF8ToString','stringToNewUTF8']"
            "-sEXPORTED_FUNCTIONS=['_malloc','_free']")
    endif()
endif()

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

    # One app, built for each instrument (app/Flavour.h): its name, its bundle, its logo — the icon and the mark the header
    # draws, from `logo` (assets/logo, the bass's, or assets/logo/guitar) — and its data: the cabinet table, the factory's
    # presets and the riffs the player carries, from `data` (the bass's) or `data/guitar`.
    function(cabir_add_app target logo data dataTarget riffsTarget)
        cmake_parse_arguments(APP "GUITAR" "PRODUCT;BUNDLE;DESCRIPTION" "" ${ARGN})
        juce_add_gui_app(${target}
            PRODUCT_NAME  "${APP_PRODUCT}"
            DESCRIPTION   "${APP_DESCRIPTION}"
            COMPANY_NAME  "Darwin's Cat"
            BUNDLE_ID     ${APP_BUNDLE}
            ICON_BIG      "${CMAKE_CURRENT_SOURCE_DIR}/${logo}/icon.png"   # the logo's mark, drawn from mark.svg
            VERSION       ${PROJECT_VERSION})
        target_sources(${target} PRIVATE app/Main.cpp app/MainComponent.cpp app/Engine.cpp)
        file(GLOB presets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${data}/presets/*.toml")   # the factory's presets
        juce_add_binary_data(${dataTarget} SOURCES ${data}/cabinets.txt ${logo}/mark.svg ${presets})
        file(GLOB riffs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${data}/riffs/*.flac")       # the riffs the player carries
        juce_add_binary_data(${riffsTarget} HEADER_NAME RiffData.h NAMESPACE RiffData SOURCES ${riffs})
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            CABIR_PLUGIN=0
            CABIR_GUITAR=$<BOOL:${APP_GUITAR}>
            CABIR_GIT_HASH="${cabirGitHash}"
            CABIR_CORE_VERSION="${cabirCoreVersion}"
            CABIR_TOML_VERSION="${cabirTomlVersion}"
            CABIR_APPKIT_VERSION="${cabirAppkitVersion}")
        target_link_libraries(${target} PRIVATE
            ${dataTarget}
            ${riffsTarget}
            cabir::panel
            felitronics::convolution
            felitronics::appkit
            juce::juce_gui_extra
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
    endfunction()

    cabir_add_app(OrbitBassCabBuilder assets/logo data CabIrData CabIrRiffs
        PRODUCT "Orbit Bass Cab Builder" BUNDLE com.darwinscat.orbitbasscabbuilder
        DESCRIPTION "Bass cabinet IR generator and cab sim")

    # THE GUITAR'S — the same app on the guitar's cabinets (data/guitar, src/panel/GuitarCatalog.h).
    option(CABIR_GUITAR_APP "Build the guitar's app too" ON)
    if(CABIR_GUITAR_APP)
        cabir_add_app(OrbitGuitarCabBuilder assets/logo/guitar data/guitar GuitarCabIrData GuitarCabIrRiffs GUITAR
            PRODUCT "Orbit Guitar Cab Builder" BUNDLE com.darwinscat.orbitguitarcabbuilder
            DESCRIPTION "Guitar cabinet IR generator and cab sim")
    endif()

    # THE PLUGINS — the same window in a host's, the track through the cabinet (app/Plugin.h): VST3, AU and CLAP, one for
    # each instrument, on the app's data (`dataTarget`, its cabinets and presets; no riffs, the track plays).
    option(CABIR_PLUGINS "Build the plugins (VST3, AU, CLAP)" ON)
    if(CABIR_PLUGINS)
        # CLAP via the MIT clap-juce-extensions wrapper, pinned as the family's plugins pin it.
        set(CABIR_CLAP_EXT_TAG "51a9359315298de632cf44e9d7524940868441e6" CACHE STRING "clap-juce-extensions git commit to build against")
        FetchContent_Declare(clap-juce-extensions GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git GIT_TAG ${CABIR_CLAP_EXT_TAG})
        FetchContent_MakeAvailable(clap-juce-extensions)

        # 4-char codes, kept once released: the family's maker (Dcat), the plugin's own (`code`).
        function(cabir_add_plugin target logo dataTarget code)
            cmake_parse_arguments(PLUG "GUITAR" "PRODUCT;BUNDLE;DESCRIPTION" "" ${ARGN})
            juce_add_plugin(${target}
                PRODUCT_NAME                "${PLUG_PRODUCT}"
                DESCRIPTION                 "${PLUG_DESCRIPTION}"
                COMPANY_NAME                "Darwin's Cat"
                BUNDLE_ID                   ${PLUG_BUNDLE}
                PLUGIN_MANUFACTURER_CODE    Dcat
                PLUGIN_CODE                 ${code}
                FORMATS                     VST3 AU
                VST3_CATEGORIES             "Fx"
                AU_MAIN_TYPE                "kAudioUnitType_Effect"
                IS_SYNTH                    FALSE
                NEEDS_MIDI_INPUT            FALSE
                NEEDS_MIDI_OUTPUT           FALSE
                IS_MIDI_EFFECT              FALSE
                EDITOR_WANTS_KEYBOARD_FOCUS FALSE
                COPY_PLUGIN_AFTER_BUILD     FALSE
                ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/${logo}/icon.png"
                VERSION                     ${PROJECT_VERSION})
            clap_juce_extensions_plugin(TARGET ${target}
                CLAP_ID       "${PLUG_BUNDLE}"
                CLAP_FEATURES audio-effect mono stereo)
            # The CLAP wrapper's own icon is the generic CLAP logo: the one juceaide made for the JUCE formats over it.
            if(APPLE)
                add_custom_command(TARGET ${target}_CLAP POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy
                            "$<TARGET_PROPERTY:${target},JUCE_ICON_FILE>"
                            "$<TARGET_BUNDLE_DIR:${target}_CLAP>/Contents/Resources/clap.icns"
                    VERBATIM)
            endif()
            target_sources(${target} PRIVATE app/Plugin.cpp app/MainComponent.cpp app/Engine.cpp)
            target_compile_definitions(${target} PUBLIC
                JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                JUCE_VST3_CAN_REPLACE_VST2=0
                JUCE_DISPLAY_SPLASH_SCREEN=0
                CABIR_PLUGIN=1
                CABIR_GUITAR=$<BOOL:${PLUG_GUITAR}>
                CABIR_GIT_HASH="${cabirGitHash}"
                CABIR_CORE_VERSION="${cabirCoreVersion}"
                CABIR_TOML_VERSION="${cabirTomlVersion}"
                CABIR_APPKIT_VERSION="${cabirAppkitVersion}")
            target_link_libraries(${target} PRIVATE
                ${dataTarget}
                cabir::panel
                felitronics::convolution
                felitronics::appkit
                juce::juce_gui_extra
                juce::juce_audio_utils
                juce::juce_audio_formats
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
        endfunction()

        cabir_add_plugin(OrbitBassCabBuilderPlugin assets/logo CabIrData Cirb
            PRODUCT "Orbit Bass Cab Builder" BUNDLE com.darwinscat.orbitbasscabbuilder.plugin
            DESCRIPTION "Bass cabinet IR generator and cab sim")
        if(CABIR_GUITAR_APP)
            cabir_add_plugin(OrbitGuitarCabBuilderPlugin assets/logo/guitar GuitarCabIrData Cirg GUITAR
                PRODUCT "Orbit Guitar Cab Builder" BUNDLE com.darwinscat.orbitguitarcabbuilder.plugin
                DESCRIPTION "Guitar cabinet IR generator and cab sim")
        endif()
    endif()
endif()
